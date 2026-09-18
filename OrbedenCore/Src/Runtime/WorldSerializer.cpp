#include <cctype>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "FileSystem/FileSystem.h"
#include "FileSystem/Utf8Path.h"
#include "Log/Log.h"
#include "Runtime/WorldSerializer.h"
#include "Runtime/Reflection.h"
#include "ResourceManager/ResourceManager.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Object/Script.h"

namespace
{
    //记录最近一次 World 加载缺失的组件类型，供 Editor 区分待编译与文件损坏。
    std::string lastUnregisteredComponentType;

    //XML Token 类型
    enum class XmlTokenKind
    {
        StartElement,
        EndElement,
    };

    //定义 XML Token
    struct XmlToken
    {
    public:
        XmlTokenKind kind = XmlTokenKind::StartElement;
        std::string name;
        std::unordered_map<std::string, std::string> attributes;
        bool emptyElement = false;
    };

    //转义 XML 属性文本
    std::string EscapeXml(const std::string& value)
    {
        std::string result;
        result.reserve(value.size());

        for (char ch : value)
        {
            switch (ch)
            {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&apos;"; break;
            default: result += ch; break;
            }
        }

        return result;
    }

    //还原 XML 属性文本
    std::string UnescapeXml(const std::string& value)
    {
        std::string result;
        result.reserve(value.size());

        for (usize index = 0; index < value.size(); ++index)
        {
            if (value[index] != '&')
            {
                result += value[index];
                continue;
            }

            if (value.compare(index, 5, "&amp;") == 0)
            {
                result += '&';
                index += 4;
            }
            else if (value.compare(index, 4, "&lt;") == 0)
            {
                result += '<';
                index += 3;
            }
            else if (value.compare(index, 4, "&gt;") == 0)
            {
                result += '>';
                index += 3;
            }
            else if (value.compare(index, 6, "&quot;") == 0)
            {
                result += '"';
                index += 5;
            }
            else if (value.compare(index, 6, "&apos;") == 0)
            {
                result += '\'';
                index += 5;
            }
            else
            {
                result += '&';
            }
        }

        return result;
    }

    //读取 XML 属性
    const std::string& GetAttribute(const XmlToken& token, const std::string& name)
    {
        static const std::string empty;

        auto it = token.attributes.find(name);
        return it == token.attributes.end() ? empty : it->second;
    }

    //判断 XML 名称字符
    bool IsNameChar(char ch)
    {
        return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-' || ch == ':' || ch == '.';
    }

    //读取 XML Token 流
    class XmlReader
    {
    private:
        std::string text;
        usize position = 0;

    public:
        //创建 XML 读取器
        explicit XmlReader(std::string content)
            : text(std::move(content))
        {
        }

        //读取下一个 XML 元素 Token
        bool Next(XmlToken& token)
        {
            while (position < text.size())
            {
                //跳到下一个元素起点
                usize open = text.find('<', position);
                if (open == std::string::npos)
                {
                    position = text.size();
                    return false;
                }

                position = open + 1;
                if (position >= text.size()) return false;

                //跳过注释
                if (text.compare(open, 4, "<!--") == 0)
                {
                    usize close = text.find("-->", position + 3);
                    if (close == std::string::npos) return false;
                    position = close + 3;
                    continue;
                }

                //跳过 XML 声明和 DOCTYPE 等控制节点
                if (text[position] == '?' || text[position] == '!')
                {
                    usize close = text.find('>', position);
                    if (close == std::string::npos) return false;
                    position = close + 1;
                    continue;
                }

                //读取结束元素
                if (text[position] == '/')
                {
                    position++;
                    token = XmlToken();
                    token.kind = XmlTokenKind::EndElement;
                    token.name = ReadName();

                    usize close = text.find('>', position);
                    if (close == std::string::npos) return false;
                    position = close + 1;
                    return !token.name.empty();
                }

                //读取开始元素
                token = XmlToken();
                token.kind = XmlTokenKind::StartElement;
                token.name = ReadName();
                if (token.name.empty()) return false;

                //读取元素属性
                while (position < text.size())
                {
                    SkipWhitespace();
                    if (position >= text.size()) return false;

                    if (text[position] == '>')
                    {
                        position++;
                        return true;
                    }

                    if (text[position] == '/' && position + 1 < text.size() && text[position + 1] == '>')
                    {
                        position += 2;
                        token.emptyElement = true;
                        return true;
                    }

                    std::string attributeName = ReadName();
                    if (attributeName.empty()) return false;

                    SkipWhitespace();
                    if (position >= text.size() || text[position] != '=') return false;
                    position++;
                    SkipWhitespace();

                    if (position >= text.size() || (text[position] != '"' && text[position] != '\'')) return false;
                    char quote = text[position++];
                    usize valueStart = position;
                    while (position < text.size() && text[position] != quote)
                    {
                        position++;
                    }

                    if (position >= text.size()) return false;

                    std::string value = text.substr(valueStart, position - valueStart);
                    position++;
                    token.attributes[attributeName] = UnescapeXml(value);
                }

                return false;
            }

            return false;
        }

        //跳过当前元素的剩余内容
        bool SkipElement(const std::string& elementName)
        {
            XmlToken token;
            uint32 depth = 1;
            while (Next(token))
            {
                if (token.kind == XmlTokenKind::StartElement && !token.emptyElement)
                {
                    depth++;
                }
                else if (token.kind == XmlTokenKind::EndElement)
                {
                    if (depth == 0) return false;
                    depth--;
                    if (depth == 0) return token.name == elementName;
                }
            }

            return false;
        }

    private:
        //跳过空白字符
        void SkipWhitespace()
        {
            while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])))
            {
                position++;
            }
        }

        //读取 XML 名称
        std::string ReadName()
        {
            SkipWhitespace();
            usize start = position;
            while (position < text.size() && IsNameChar(text[position]))
            {
                position++;
            }

            return text.substr(start, position - start);
        }
    };

    //输出序列化错误
    void LogSerializerError(const std::string& message)
    {
        Log::Error(message.c_str());
    }

    //判断是否是World内部对象引用
    bool IsWorldObjectRef(const std::string& path)
    {
        return path.size() >= 8 && path.compare(0, 8, "world://") == 0;
    }

    //写入缩进
    void WriteIndent(std::ostream& output, int depth)
    {
        for (int index = 0; index < depth; ++index)
        {
            output << "    ";
        }
    }

    //写入单个组件和持久化字段
    void WriteComponent(std::ostream& output, Component* component, int depth)
    {
        if (!component) return;

        //写入组件起始节点
        Type* type = component->GetType();
        WriteIndent(output, depth);
        output << "<Component type=\"" << EscapeXml(type->GetName()) << "\" stableId=\""
            << EscapeXml(component->GetInstanceId().GetPath()) << "\">\n";

        Script* script = component->Cast<Script>();
        if (script)
        {
            WriteIndent(output, depth + 1);
            output << "<Field name=\"domain\" type=\"ScriptDomain\" value=\""
                << (script->GetDomain() == ScriptDomain::Managed ? "Managed" : "Native") << "\" />\n";
            if (script->IsManagedHost())
            {
                WriteIndent(output, depth + 1);
                output << "<Field name=\"managedTypeName\" type=\"string\" value=\""
                    << EscapeXml(script->GetManagedTypeName()) << "\" />\n";
            }
        }

        //按反射元数据写入持久化字段
        List<const Reflection::FieldInfo*> fields;
        Reflection::CollectFields(type, fields);
        for (const Reflection::FieldInfo* field : fields)
        {
            if (!field || !field->persistent || !field->getter) continue;

            std::string fieldValue = field->getter(component);
            if (field->kind == Reflection::FieldKind::EnsId)
            {
                EnsId id;
                if (Reflection::SetFromXmlValue(id, fieldValue))
                {
                    Ens* target = component->GetWorld()->GetEns(id);
                    fieldValue = target ? target->GetInstanceId().GetPath() : "";
                }
            }
            WriteIndent(output, depth + 1);
            output << "<Field name=\"" << EscapeXml(field->name ? field->name : "") << "\" type=\""
                << EscapeXml(field->typeName ? field->typeName : "") << "\" value=\""
                << EscapeXml(fieldValue) << "\" />\n";
        }

        if (script && script->IsManagedHost())
        {
            for (const ManagedScriptField& field : script->GetManagedFields())
            {
                WriteIndent(output, depth + 1);
                output << "<Field name=\"" << EscapeXml(field.name) << "\" type=\""
                    << EscapeXml(field.typeName) << "\" value=\"" << EscapeXml(field.value)
                    << "\" inspectorVisible=\"" << (field.inspectorVisible ? "true" : "false") << "\" />\n";
            }
        }

        WriteIndent(output, depth);
        output << "</Component>\n";
    }

    //递归写入 Ens 层级
    void WriteEns(std::ostream& output, Ens& ens, int depth)
    {
        Transform* transform = ens.Transform();
        if (!transform) return;

        //写入当前 Ens 和它的组件
        WriteIndent(output, depth);
        output << "<Ens stableId=\"" << EscapeXml(ens.GetInstanceId().GetPath()) << "\" name=\""
            << EscapeXml(ens.GetName()) << "\" localActive=\""
            << (ens.GetLocalActive() ? "true" : "false") << "\">\n";

        for (Component* component : ens.GetComponents())
        {
            WriteComponent(output, component, depth + 1);
        }

        //按变换组件链表写入子级 Ens
        EnsId child = transform->firstChild;
        while (!child.IsNull())
        {
            Ens* childEns = ens.GetWorld() ? ens.GetWorld()->GetEns(child) : nullptr;
            Transform* childTransform = childEns ? childEns->Transform() : nullptr;
            EnsId nextChild = childTransform ? childTransform->next : EnsId();
            if (childEns)
            {
                WriteEns(output, *childEns, depth + 1);
            }
            child = nextChild;
        }

        WriteIndent(output, depth);
        output << "</Ens>\n";
    }

    //把 XML Field 节点应用到组件
    bool ApplyField(Component* component, const XmlToken& token)
    {
        if (!component) return false;

        const std::string& name = GetAttribute(token, "name");
        const std::string& typeName = GetAttribute(token, "type");
        const std::string& value = GetAttribute(token, "value");
        if (name.empty()) return false;

        Script* script = component->Cast<Script>();
        if (script && name == "domain")
        {
            const char* expected = script->GetDomain() == ScriptDomain::Managed ? "Managed" : "Native";
            return value == expected;
        }
        if (script && script->IsManagedHost() && name == "managedTypeName")
        {
            return script->SetManagedTypeName(value);
        }

        const Reflection::FieldInfo* field = Reflection::FindField(component->GetType(), name);
        if (field && field->persistent && field->setter)
        {
            if (field->kind == Reflection::FieldKind::EnsId && (value.empty() || IsWorldObjectRef(value))) return true;
            return field->setter(component, value);
        }
        if (!script || !script->IsManagedHost()) return true;

        Reflection::FieldKind kind = Script::GetManagedFieldKind(typeName);
        const std::string& visible = GetAttribute(token, "inspectorVisible");
        bool inspectorVisible = visible != "false" && visible != "0";
        return script->SetManagedField(name, typeName, kind, value, inspectorVisible);
    }

    //读取组件节点并应用字段
    bool ReadComponent(XmlReader& reader, World& world, Ens& ens, const XmlToken& startToken)
    {
        (void)world;

        //解析并校验组件类型
        const std::string& typeName = GetAttribute(startToken, "type");
        if (typeName.empty())
        {
            LogSerializerError("World XML Component is missing type attribute.");
            if (!startToken.emptyElement) reader.SkipElement(startToken.name);
            return false;
        }

        Type* type = Object::FindType(typeName);
        if (!type || !type->Is(Component::StaticType()))
        {
            lastUnregisteredComponentType = typeName;
            Log::Warning(("World XML component type is not registered yet: " + typeName
                + ". Build Game C++ if this component is defined by the project Native module.").c_str());
            if (!startToken.emptyElement) reader.SkipElement(startToken.name);
            return false;
        }
        if (!type->CanCreateObject())
        {
            LogSerializerError("World XML references abstract component type: " + typeName);
            if (!startToken.emptyElement) reader.SkipElement(startToken.name);
            return false;
        }

        //创建组件实例
        Component* component = type == Transform::StaticType() ? ens.Transform()
            : world.AddComponentInstance(ens.GetId(), type, GetAttribute(startToken, "stableId"));
        if (!component)
        {
            LogSerializerError("World XML failed to create component: " + typeName);
            if (!startToken.emptyElement) reader.SkipElement(startToken.name);
            return false;
        }

        if (type == Transform::StaticType())
        {
            const std::string& identity = GetAttribute(startToken, "stableId");
            Object* existing = identity.empty() ? nullptr : Object::FindObject(StringId(identity));
            if (existing && existing != component) return false;
            if (!identity.empty()) component->ChangeInstancePath(StringId(identity));
        }

        if (startToken.emptyElement) return true;

        //读取组件字段
        XmlToken token;
        while (reader.Next(token))
        {
            if (token.kind == XmlTokenKind::EndElement && token.name == startToken.name)
            {
                return true;
            }

            if (token.kind == XmlTokenKind::StartElement && token.name == "Field")
            {
                if (!ApplyField(component, token))
                {
                    LogSerializerError("World XML failed to apply field on component: " + typeName);
                    if (!token.emptyElement) reader.SkipElement(token.name);
                    return false;
                }

                if (!token.emptyElement && !reader.SkipElement(token.name))
                {
                    return false;
                }
                continue;
            }

            if (token.kind == XmlTokenKind::StartElement && !token.emptyElement)
            {
                reader.SkipElement(token.name);
            }
        }

        return false;
    }

    //读取 Ens 节点并递归读取子级
    bool ReadEns(XmlReader& reader, World& world, Ens* parent, const XmlToken& startToken)
    {
        //创建 Ens 并恢复父子关系
        const std::string& stableId = GetAttribute(startToken, "stableId");
        const std::string& name = GetAttribute(startToken, "name");
        const std::string& localActive = GetAttribute(startToken, "localActive");
        Ens* ens = stableId.empty() ? world.CreateEns(name) : world.CreateEnsWithStableId(stableId, name);
        if (!ens)
        {
            LogSerializerError("World XML failed to create Ens.");
            if (!startToken.emptyElement) reader.SkipElement(startToken.name);
            return false;
        }

        if (parent)
        {
            ens->SetParent(parent);
        }
        if (localActive == "false" || localActive == "0")
        {
            ens->SetLocalActive(false);
        }

        if (startToken.emptyElement) return true;

        //读取 Ens 内容
        XmlToken token;
        while (reader.Next(token))
        {
            if (token.kind == XmlTokenKind::EndElement && token.name == startToken.name)
            {
                return true;
            }

            if (token.kind == XmlTokenKind::StartElement && token.name == "Component")
            {
                if (!ReadComponent(reader, world, *ens, token))
                {
                    return false;
                }
                continue;
            }

            if (token.kind == XmlTokenKind::StartElement && token.name == "Ens")
            {
                if (!ReadEns(reader, world, ens, token))
                {
                    return false;
                }
                continue;
            }

            if (token.kind == XmlTokenKind::StartElement && !token.emptyElement)
            {
                reader.SkipElement(token.name);
            }
        }

        return false;
    }

    //读取 World 根节点内容
    bool ReadWorld(XmlReader& reader, World& world, const XmlToken& startToken)
    {
        if (startToken.emptyElement) return true;

        //读取根 Ens
        XmlToken token;
        while (reader.Next(token))
        {
            if (token.kind == XmlTokenKind::EndElement && token.name == startToken.name)
            {
                return true;
            }

            if (token.kind == XmlTokenKind::StartElement && token.name == "RenderSettings")
            {
                world.renderSettings.skybox.SetInstanceId(StringId(GetAttribute(token, "skybox")));
                const std::string& enabled = GetAttribute(token, "skyboxEnabled");
                if (!enabled.empty() && !Reflection::SetFromXmlValue(world.renderSettings.skyboxEnabled, enabled)) return false;
                const std::string& ambient = GetAttribute(token, "ambientColor");
                if (!ambient.empty() && !Reflection::SetFromXmlValue(world.renderSettings.ambientColor, ambient)) return false;
                if (!token.emptyElement && !reader.SkipElement(token.name)) return false;
                continue;
            }
            if (token.kind == XmlTokenKind::StartElement && token.name == "Ens")
            {
                if (!ReadEns(reader, world, nullptr, token))
                {
                    return false;
                }
                continue;
            }

            if (token.kind == XmlTokenKind::StartElement && !token.emptyElement)
            {
                reader.SkipElement(token.name);
            }
        }

        return false;
    }

    //扫描单个对象的资源Ref字段
    bool LoadResourceRefsFromObject(World& world, Object* object)
    {
        if (!object) return true;

        Script* host = object->Cast<Script>();
        if (host && host->IsManagedHost())
        {
            for (const ManagedScriptField& field : host->GetManagedFields())
            {
                if (field.kind != Reflection::FieldKind::ObjectRef || field.value.empty()
                    || IsWorldObjectRef(field.value) || !field.typeName.starts_with("Ref<")) continue;
                std::string name = field.typeName.substr(4, field.typeName.size() - 5);
                if (name.starts_with("Orbeden.")) name.erase(0, 8);
                Type* type = Object::FindType(name);
                if (!type || !ResourceManager::Load(type, field.value)) return false;
            }
        }

        const Reflection::TypeInfo* typeInfo = Reflection::FindTypeInfo(object->GetType());
        if (!typeInfo) return true;

        for (const Reflection::FieldInfo& field : typeInfo->fields)
        {
            bool isList = field.kind == Reflection::FieldKind::ObjectRefList;
            if ((!isList && field.kind != Reflection::FieldKind::ObjectRef) || !field.objectRefTypeName || !field.getter) continue;

            //引用列表逐条预加载，空槽跳过
            std::vector<std::string> keys;
            std::string value = field.GetValueAsString(object);
            if (!isList)
            {
                keys.push_back(value);
            }
            else
            {
                usize start = 0;
                while (true)
                {
                    usize separator = value.find(Reflection::ReferenceListSeparator, start);
                    usize length = separator == std::string::npos ? std::string::npos : separator - start;
                    keys.push_back(value.substr(start, length));
                    if (separator == std::string::npos) break;
                    start = separator + 1;
                }
            }

            Type* refType = Object::FindType(field.objectRefTypeName);
            if (!refType)
            {
                Log::Warning(("Resource Ref uses unknown type: " + std::string(field.objectRefTypeName)).c_str());
                return false;
            }

            for (const std::string& key : keys)
            {
                if (key.empty() || IsWorldObjectRef(key)) continue;
                if (!ResourceManager::Load(refType, key)) return false;
            }
        }
        return true;
    }

    //扫描World中所有组件的资源Ref字段
    bool LoadWorldResourceRefs(World& world)
    {
        bool success = true;
        const std::string& skybox = world.renderSettings.skybox.GetInstanceId().GetPath();
        if (!skybox.empty() && !ResourceManager::Load<Skybox>(skybox)) success = false;
        world.ForEachEns([&world, &success](Ens& ens)
            {
                for (Component* component : ens.GetComponents())
                {
                    if (!LoadResourceRefsFromObject(world, component)) success = false;
                }
            });
        return success;
    }
}

//从 XML 文件反序列化 World
std::string WorldSerializer::CaptureComponent(Component* component)
{
    std::ostringstream output;
    WriteComponent(output, component, 0);
    return output.str();
}

Component* WorldSerializer::RestoreComponent(Ens& ens, const std::string& snapshot, int32 index)
{
    XmlReader reader(snapshot);
    XmlToken token;
    if (!reader.Next(token) || token.name != "Component" || !ens.GetWorld()) return nullptr;
    usize oldCount = ens.GetComponents().size();
    bool success = ReadComponent(reader, *ens.GetWorld(), ens, token);
    if (ens.GetComponents().size() != oldCount + 1) return nullptr;
    Component* component = ens.GetComponents().back();
    if (!success || !ens.MoveComponent(component, index))
    {
        ens.RemoveComponent(component);
        return nullptr;
    }
    //恢复快照中以稳定路径保存的 EnsId 字段
    XmlReader referenceReader(snapshot);
    XmlToken reference;
    while (referenceReader.Next(reference))
    {
        if (reference.name != "Field" || GetAttribute(reference, "type") != "EnsId") continue;
        const Reflection::FieldInfo* field = Reflection::FindField(component->GetType(), GetAttribute(reference, "name"));
        if (!field || !field->setter || field->kind != Reflection::FieldKind::EnsId) continue;
        const std::string& path = GetAttribute(reference, "value");
        if (!path.empty() && !IsWorldObjectRef(path)) continue;
        Ens* target = path.empty() ? nullptr : ens.GetWorld()->FindEns(StringId(path));
        if (!field->setter(component, Reflection::ToXmlValue(target ? target->GetId() : EnsId())))
        {
            ens.RemoveComponent(component);
            return nullptr;
        }
    }
    if (!LoadResourceRefsFromObject(*ens.GetWorld(), component))
    {
        ens.RemoveComponent(component);
        return nullptr;
    }
    return component;
}

//获取最近一次 World 加载遇到的未注册组件类型。
const std::string& WorldSerializer::GetLastUnregisteredComponentType()
{
    return lastUnregisteredComponentType;
}

bool WorldSerializer::LoadXml(World& world, const std::string& path)
{
    std::string error;
    auto document = ReadDocument(path, error);
    auto prepared = document ? PrepareWorld(world, *document, error) : nullptr;
    if (!prepared)
    {
        Log::Error(error.c_str());
        return false;
    }
    world.CommitReplacement(*prepared);
    return true;
}

//将 World 序列化到 XML 文件
bool WorldSerializer::SaveXml(const World& world, const std::string& path)
{
    Reflection::RegisterGeneratedReflection();

    //创建目标目录
    std::filesystem::path filePath = Utf8Path::FromUtf8(path);
    if (filePath.has_parent_path())
    {
        std::filesystem::create_directories(filePath.parent_path());
    }

    //二进制模式：文本模式会把每个 \n 写成 \r\n，存一次盘就把整个场景文件的行尾翻掉，
    //没有改动的场景也会在版本控制里显示成全文件重写。
    std::ofstream output(filePath, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!output)
    {
        Log::Error(("Save world XML failed: " + path).c_str());
        return false;
    }

    //写入 World 根节点和所有根 Ens
    output << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    output << "<World>\n";

    output << "    <RenderSettings skybox=\"" << EscapeXml(world.renderSettings.skybox.GetInstanceId().GetPath())
        << "\" skyboxEnabled=\"" << (world.renderSettings.skyboxEnabled ? "true" : "false")
        << "\" ambientColor=\"" << EscapeXml(Reflection::ToXmlValue(world.renderSettings.ambientColor)) << "\" />\n";
    world.ForEachEns([&output](Ens& ens)
        {
            if (ens.GetParent()) return;
            WriteEns(output, ens, 1);
        });

    output << "</World>\n";
    output.flush();
    return static_cast<bool>(output);
}

struct WorldDocument
{
    List<XmlToken> tokens;
};

//解析内存中的层级文档
std::shared_ptr<WorldDocument> WorldSerializer::ParseDocument(const std::string& text, std::string& error)
{
    error.clear();
    auto document = std::make_shared<WorldDocument>();
    XmlReader reader(text);
    List<std::string> stack;
    XmlToken token;
    bool rootSeen = false;
    bool closed = false;
    while (reader.Next(token))
    {
        if (closed) { error = "Unexpected content after document root."; return nullptr; }
        if (token.kind == XmlTokenKind::StartElement)
        {
            if (!rootSeen)
            {
                if (token.name != "World" && token.name != "Prefab")
                { error = "Expected World or Prefab root."; return nullptr; }
                rootSeen = true;
            }
            if (!token.emptyElement) stack.push_back(token.name);
            else if (stack.empty()) closed = true;
        }
        else
        {
            if (stack.empty() || stack.back() != token.name)
            { error = "Mismatched XML element: " + token.name; return nullptr; }
            stack.pop_back();
            if (stack.empty()) closed = true;
        }
        document->tokens.push_back(token);
    }
    if (!rootSeen || !closed || !stack.empty())
    { error = "Incomplete XML document."; return nullptr; }
    return document;
}

//读取文档，不访问反射、资源和对象运行时
std::shared_ptr<WorldDocument> WorldSerializer::ReadDocument(const std::string& path, std::string& error)
{
    std::ifstream input(Utf8Path::FromUtf8(path), std::ios::binary);
    if (!input) { error = "Cannot read file: " + path; return nullptr; }
    std::ostringstream text;
    text << input.rdbuf();
    if (input.bad()) { error = "File read failed: " + path; return nullptr; }
    return ParseDocument(text.str(), error);
}

namespace
{
    //输出文档并替换对象身份及引用
    std::string WriteDocument(const WorldDocument& document,
        const std::unordered_map<std::string, std::string>& paths, bool clearExternal)
    {
        std::ostringstream output;
        output << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
        for (const XmlToken& token : document.tokens)
        {
            output << '<';
            if (token.kind == XmlTokenKind::EndElement)
            {
                output << '/' << token.name << ">\n";
                continue;
            }
            output << token.name;
            for (const auto& attribute : token.attributes)
            {
                std::string value = attribute.second;
                bool reference = token.name == "Field" && attribute.first == "value"
                    && (GetAttribute(token, "type").starts_with("Ref<")
                        || GetAttribute(token, "type") == "EnsId");
                if (attribute.first == "stableId" || reference)
                {
                    auto found = paths.find(value);
                    if (found != paths.end()) value = found->second;
                    else if (reference && clearExternal && IsWorldObjectRef(value)) value.clear();
                }
                output << ' ' << attribute.first << "=\"" << EscapeXml(value) << '"';
            }
            output << (token.emptyElement ? " />\n" : ">\n");
        }
        return output.str();
    }

    //恢复稳定身份表示的 Ens 引用
    bool ApplyEnsReferences(World& world, const WorldDocument& document,
        const std::unordered_map<std::string, std::string>& paths)
    {
        Component* component = nullptr;
        for (const XmlToken& token : document.tokens)
        {
            if (token.name == "Component")
            {
                component = nullptr;
                if (token.kind == XmlTokenKind::StartElement)
                {
                    std::string path = GetAttribute(token, "stableId");
                    auto found = paths.find(path);
                    if (found != paths.end()) path = found->second;
                    Object* object = Object::FindObject(StringId(path));
                    component = object ? object->Cast<Component>() : nullptr;
                }
            }
            if (!component || token.name != "Field" || GetAttribute(token, "type") != "EnsId") continue;
            std::string path = GetAttribute(token, "value");
            if (!path.empty() && !IsWorldObjectRef(path)) continue;
            auto found = paths.find(path);
            if (found != paths.end()) path = found->second;
            Ens* target = world.FindEns(StringId(path));
            const Reflection::FieldInfo* field = Reflection::FindField(component->GetType(), GetAttribute(token, "name"));
            if (field && field->setter && !field->setter(component,
                Reflection::ToXmlValue(target ? target->GetId() : EnsId()))) return false;
        }
        return true;
    }
}

//在主线程准备独立且未激活的世界
std::unique_ptr<World> WorldSerializer::PrepareWorld(const World& current,
    const WorldDocument& document, std::string& error)
{
    error.clear();
    lastUnregisteredComponentType.clear();
    if (document.tokens.empty() || document.tokens.front().name != "World")
    { error = "Expected World document."; return nullptr; }
    Reflection::RegisterGeneratedReflection();
    std::unordered_map<std::string, std::string> paths;
    for (const XmlToken& token : document.tokens)
    {
        if (token.kind != XmlTokenKind::StartElement) continue;
        if (token.name != "Ens" && token.name != "Component") continue;
        const std::string& original = GetAttribute(token, "stableId");
        if (original.empty()) continue;
        if (paths.contains(original))
        {
            error = "Duplicate object identity: " + original;
            return nullptr;
        }
        paths.emplace(original, "world://preparing/" + Object::GenerateUuidText());
    }
    auto prepared = std::make_unique<World>();
    prepared->PrepareReplacement(current);
    //只替换对象身份，Ref 字段保留目标文件中的稳定路径
    WorldDocument renamed = document;
    for (XmlToken& token : renamed.tokens)
    {
        auto identity = token.attributes.find("stableId");
        if (identity != token.attributes.end() && paths.contains(identity->second))
            identity->second = paths.at(identity->second);
    }
    XmlReader reader(WriteDocument(renamed, {}, false));
    XmlToken root;

    //构建期间把运行时对象归属到准备中的世界，否则组件在字段回调里生成的网格、贴图与材质
    //会挂到旧世界上，并在提交时随旧世界一起销毁，留下悬空指针。
    World* previousWorld = World::CurrentWorld();
    World::SetCurrentWorld(prepared.get());
    bool loaded = reader.Next(root) && ReadWorld(reader, *prepared, root)
        && ApplyEnsReferences(*prepared, document, paths) && LoadWorldResourceRefs(*prepared);
    World::SetCurrentWorld(previousWorld);

    if (!loaded)
    {
        error = lastUnregisteredComponentType.empty() ? "World content or resource validation failed."
            : "Unregistered component: " + lastUnregisteredComponentType;
        return nullptr;
    }
    for (const auto& entry : paths)
    {
        Object* object = Object::FindObject(StringId(entry.second));
        if (object && object->GetWorld() == prepared.get())
            prepared->preparedObjectPaths.emplace_back(object, StringId(entry.first));
    }
    return prepared;
}

//捕获完整 Ens 子树
std::string WorldSerializer::CaptureEns(Ens& ens)
{
    std::ostringstream output;
    output << "<Prefab version=\"1\">\n";
    WriteEns(output, ens, 1);
    output << "</Prefab>\n";
    return output.str();
}

//保存预制体并清空外部场景引用
bool WorldSerializer::SavePrefab(Ens& ens, const std::string& path, std::string& error)
{
    auto document = ParseDocument(CaptureEns(ens), error);
    if (!document) return false;
    std::unordered_map<std::string, std::string> paths;
    for (const XmlToken& token : document->tokens)
    {
        const std::string& id = GetAttribute(token, "stableId");
        if (!id.empty()) paths.emplace(id, id);
    }
    std::ofstream output(Utf8Path::FromUtf8(path), std::ios::binary | std::ios::trunc);
    if (!output) { error = "Cannot create prefab: " + path; return false; }
    output << WriteDocument(*document, paths, true);
    output.flush();
    if (!output) { error = "Cannot write prefab: " + path; return false; }
    return true;
}

//实例化预制体并映射子树身份
Ens* WorldSerializer::InstantiatePrefab(World& world, const std::string& path, EnsId parent, std::string& error)
{
    auto document = ReadDocument(path, error);
    if (!document || document->tokens.front().name != "Prefab")
    { if (error.empty()) error = "Expected Prefab document."; return nullptr; }
    if (!parent.IsNull() && !world.IsAlive(parent))
    { error = "Prefab parent no longer exists."; return nullptr; }
    std::unordered_map<std::string, std::string> paths;
    for (const XmlToken& token : document->tokens)
    {
        const std::string& id = GetAttribute(token, "stableId");
        if (!id.empty() && !paths.contains(id)) paths.emplace(id, "world://ens/" + Object::GenerateUuidText());
    }
    std::string xml = WriteDocument(*document, paths, true);
    return RestoreEns(world, xml, parent, error);
}

//恢复完整子树快照并在失败时清理新增对象
Ens* WorldSerializer::RestoreEns(World& world, const std::string& xml, EnsId parent, std::string& error)
{
    auto remapped = ParseDocument(xml, error);
    if (!remapped || remapped->tokens.front().name != "Prefab")
    { if (error.empty()) error = "Expected Prefab snapshot."; return nullptr; }
    if (!parent.IsNull() && !world.IsAlive(parent))
    { error = "Prefab parent no longer exists."; return nullptr; }
    //检查全部身份冲突
    for (const XmlToken& token : remapped->tokens)
    {
        const std::string& id = GetAttribute(token, "stableId");
        if (!id.empty() && Object::FindObject(StringId(id)))
        { error = "Prefab identity already exists: " + id; return nullptr; }
    }
    //逐个创建对象前记录已有身份，失败时只清理本次新增内容
    List<EnsId> previous;
    world.ForEachEns([&](Ens& ens) { previous.push_back(ens.GetId()); });
    XmlReader reader(xml);
    XmlToken token;
    Ens* root = nullptr;
    bool success = reader.Next(token);
    if (success) success = reader.Next(token) && token.name == "Ens" && token.kind == XmlTokenKind::StartElement;
    if (success)
    {
        success = ReadEns(reader, world, world.GetEns(parent), token);
        root = world.FindEns(StringId(GetAttribute(token, "stableId")));
    }
    if (success) success = reader.Next(token) && token.name == "Prefab" && token.kind == XmlTokenKind::EndElement;
    if (success) success = ApplyEnsReferences(world, *remapped, {});
    if (success && root)
    {
        List<EnsId> added;
        world.ForEachEns([&](Ens& ens) {
            if (std::find(previous.begin(), previous.end(), ens.GetId()) == previous.end()) added.push_back(ens.GetId());
        });
        for (EnsId id : added)
            for (Component* component : world.GetEns(id)->GetComponents())
                if (!LoadResourceRefsFromObject(world, component)) success = false;
    }
    if (success && root) return root;
    List<EnsId> added;
    world.ForEachEns([&](Ens& ens) {
        if (std::find(previous.begin(), previous.end(), ens.GetId()) == previous.end()) added.push_back(ens.GetId());
    });
    for (auto it = added.rbegin(); it != added.rend(); ++it) world.DestroyEns(*it);
    error = "Prefab snapshot restoration failed.";
    return nullptr;
}
