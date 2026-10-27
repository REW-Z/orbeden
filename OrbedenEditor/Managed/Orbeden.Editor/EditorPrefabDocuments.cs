using System.Xml.Linq;
using Orbeden;

namespace OrbedenEditor;

/// <summary>读写预制体关联和更新场景文档</summary>
internal static class EditorPrefabDocuments
{
    //读取并校验实例关联
    internal static List<EditorPrefabBinding> ReadBindings(XElement root)
    {
        XElement[] blocks = root.Elements("PrefabBindings").ToArray();
        if (blocks.Length == 0) return [];
        if (blocks.Length != 1 || (string?)blocks[0].Attribute("version") != "1")
            throw new InvalidDataException("Unsupported prefab bindings.");
        Dictionary<string, XElement> objects = root.Descendants().Where(e => e.Name == "Ens" || e.Name == "Component")
            .ToDictionary(e => (string?)e.Attribute("stableId") ?? throw new InvalidDataException("Missing object identity."), StringComparer.Ordinal);
        HashSet<string> occupied = new(StringComparer.Ordinal);
        List<EditorPrefabBinding> result = [];
        foreach (XElement entry in blocks[0].Elements("Instance"))
        {
            EditorPrefabBinding binding = new()
            {
                AssetKey = (string?)entry.Attribute("source") ?? string.Empty,
                RootKey = (string?)entry.Attribute("root") ?? string.Empty,
            };
            if (binding.AssetKey.Length == 0 || !objects.TryGetValue(binding.RootKey, out XElement? instance) || instance.Name != "Ens")
                throw new InvalidDataException("Invalid prefab instance root.");
            HashSet<string> subtree = instance.DescendantsAndSelf().Where(e => e.Name == "Ens" || e.Name == "Component")
                .Select(e => (string)e.Attribute("stableId")!).ToHashSet(StringComparer.Ordinal);
            foreach (XElement item in entry.Elements("Object"))
            {
                string source = (string?)item.Attribute("source") ?? string.Empty;
                string target = (string?)item.Attribute("instance") ?? string.Empty;
                if (source.Length == 0 || !subtree.Contains(target) || !occupied.Add(target) || !binding.ObjectKeys.TryAdd(source, target))
                    throw new InvalidDataException("Invalid or overlapping prefab object mapping.");
            }
            if (!subtree.SetEquals(binding.ObjectKeys.Values)) throw new InvalidDataException("Incomplete prefab object mapping.");
            result.Add(binding);
        }
        return result;
    }

    //写入非空实例关联
    internal static void WriteBindings(XElement root, IEnumerable<EditorPrefabBinding> bindings)
    {
        root.Elements("PrefabBindings").Remove();
        XElement block = new("PrefabBindings", new XAttribute("version", "1"));
        foreach (EditorPrefabBinding binding in bindings)
            block.Add(new XElement("Instance", new XAttribute("source", binding.AssetKey), new XAttribute("root", binding.RootKey),
                binding.ObjectKeys.OrderBy(p => p.Key, StringComparer.Ordinal).Select(p =>
                    new XElement("Object", new XAttribute("source", p.Key), new XAttribute("instance", p.Value)))));
        if (block.HasElements) root.Add(block);
    }

    //读取内容根内的单根预制体
    internal static XElement ReadSource(string contentRoot, string assetKey)
    {
        string directory = Path.GetFullPath(contentRoot) + Path.DirectorySeparatorChar;
        string path = Path.GetFullPath(Path.Combine(directory, assetKey));
        if (!path.StartsWith(directory, StringComparison.OrdinalIgnoreCase) || Path.IsPathRooted(assetKey) || PathDefines.IsRawContentPath(assetKey)
            || !Path.GetExtension(path).Equals(".prefab", StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("Expected a Content-relative prefab key.");
        XElement prefab = XDocument.Load(path).Root ?? throw new InvalidDataException("Missing prefab document.");
        if (prefab.Name != "Prefab" || prefab.Elements("Ens").Count() != 1 || prefab.Descendants("PrefabBindings").Any())
            throw new InvalidDataException("Expected a single-root prefab without nested bindings.");
        if (prefab.Descendants("Ens").Any(e => e.Elements("Component").Count(c => (string?)c.Attribute("type") == "Transform") != 1))
            throw new InvalidDataException("Every prefab node must contain one Transform.");
        XElement[] objects = prefab.Descendants().Where(e => e.Name == "Ens" || e.Name == "Component").ToArray();
        HashSet<string> identities = new(StringComparer.Ordinal);
        foreach (XElement item in objects)
        {
            string key = (string?)item.Attribute("stableId") ?? string.Empty;
            if (!key.StartsWith("world://", StringComparison.Ordinal) || !identities.Add(key))
                throw new InvalidDataException("Invalid prefab object identity.");
        }
        return prefab;
    }

    //建立对应子树的对象身份映射
    internal static Dictionary<string, string> MapObjects(XElement source, XElement instance)
    {
        XElement[] originals = source.DescendantsAndSelf().Where(e => e.Name == "Ens" || e.Name == "Component").ToArray();
        XElement[] copies = instance.DescendantsAndSelf().Where(e => e.Name == "Ens" || e.Name == "Component").ToArray();
        if (originals.Length != copies.Length) throw new InvalidDataException("Prefab snapshot structure changed.");
        Dictionary<string, string> map = new(StringComparer.Ordinal);
        for (int i = 0; i < originals.Length; ++i)
        {
            if (originals[i].Name != copies[i].Name || (string?)originals[i].Attribute("type") != (string?)copies[i].Attribute("type"))
                throw new InvalidDataException("Prefab snapshot types changed.");
            map.Add((string)originals[i].Attribute("stableId")!, (string)copies[i].Attribute("stableId")!);
        }
        return map;
    }

    //更新实例内容并保留根节点属性与局部变换
    internal static bool Synchronize(XDocument world, string contentRoot, string? assetKey)
    {
        XElement root = world.Root ?? throw new InvalidDataException("Missing world document.");
        List<EditorPrefabBinding> bindings = ReadBindings(root);
        bool changed = false;
        Dictionary<string, string> removed = new(StringComparer.Ordinal);
        foreach (EditorPrefabBinding binding in bindings)
        {
            if (assetKey != null && binding.AssetKey != assetKey) continue;
            XElement source = ReadSource(contentRoot, binding.AssetKey);
            XElement instance = root.Descendants("Ens").Single(e => (string?)e.Attribute("stableId") == binding.RootKey);
            string sourceRoot = (string)source.Element("Ens")!.Attribute("stableId")!;
            if (!binding.ObjectKeys.TryGetValue(sourceRoot, out string? mappedRoot) || mappedRoot != binding.RootKey)
                throw new InvalidDataException("Prefab root identity changed: " + binding.AssetKey);

            //保留已有身份并分配新增身份
            HashSet<string> sourceKeys = source.Descendants().Where(e => e.Name == "Ens" || e.Name == "Component")
                .Select(e => (string)e.Attribute("stableId")!).ToHashSet(StringComparer.Ordinal);
            Dictionary<string, string> map = new(StringComparer.Ordinal);
            foreach (string key in sourceKeys)
                map.Add(key, binding.ObjectKeys.TryGetValue(key, out string? previous) ? previous : "world://ens/" + Guid.NewGuid().ToString("D"));
            foreach (var item in binding.ObjectKeys)
                if (!sourceKeys.Contains(item.Key)) removed[item.Value] = string.Empty;
            XElement replacement = XDocument.Parse(EditorAssetsNative.RemapObjectIdentities(source.ToString(), map, true)).Root!.Element("Ens")!;

            //保留实例根节点属性
            replacement.SetAttributeValue("name", (string?)instance.Attribute("name") ?? string.Empty);
            replacement.SetAttributeValue("localActive", (string?)instance.Attribute("localActive") ?? "true");
            replacement.SetAttributeValue("static", (string?)instance.Attribute("static") ?? "false");

            //保留实例根节点局部变换
            XElement? oldTransform = instance.Elements("Component").FirstOrDefault(e => (string?)e.Attribute("type") == "Transform");
            XElement? newTransform = replacement.Elements("Component").FirstOrDefault(e => (string?)e.Attribute("type") == "Transform");
            foreach (string name in new[] { "localPosition", "localRotation", "localScale" })
            {
                XElement? previous = oldTransform?.Elements("Field").FirstOrDefault(e => (string?)e.Attribute("name") == name);
                XElement? field = newTransform?.Elements("Field").FirstOrDefault(e => (string?)e.Attribute("name") == name);
                if (previous == null) continue;
                if (field != null) field.SetAttributeValue("value", (string?)previous.Attribute("value"));
                else newTransform?.Add(new XElement(previous));
            }
            if (!XNode.DeepEquals(instance, replacement)) changed = true;
            instance.ReplaceWith(replacement);
            binding.ObjectKeys = map;
        }
        WriteBindings(root, bindings);
        if (removed.Count != 0)
        {
            XDocument remapped = XDocument.Parse(EditorAssetsNative.RemapObjectIdentities(world.ToString(), removed, false));
            world.Root!.ReplaceWith(remapped.Root!);
            changed = true;
        }
        return changed;
    }

    //安全替换文档文件
    internal static void WriteFile(string path, XDocument document)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        string pending = path + "." + Guid.NewGuid().ToString("N") + ".pending";
        try
        {
            File.WriteAllText(pending, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n" + document.Root!.ToString() + "\n", InteropText.Utf8);
            File.Move(pending, path, true);
        }
        finally { if (File.Exists(pending)) File.Delete(pending); }
    }
}
