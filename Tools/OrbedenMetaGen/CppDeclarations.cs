using System.Text;
using System.Text.Json.Serialization;

namespace OrbedenMetaGen;

internal sealed record CppToken(string Text, int Line);
internal sealed record CppParameter(string Name, string Type, string DefaultValue);
internal sealed class CppMember
{
    public string Name { get; set; } = "";
    public string Type { get; set; } = "";
    public string Access { get; set; } = "private";
    //行号只服务于本地报错，写进清单就会带上编译机的路径，因此不参与序列化
    [JsonIgnore]
    public int Line { get; set; }
    //声明上方紧贴的行注释，只服务于用户文档生成；清单是跨模块导入契约，不掺文档文本
    [JsonIgnore]
    public string Doc { get; set; } = "";
    public bool IsMethod { get; set; }
    public bool IsStatic { get; set; }
    public bool IsConst { get; set; }
    public bool IsVirtual { get; set; }
    public bool IsTemplate { get; set; }
    public bool Serialize { get; set; }
    public bool IgnoreBinding { get; set; }
    public string Changed { get; set; } = "";
    public string Getter { get; set; } = "";
    public string Setter { get; set; } = "";
    public string Buffer { get; set; } = "";
    public string Count { get; set; } = "";
    public bool FixedArray { get; set; }
    public List<CppParameter> Parameters { get; set; } = [];
}
internal sealed class CppType
{
    public string Name { get; set; } = "";
    public string QualifiedName { get; set; } = "";
    public string BaseName { get; set; } = "";
    //清单要跨机器共用，来源文件是扫描时的绝对路径，只服务于本地报错，不参与序列化
    [JsonIgnore]
    public string File { get; set; } = "";
    [JsonIgnore]
    public int Line { get; set; }
    //类型上方的行注释，只服务于用户文档生成，不参与序列化
    [JsonIgnore]
    public string Doc { get; set; } = "";
    public string Kind { get; set; } = "class";
    public string EnumBase { get; set; } = "int32";
    public bool IsObject { get; set; }
    public bool IsAbstract { get; set; }
    public bool IsUnique { get; set; }
    public bool IsFinal { get; set; }
    public List<CppMember> Members { get; set; } = [];
    public List<KeyValuePair<string, string>> EnumValues { get; set; } = [];
}

internal static class CppDeclarations
{
    /// <summary>扫描声明所需的 C++ token，保留诊断行号并跳过注释和预处理指令。</summary>
    internal static List<CppToken> Tokenize(string source)
    {
        List<CppToken> result = [];
        int line = 1;
        for (int index = 0; index < source.Length;)
        {
            char value = source[index];
            if (char.IsWhiteSpace(value)) { if (value == '\n') ++line; ++index; continue; }
            if (value == '#')
            {
                do
                {
                    int end = source.IndexOf('\n', index);
                    if (end < 0) { index = source.Length; break; }
                    bool continued = source[index..end].TrimEnd().EndsWith('\\');
                    index = end + 1; ++line;
                    if (!continued) break;
                } while (index < source.Length);
                continue;
            }
            if (value == '/' && index + 1 < source.Length && source[index + 1] == '/')
            {
                while (index < source.Length && source[index] != '\n') ++index;
                continue;
            }
            if (value == '/' && index + 1 < source.Length && source[index + 1] == '*')
            {
                index += 2;
                while (index + 1 < source.Length && !(source[index] == '*' && source[index + 1] == '/'))
                    if (source[index++] == '\n') ++line;
                index = Math.Min(source.Length, index + 2);
                continue;
            }
            int start = index++;
            int tokenLine = line;
            if (value is '\"' or '\'')
            {
                while (index < source.Length)
                {
                    char next = source[index++];
                    if (next == '\n') ++line;
                    if (next == '\\' && index < source.Length) { ++index; continue; }
                    if (next == value) break;
                }
            }
            else if (char.IsLetterOrDigit(value) || value == '_')
            {
                while (index < source.Length && (char.IsLetterOrDigit(source[index]) || source[index] == '_')) ++index;
            }
            else if (index < source.Length && (source[start..(index + 1)] is "::" or "&&" or "->" or "==" or "!=" or "<=" or ">=" or "++" or "--")) ++index;
            result.Add(new(source[start..index], tokenLine));
        }
        return result;
    }

    /// <summary>读取命名空间、类型、字段和方法，共享给反射与 Binding 输出。</summary>
    internal static List<CppType> Parse(string source, string file)
    {
        List<CppType> result = [];
        List<CppToken> tokens = Tokenize(source);
        ParseScope(tokens, 0, tokens.Count, "", file, source.Split('\n'), result);
        return result;
    }

    private static void ParseScope(List<CppToken> tokens, int start, int end, string scope, string file, string[] lines, List<CppType> result)
    {
        for (int index = start; index < end; ++index)
        {
            string keyword = tokens[index].Text;
            if (keyword == "namespace")
            {
                int open = index + 1;
                while (open < end && tokens[open].Text is not ("{" or ";" or "=")) ++open;
                if (open >= end || tokens[open].Text != "{") continue;
                int close = Match(tokens, open, "{", "}");
                string name = Join(tokens.GetRange(index + 1, open - index - 1));
                ParseScope(tokens, open + 1, close, Qualify(scope, name), file, lines, result);
                index = close; continue;
            }
            if (keyword is not ("class" or "struct" or "enum"))
            {
                if (keyword == "{") index = Match(tokens, index, "{", "}");
                continue;
            }
            int nameIndex = index + 1;
            if (keyword == "enum" && nameIndex < end && tokens[nameIndex].Text is "class" or "struct") ++nameIndex;
            if (nameIndex >= end || !IsIdentifier(tokens[nameIndex].Text)) continue;
            int brace = nameIndex + 1;
            while (brace < end && tokens[brace].Text is not ("{" or ";" or "(")) ++brace;
            if (brace >= end || tokens[brace].Text != "{") continue;
            int closing = Match(tokens, brace, "{", "}");
            CppType type = new()
            {
                Name = tokens[nameIndex].Text, QualifiedName = Qualify(scope, tokens[nameIndex].Text),
                File = file, Line = tokens[index].Line, Kind = keyword,
                Doc = DocAbove(lines, tokens[index].Line),
                IsFinal = tokens.GetRange(nameIndex + 1, brace - nameIndex - 1).Any(value => value.Text == "final")
            };
            int colon = tokens.FindIndex(nameIndex + 1, brace - nameIndex - 1, value => value.Text == ":");
            if (colon >= 0)
            {
                string baseName = Join(tokens.GetRange(colon + 1, brace - colon - 1).Where(value => value.Text is not ("public" or "private" or "protected" or "virtual")).ToList());
                if (keyword == "enum") type.EnumBase = baseName;
                else type.BaseName = baseName;
            }
            if (keyword == "enum")
            {
                foreach (List<CppToken> item in Split(tokens.GetRange(brace + 1, closing - brace - 1), ",", false))
                {
                    if (item.Count == 0) continue;
                    int equals = item.FindIndex(value => value.Text == "=");
                    type.EnumValues.Add(new(item[0].Text, equals < 0 ? "" : Join(item[(equals + 1)..])));
                }
            }
            else ParseMembers(tokens, brace + 1, closing, type, lines, result);
            result.Add(type);
            index = closing;
        }
    }

    private static void ParseMembers(List<CppToken> tokens, int start, int end, CppType type, string[] lines, List<CppType> result)
    {
        string access = type.Kind == "struct" ? "public" : "private";
        bool ignored = false, serialize = false, template = false;
        string getter = "", setter = "", buffer = "", count = "", changed = "";
        for (int index = start; index < end;)
        {
            string token = tokens[index].Text;
            if (token is "public" or "private" or "protected" && index + 1 < end && tokens[index + 1].Text == ":")
            { access = token; index += 2; continue; }
            if (token.StartsWith("OBJECT_TYPE_DECLARE", StringComparison.Ordinal))
            {
                type.IsObject = true; type.IsAbstract = token.EndsWith("ABSTRACT", StringComparison.Ordinal);
                index = Match(tokens, index + 1, "(", ")") + 1; access = "public"; continue;
            }
            if (token == "ORBEDEN_COMPONENT_UNIQUE") { type.IsUnique = true; ++index; continue; }
            if (token == "ORBEDEN_BIND_CHANGED")
            {
                int close = Match(tokens, index + 1, "(", ")");
                changed = Join(tokens.GetRange(index + 2, close - index - 2));
                if (!IsIdentifier(changed)) throw new InvalidDataException($"{type.File}:{tokens[index].Line}: invalid change callback");
                index = close + 1; continue;
            }
            if (token is "ORBEDEN_BIND_IGNORE" or "ORBEDEN_SERIALIZE_FIELD")
            { ignored |= token == "ORBEDEN_BIND_IGNORE"; serialize |= token == "ORBEDEN_SERIALIZE_FIELD"; ++index; continue; }
            if (token is "ORBEDEN_BIND_ACCESSORS" or "ORBEDEN_BIND_BUFFER")
            {
                int close = Match(tokens, index + 1, "(", ")");
                var values = Split(tokens.GetRange(index + 2, close - index - 2), ",").Select(Join).ToArray();
                if (values.Length != 2) throw new InvalidDataException($"{type.File}:{tokens[index].Line}: {token} requires two arguments");
                if (token == "ORBEDEN_BIND_ACCESSORS") { getter = values[0]; setter = values[1]; }
                else { buffer = values[0]; count = values[1]; }
                index = close + 1; continue;
            }
            if (token == "template")
            { template = true; index = Match(tokens, index + 1, "<", ">") + 1; continue; }
            int statementStart = index;
            int parentheses = 0, angles = 0;
            bool body = false;
            for (; index < end; ++index)
            {
                token = tokens[index].Text;
                if (token == "(") ++parentheses;
                if (token == ")") --parentheses;
                if (token == "<") ++angles;
                if (token == ">") angles = Math.Max(0, angles - 1);
                if (token == "{" && parentheses == 0)
                {
                    bool method = tokens.GetRange(statementStart, index - statementStart).Any(value => value.Text == ")");
                    bool nested = tokens[statementStart].Text is "class" or "struct" or "enum";
                    int close = Match(tokens, index, "{", "}");
                    if (nested) ParseScope(tokens, statementStart, close + 1, type.QualifiedName, type.File, lines, result);
                    if (method || nested) { body = true; break; }
                    index = close;
                }
                if (token == ";" && parentheses == 0 && angles == 0) break;
            }
            List<CppToken> declaration = tokens.GetRange(statementStart, index - statementStart);
            if (declaration.Count != 0 && declaration[0].Text is not ("friend" or "using" or "typedef" or "class" or "struct" or "enum"))
            {
                CppMember? member = ParseMember(declaration, type);
                if (member != null)
                {
                    member.Access = access; member.IgnoreBinding = ignored; member.Serialize = serialize;
                    member.Changed = changed; member.Getter = getter; member.Setter = setter; member.Buffer = buffer; member.Count = count; member.IsTemplate = template;
                    member.Doc = DocAbove(lines, member.Line);
                    type.Members.Add(member);
                }
            }
            if (body) index = Match(tokens, index, "{", "}");
            ++index;
            ignored = serialize = template = false; getter = setter = buffer = count = changed = "";
        }
    }

    private static CppMember? ParseMember(List<CppToken> declaration, CppType owner)
    {
        if (declaration.Count == 0) return null;
        int open = declaration.FindIndex(value => value.Text == "(");
        int equals = declaration.FindIndex(value => value.Text == "=");
        bool method = open >= 0 && (equals < 0 || open < equals);
        if (declaration.Any(value => value.Text == "operator")) return null;
        CppMember member = new() { IsMethod = method, Line = declaration[0].Line };
        if (method)
        {
            if (open == 0) return null;
            member.Name = declaration[open - 1].Text;
            if (member.Name == owner.Name) return null;
            member.Type = Join(declaration.Take(open - 1).Where(value => value.Text is not ("static" or "virtual" or "inline" or "constexpr" or "explicit")).ToList());
            int close = Match(declaration, open, "(", ")");
            member.IsConst = declaration.Skip(close + 1).Any(value => value.Text == "const");
            member.IsVirtual = declaration.Any(value => value.Text is "virtual" or "override");
            foreach (List<CppToken> parameter in Split(declaration.GetRange(open + 1, close - open - 1), ","))
            {
                if (parameter.Count == 0 || parameter.Count == 1 && parameter[0].Text == "void") continue;
                int defaultAt = parameter.FindIndex(value => value.Text == "=");
                var signature = defaultAt < 0 ? parameter : parameter[..defaultAt];
                int nameAt = signature.Count - 1;
                bool hasName = IsIdentifier(signature[nameAt].Text) && signature.Count > 1 && signature[nameAt - 1].Text != "::" && !(signature.Count == 2 && signature[0].Text == "const");
                string name = hasName ? signature[nameAt].Text : "arg" + member.Parameters.Count;
                string parameterType = Join(hasName ? signature[..nameAt] : signature);
                member.Parameters.Add(new(name, parameterType, defaultAt < 0 ? "" : Join(parameter[(defaultAt + 1)..])));
            }
        }
        else
        {
            int initializer = declaration.FindIndex(value => value.Text is "=" or "{");
            var signature = initializer < 0 ? declaration : declaration[..initializer];
            int subscript = signature.FindIndex(value => value.Text == "[");
            member.FixedArray = subscript >= 0;
            string arrayLength = string.Empty;
            if (member.FixedArray)
            {
                if (subscript <= 0) return null;
                int close = signature.FindIndex(subscript + 1, token => token.Text == "]");
                if (close < 0 || close != signature.Count - 1) throw new InvalidDataException("Only one-dimensional fixed arrays are supported.");
                arrayLength = Join(signature.GetRange(subscript + 1, close - subscript - 1));
                signature = signature[..subscript];
            }
            if (signature.Count < 2) return null;
            member.Name = signature[^1].Text;
            member.Type = Join(signature.Take(signature.Count - 1).Where(value => value.Text is not ("static" or "mutable" or "constexpr" or "inline")).ToList());
            if (member.FixedArray) member.Type = $"std::array<{member.Type},{arrayLength}>";
        }
        member.IsStatic = declaration.Any(value => value.Text == "static");
        return member;
    }

    internal static string Join(List<CppToken> tokens)
    {
        StringBuilder result = new();
        string previous = "";
        foreach (var token in tokens)
        {
            if (result.Length != 0 && IsWord(previous) && IsWord(token.Text)) result.Append(' ');
            result.Append(token.Text); previous = token.Text;
        }
        return result.ToString();
    }
    internal static List<List<CppToken>> Split(List<CppToken> tokens, string separator, bool templates = true)
    {
        List<List<CppToken>> result = []; List<CppToken> current = [];
        int parentheses = 0, angles = 0, braces = 0;
        foreach (var token in tokens)
        {
            if (token.Text == separator && parentheses == 0 && angles == 0 && braces == 0) { result.Add(current); current = []; continue; }
            current.Add(token);
            if (token.Text == "(") ++parentheses; if (token.Text == ")") --parentheses;
            if (templates && token.Text == "<") ++angles; if (templates && token.Text == ">") angles = Math.Max(0, angles - 1);
            if (token.Text == "{") ++braces; if (token.Text == "}") --braces;
        }
        result.Add(current); return result;
    }
    private static int Match(List<CppToken> tokens, int start, string open, string close)
    {
        if (start >= tokens.Count || tokens[start].Text != open) throw new InvalidDataException($"Expected {open} near line {(start < tokens.Count ? tokens[start].Line : 0)}");
        int depth = 0;
        for (int index = start; index < tokens.Count; ++index)
        { if (tokens[index].Text == open) ++depth; if (tokens[index].Text == close && --depth == 0) return index; }
        throw new InvalidDataException($"Unclosed {open} at line {tokens[start].Line}");
    }
    private static string Qualify(string scope, string name) => scope.Length == 0 ? name : name.Length == 0 ? scope : scope + "::" + name;
    private static bool IsIdentifier(string value) => value.Length != 0 && (char.IsLetter(value[0]) || value[0] == '_');
    private static bool IsWord(string value) => value.Length != 0 && (char.IsLetterOrDigit(value[0]) || value[0] == '_');

    //声明上方紧贴的行注释；跨空行、跨预处理指令、跨上一条声明都不算——那类注释是分节标题，不是文档。
    //注解宏与 template 行不携带文档，向上找时越过它们，否则带 ORBEDEN_ 注解的成员会整批漏掉说明。
    private static string DocAbove(string[] lines, int declarationLine)
    {
        int index = declarationLine - 2;
        while (index >= 0 && IsAnnotationLine(lines[index])) --index;
        List<string> collected = [];
        while (index >= 0)
        {
            string text = lines[index].Trim();
            if (!text.StartsWith("//", StringComparison.Ordinal)) break;
            collected.Insert(0, text);
            --index;
        }
        return NormalizeComment(collected);
    }

    //去掉行注释标记与 <summary> 包裹，压掉首尾空行后按行合并。
    private static string NormalizeComment(List<string> lines)
    {
        List<string> text = [];
        foreach (string line in lines)
        {
            string value = line.StartsWith("///", StringComparison.Ordinal) || line.StartsWith("//!", StringComparison.Ordinal)
                ? line[3..] : line[2..];
            value = value.Trim();
            if (value.StartsWith("<summary>", StringComparison.Ordinal)) value = value["<summary>".Length..];
            if (value.EndsWith("</summary>", StringComparison.Ordinal)) value = value[..^"</summary>".Length];
            text.Add(value.Trim());
        }
        while (text.Count != 0 && text[0].Length == 0) text.RemoveAt(0);
        while (text.Count != 0 && text[^1].Length == 0) text.RemoveAt(text.Count - 1);
        return string.Join("\n", text);
    }

    //注解宏与 template 声明行：它们不构成声明，注释属于紧随其后的那个成员或类型。
    private static bool IsAnnotationLine(string line)
    {
        string text = line.Trim();
        return text.StartsWith("ORBEDEN_", StringComparison.Ordinal)
            || text.StartsWith("template<", StringComparison.Ordinal)
            || text.StartsWith("template <", StringComparison.Ordinal);
    }
}
