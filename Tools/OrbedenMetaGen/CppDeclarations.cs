using System.Text;

namespace OrbedenMetaGen;

internal sealed record CppToken(string Text, int Line);
internal sealed record CppParameter(string Name, string Type, string DefaultValue);
internal sealed class CppMember
{
    public string Name { get; set; } = "";
    public string Type { get; set; } = "";
    public string Access { get; set; } = "private";
    public int Line { get; set; }
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
    public string File { get; set; } = "";
    public int Line { get; set; }
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
        ParseScope(tokens, 0, tokens.Count, "", file, result);
        return result;
    }

    private static void ParseScope(List<CppToken> tokens, int start, int end, string scope, string file, List<CppType> result)
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
                ParseScope(tokens, open + 1, close, Qualify(scope, name), file, result);
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
            else ParseMembers(tokens, brace + 1, closing, type, result);
            result.Add(type);
            index = closing;
        }
    }

    private static void ParseMembers(List<CppToken> tokens, int start, int end, CppType type, List<CppType> result)
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
                    if (nested) ParseScope(tokens, statementStart, close + 1, type.QualifiedName, type.File, result);
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
            if (member.FixedArray)
            {
                if (subscript <= 0) return null;
                signature = signature[..subscript];
            }
            if (signature.Count < 2) return null;
            member.Name = signature[^1].Text;
            member.Type = Join(signature.Take(signature.Count - 1).Where(value => value.Text is not ("static" or "mutable" or "constexpr" or "inline")).ToList());
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
}
