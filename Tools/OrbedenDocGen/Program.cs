using OrbedenDocGen;

//规范化路径盘符大小写，保证不同 shell 下行为一致。
static string CanonicalPath(string path)
{
    string full = Path.GetFullPath(path);
    return full.Length >= 2 && full[1] == ':' ? char.ToUpperInvariant(full[0]) + full[1..] : full;
}

if (args.Length != 4)
{
    Console.Error.WriteLine("Usage: OrbedenDocGen <apiDocsJson> <managedAssembly> <docXml> <outDir>");
    return 1;
}

var projectionPath = CanonicalPath(args[0]);
var assemblyPath = CanonicalPath(args[1]);
var xmlPath = CanonicalPath(args[2]);
var outputDir = CanonicalPath(args[3]);

foreach (var input in new[] { projectionPath, assemblyPath, xmlPath })
{
    if (File.Exists(input)) continue;
    Console.Error.WriteLine($"Input file does not exist: {input}");
    return 1;
}

//投影读不进来是结构问题，单独一档退出码，免得和路径写错混在一起。
DocDocument projection;
try
{
    projection = DocProjection.Load(projectionPath);
}
catch (FileNotFoundException exception) { Console.Error.WriteLine(exception.Message); return 1; }
catch (InvalidDataException exception) { Console.Error.WriteLine(exception.Message); return 2; }

XmlDocumentation documentation;
try
{
    documentation = XmlDocumentation.Load(xmlPath);
}
catch (FileNotFoundException exception) { Console.Error.WriteLine(exception.Message); return 1; }
catch (InvalidDataException exception) { Console.Error.WriteLine(exception.Message); return 1; }

//反射加载托管程序集；生成物里有 [ModuleInitializer]，所以只读元数据，不构造特性实例。
ManagedAssembly managed;
try
{
    managed = ManagedAssembly.Load(assemblyPath);
}
catch (Exception exception) when (exception is BadImageFormatException or FileNotFoundException or FileLoadException or NotSupportedException)
{
    Console.Error.WriteLine($"Managed assembly could not be loaded: {exception.Message}");
    return 3;
}

//逐条核对 xml 条目：数对不上的数量每次运行都报，退回代码改动时能立刻看见。
var coverage = XmlCoverage.Measure(documentation, managed.MembersByDocumentationId);
Console.Error.WriteLine($"xml documentation: {coverage.XmlMatched}/{coverage.XmlEntries} entries matched to reflection members");
foreach (var id in coverage.Unmatched) Console.Error.WriteLine($"  unmatched: {id}");

List<string> warnings = [];
var pages = DocPages.Build(projection, managed, documentation, warnings);
foreach (var warning in warnings) Console.Error.WriteLine($"warning: {warning}");

//基类型链接按托管全名查表，文件名去重与否都不影响指向
Dictionary<string, string> pagesByTypeName = new(StringComparer.Ordinal);
foreach (var page in pages) pagesByTypeName[page.TypeFullName] = DocPages.RelativePath(page);

ManifestCounts counts = new() { Coverage = coverage };
foreach (var page in pages)
foreach (var entry in page.Entries)
{
    ++counts.Members;
    if (entry.Summary.Length == 0) continue;
    ++counts.Documented;
    if (entry.Source == DocSource.Xml) ++counts.XmlDocumented;
    else if (entry.Source == DocSource.Projection) ++counts.ProjectionDocumented;
}

var indexText = MarkdownWriter.RenderIndex(pages, projection, counts);
var outputs = pages.Select(page => (Path: DocPages.RelativePath(page), Text: MarkdownWriter.RenderPage(page, pagesByTypeName))).ToList();

try
{
    Directory.CreateDirectory(outputDir);
    MarkdownWriter writer = new(outputDir);
    foreach (var output in outputs) writer.Write(output.Path, output.Text);
    writer.Write("index.md", indexText);
    foreach (var removed in writer.Prune()) Console.Error.WriteLine($"removed stale page: {removed}");
}
catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or NotSupportedException)
{
    Console.Error.WriteLine($"Could not write documentation: {exception.Message}");
    return 5;
}

Console.WriteLine($"Generated {outputDir} ({pages.Count} types, {counts.Members} members, {counts.Documented} documented)");
return 0;
