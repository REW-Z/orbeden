using System.Reflection;
using System.Text;
using System.Xml;
using System.Xml.Linq;

namespace OrbedenDocGen;

/// <summary>编译器产出的 xml 注释索引：文档 ID 到摘要文本。</summary>
/// <remarks>
/// 只认 &lt;summary&gt;，并且把同一个成员上的多段摘要合起来——partial 类型每个文件都能写一段，
/// Orbeden.GUI 就是三段。合并时中文之间不补空格，英文单词之间补一个，免得排版出双空格。
/// </remarks>
internal sealed class XmlDocumentation
{
    private readonly Dictionary<string, string> summaries;

    private XmlDocumentation(Dictionary<string, string> summaries) => this.summaries = summaries;

    /// <summary>读取 xml 注释文件；文件缺失抛 <see cref="FileNotFoundException"/>，格式损坏抛 <see cref="InvalidDataException"/>。</summary>
    internal static XmlDocumentation Load(string path)
    {
        if (!File.Exists(path)) throw new FileNotFoundException($"Documentation XML not found: {path}", path);
        XDocument document;
        try
        {
            document = XDocument.Load(path);
        }
        catch (XmlException exception)
        {
            throw new InvalidDataException($"Documentation XML is malformed: {exception.Message}", exception);
        }

        Dictionary<string, string> summaries = new(StringComparer.Ordinal);
        foreach (var member in document.Descendants("member"))
        {
            var id = member.Attribute("name")?.Value;
            if (string.IsNullOrEmpty(id)) continue;
            var text = MergeSummaries(member);
            if (text.Length != 0) summaries[id] = text;
        }
        return new XmlDocumentation(summaries);
    }

    /// <summary>按文档 ID 取摘要，没有就返回空串。</summary>
    internal string Get(string id) => summaries.TryGetValue(id, out var value) ? value : "";

    /// <summary>成员在 xml 里的条目数。</summary>
    internal int Count => summaries.Count;

    /// <summary>全部文档 ID，按序数排序，便于稳定输出。</summary>
    internal IEnumerable<string> Ids => summaries.Keys.OrderBy(id => id, StringComparer.Ordinal);

    //同一个成员上的多段 summary 依次拼起来
    private static string MergeSummaries(XElement member)
    {
        StringBuilder result = new();
        foreach (var summary in member.Elements("summary"))
        {
            var text = Normalize(summary.Value);
            if (text.Length == 0) continue;
            if (result.Length != 0 && NeedsSpace(result[^1], text[0])) result.Append(' ');
            result.Append(text);
        }
        return result.ToString();
    }

    //逐行去空白后重排：中文行之间直接接上，两边都是 ASCII 字母数字时补空格
    private static string Normalize(string text)
    {
        StringBuilder result = new();
        foreach (var line in text.Split('\n'))
        {
            var trimmed = line.Trim();
            if (trimmed.Length == 0) continue;
            if (result.Length != 0 && NeedsSpace(result[^1], trimmed[0])) result.Append(' ');
            result.Append(trimmed);
        }
        return result.ToString();
    }

    private static bool NeedsSpace(char left, char right) => char.IsAsciiLetterOrDigit(left) && char.IsAsciiLetterOrDigit(right);
}

/// <summary>xml 条目与反射成员的对照结果，用来在每次运行时报告对不上的条目。</summary>
internal sealed class XmlCoverage
{
    public List<string> Unmatched { get; } = [];
    public int XmlEntries { get; set; }
    public int XmlMatched { get; set; }

    /// <summary>逐条遍历 xml 自己的 name 条目去找反射成员，能同时验出 ID 拼错和被过滤掉的成员。</summary>
    internal static XmlCoverage Measure(XmlDocumentation documentation, IReadOnlyDictionary<string, MemberInfo> membersById)
    {
        XmlCoverage result = new();
        foreach (var id in documentation.Ids)
        {
            ++result.XmlEntries;
            if (membersById.ContainsKey(id)) ++result.XmlMatched;
            else result.Unmatched.Add(id);
        }
        return result;
    }
}
