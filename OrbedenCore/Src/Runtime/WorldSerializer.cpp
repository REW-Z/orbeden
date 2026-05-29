#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "FileSystem/FileSystem.h"
#include "Log/Log.h"
#include "Runtime/WorldSerializer.h"
#include "Runtime/Reflection.h"
#include "Runtime/SpaceComponent.h"

namespace
{
    //XML Token 类型
    enum class XmlTokenKind
    {
        StartElement,
        EndElement,
    };

    //轻量 XML Token，只保存元素名和属性
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

    //读取属性，不存在时返回空字符串
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

    //轻量 Pull XML 读取器，不构建 DOM 树
    class XmlReader
    {
    private:
        std::string text;
        usize position = 0;

    public:
        //创建 XML 读取器并接管文本内容
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

                //读取属性，直到元素结束
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
        output << "<Component type=\"" << EscapeXml(type->GetName()) << "\">\n";

        //按反射元数据写入持久化字段
        const Reflection::TypeInfo* typeInfo = Reflection::FindTypeInfo(type);
        if (typeInfo)
        {
            for (const Reflection::FieldInfo& field : typeInfo->fields)
            {
                if (!field.persistent || !field.getter) continue;

                WriteIndent(output, depth + 1);
                output << "<Field name=\"" << EscapeXml(field.name ? field.name : "") << "\" type=\""
                    << EscapeXml(field.typeName ? field.typeName : "") << "\" value=\""
                    << EscapeXml(field.getter(component)) << "\" />\n";
            }
        }

        WriteIndent(output, depth);
        output << "</Component>\n";
    }

    //递归写入 Ens 层级
    void WriteEns(std::ostream& output, Ens ens, int depth)
    {
        SpaceComponent* space = ens.Space();
        if (!space) return;

        //写入当前 Ens 和它的组件
        WriteIndent(output, depth);
        output << "<Ens stableId=\"" << EscapeXml(space->GetInstanceId().GetPath()) << "\" name=\""
            << EscapeXml(ens.GetName()) << "\">\n";

        for (TypeId typeId : ens.GetComponentTypes())
        {
            Type* type = Object::FindType(typeId);
            if (!type) continue;

            Component* component = ens.GetComponent(type);
            WriteComponent(output, component, depth + 1);
        }

        //按空间组件链表写入子级 Ens
        EnsId child = space->firstChild;
        while (!child.IsNull())
        {
            Ens childEns = Ens::FromEns(ens.GetWorld(), child);
            SpaceComponent* childSpace = childEns.Space();
            EnsId nextChild = childSpace ? childSpace->next : EnsId();
            WriteEns(output, childEns, depth + 1);
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
        const std::string& value = GetAttribute(token, "value");
        if (name.empty()) return false;

        const Reflection::FieldInfo* field = Reflection::FindField(component->GetType(), name);
        if (!field || !field->persistent || !field->setter)
        {
            return true;
        }

        return field->setter(component, value);
    }

    //读取组件节点并应用字段
    bool ReadComponent(XmlReader& reader, World& world, Ens ens, const XmlToken& startToken)
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
            LogSerializerError("World XML references unknown component type: " + typeName);
            if (!startToken.emptyElement) reader.SkipElement(startToken.name);
            return false;
        }

        //空间组件复用 Ens 自带实例，其余组件按类型挂载
        Component* component = type == SpaceComponent::StaticType() ? ens.Space() : ens.AddComponent(type);
        if (!component)
        {
            LogSerializerError("World XML failed to create component: " + typeName);
            if (!startToken.emptyElement) reader.SkipElement(startToken.name);
            return false;
        }

        if (startToken.emptyElement) return true;

        //读取子 Field 节点，其他未知节点跳过
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
    bool ReadEns(XmlReader& reader, World& world, Ens parent, const XmlToken& startToken)
    {
        //先创建 Ens，再通过嵌套结构恢复父子关系
        const std::string& stableId = GetAttribute(startToken, "stableId");
        const std::string& name = GetAttribute(startToken, "name");
        Ens ens = stableId.empty() ? world.CreateEns(name.empty() ? "Ens" : name) : world.CreateEnsWithStableId(stableId, name.empty() ? "Ens" : name);
        if (!ens.IsValid())
        {
            LogSerializerError("World XML failed to create Ens.");
            if (!startToken.emptyElement) reader.SkipElement(startToken.name);
            return false;
        }

        if (parent.IsValid())
        {
            ens.SetParent(parent);
        }

        if (startToken.emptyElement) return true;

        //读取组件和子 Ens，其他未知节点跳过
        XmlToken token;
        while (reader.Next(token))
        {
            if (token.kind == XmlTokenKind::EndElement && token.name == startToken.name)
            {
                return true;
            }

            if (token.kind == XmlTokenKind::StartElement && token.name == "Component")
            {
                if (!ReadComponent(reader, world, ens, token))
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

        //World 下只关心根 Ens，其余节点保留向前兼容并跳过
        XmlToken token;
        while (reader.Next(token))
        {
            if (token.kind == XmlTokenKind::EndElement && token.name == startToken.name)
            {
                return true;
            }

            if (token.kind == XmlTokenKind::StartElement && token.name == "Ens")
            {
                if (!ReadEns(reader, world, Ens(), token))
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
}

//从 XML 文件反序列化 World
bool WorldSerializer::LoadXml(World& world, const std::string& path)
{
    Reflection::RegisterGeneratedReflection();

    //缺失文件不是致命错误，由 Application 决定是否继续
    if (!FileSystem::Exist(path))
    {
        Log::Warning(("World file does not exist: " + path).c_str());
        return false;
    }

    std::string content = FileSystem::LoadText(path);
    XmlReader reader(std::move(content));

    world.Clear();

    //查找 World 根节点并读取
    XmlToken token;
    while (reader.Next(token))
    {
        if (token.kind == XmlTokenKind::StartElement && token.name == "World")
        {
            bool success = ReadWorld(reader, world, token);
            if (!success)
            {
                world.Clear();
            }

            return success;
        }
    }

    LogSerializerError("World XML does not contain a World root element.");
    world.Clear();
    return false;
}

//将 World 序列化到 XML 文件
bool WorldSerializer::SaveXml(const World& world, const std::string& path)
{
    Reflection::RegisterGeneratedReflection();

    //确保目标目录存在
    std::filesystem::path filePath(path);
    if (filePath.has_parent_path())
    {
        std::filesystem::create_directories(filePath.parent_path());
    }

    std::ofstream output(path, std::ios::out | std::ios::trunc);
    if (!output)
    {
        Log::Error(("Save world XML failed: " + path).c_str());
        return false;
    }

    //写入 World 根节点和所有根 Ens
    output << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    output << "<World version=\"1\">\n";

    world.ForEachEns([&output](Ens ens)
        {
            if (ens.GetParent().IsValid()) return;
            WriteEns(output, ens, 1);
        });

    output << "</World>\n";
    return true;
}
