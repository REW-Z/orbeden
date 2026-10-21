using System.Globalization;
using System.Numerics;
using System.Xml.Linq;
using Orbeden;

namespace OrbedenEditor;

/// <summary>将导入器中间节点转换为可重建的 Ens 预制体文档</summary>
internal static class EditorModelImport
{
    internal sealed record Node(string Name, int ParentIndex, string LocalMatrix, string MeshKey, List<string> MaterialKeys);
    private const string DefaultMaterialKey = "Builtin/Materials/default.orbmat";

    //识别生成预制体的模型源
    internal static bool IsModel(string path) => Path.GetExtension(path).ToLowerInvariant() is ".obj" or ".gltf" or ".glb";

    //生成单根 Ens 子树并校验节点资源
    internal static string CreatePrefab(string sourceKey, IReadOnlyList<Node> nodes, IReadOnlyList<EditorAssetInspection.Asset> objects)
    {
        if (nodes.Count == 0) throw new InvalidDataException("Model hierarchy is unavailable.");
        HashSet<string> meshes = objects.Where(asset => asset.TypeName == nameof(Mesh)).Select(asset => asset.Key).ToHashSet(StringComparer.Ordinal);
        HashSet<string> materials = objects.Where(asset => asset.TypeName == nameof(Material)).Select(asset => asset.Key).ToHashSet(StringComparer.Ordinal);
        XElement root = CreateEns("root", Path.GetFileNameWithoutExtension(sourceKey), Vector3.Zero, Quaternion.Identity, Vector3.One);
        List<XElement> instances = [];
        for (int index = 0; index < nodes.Count; ++index)
        {
            Node node = nodes[index];
            if (node.ParentIndex < -1 || node.ParentIndex >= index || node.MeshKey.Length == 0 && node.MaterialKeys.Count != 0
                || node.MeshKey.Length != 0 && !meshes.Contains(node.MeshKey)
                || node.MaterialKeys.Any(key => key.Length != 0 && !materials.Contains(key)))
                throw new InvalidDataException("Invalid model node or resource reference: " + node.Name);

            //转换节点变换
            string[] fields = node.LocalMatrix.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            if (fields.Length != 16) throw new InvalidDataException("Model node matrix must contain 16 numbers.");
            float[] values = new float[16];
            for (int entry = 0; entry < values.Length; ++entry)
                if (!float.TryParse(fields[entry], NumberStyles.Float, CultureInfo.InvariantCulture, out values[entry]) || !float.IsFinite(values[entry]))
                    throw new InvalidDataException("Model node matrix contains an invalid number.");
            Matrix4x4 matrix = new(values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7],
                values[8], values[9], values[10], values[11], values[12], values[13], values[14], values[15]);
            if (!Matrix4x4.Decompose(matrix, out Vector3 scale, out Quaternion rotation, out Vector3 position)
                || !float.IsFinite(scale.X) || !float.IsFinite(scale.Y) || !float.IsFinite(scale.Z)
                || !float.IsFinite(rotation.X) || !float.IsFinite(rotation.Y) || !float.IsFinite(rotation.Z) || !float.IsFinite(rotation.W))
                throw new InvalidDataException("Model node matrix cannot be represented by a Transform: " + node.Name);
            string identity = "node/" + index.ToString(CultureInfo.InvariantCulture);
            XElement instance = CreateEns(identity, node.Name, position, rotation, scale);
            (node.ParentIndex < 0 ? root : instances[node.ParentIndex]).Add(instance);
            instances.Add(instance);
            if (node.MeshKey.Length == 0) continue;

            //写入共享资源引用和材质槽
            string[] slots = node.MaterialKeys.Select(key => key.Length == 0 ? DefaultMaterialKey : key).ToArray();
            string encoded = slots.Length.ToString(CultureInfo.InvariantCulture) + ":"
                + string.Concat(slots.Select(key => InteropText.GetUtf8ByteCount(key).ToString(CultureInfo.InvariantCulture) + ":" + key));
            instance.Add(new XElement("Component", new XAttribute("type", nameof(StaticMeshRenderer)),
                new XAttribute("stableId", "world://ens/imported/" + identity + "/StaticMeshRenderer"),
                CreateField("mesh", "Ref<Mesh>", node.MeshKey), CreateField("materials", "List<Ref<Material>>", encoded)));
        }
        return new XElement("Prefab", new XAttribute("version", "1"), root).ToString().Replace("\r\n", "\n") + "\n";
    }

    //创建带确定身份和本地变换的 Ens 文档
    private static XElement CreateEns(string identity, string name, Vector3 position, Quaternion rotation, Vector3 scale)
    {
        string key = "world://ens/imported/" + identity;
        return new XElement("Ens", new XAttribute("stableId", key), new XAttribute("name", name),
            new XAttribute("localActive", "true"), new XAttribute("static", "false"),
            new XElement("Component", new XAttribute("type", "Transform"), new XAttribute("stableId", key + "/Transform"),
                CreateField("localPosition", "vector3", FormattableString.Invariant($"{position.X:R} {position.Y:R} {position.Z:R}")),
                CreateField("localRotation", "quaternion", FormattableString.Invariant($"{rotation.X:R} {rotation.Y:R} {rotation.Z:R} {rotation.W:R}")),
                CreateField("localScale", "vector3", FormattableString.Invariant($"{scale.X:R} {scale.Y:R} {scale.Z:R}"))));
    }

    //创建标准组件字段
    private static XElement CreateField(string name, string type, string value) => new("Field",
        new XAttribute("name", name), new XAttribute("type", type), new XAttribute("value", value));

    //按 UTF-8 字节长度读取材质数组
    internal static IReadOnlyList<string> ReadMaterialKeys(string value)
    {
        byte[] bytes = InteropText.EncodeUtf8(value);
        int position = 0;
        int ReadLength()
        {
            int end = Array.IndexOf(bytes, (byte)':', position);
            if (end <= position || !int.TryParse(InteropText.DecodeUtf8(bytes.AsSpan(position, end - position)),
                NumberStyles.None, CultureInfo.InvariantCulture, out int length))
                throw new InvalidDataException("Invalid prefab material array.");
            position = end + 1;
            return length;
        }
        int count = ReadLength();
        if (count > (bytes.Length - position) / 2) throw new InvalidDataException("Invalid prefab material count.");
        List<string> keys = [];
        for (int index = 0; index < count; ++index)
        {
            int length = ReadLength();
            if (length > bytes.Length - position) throw new InvalidDataException("Truncated prefab material array.");
            keys.Add(InteropText.DecodeUtf8(bytes.AsSpan(position, length)));
            position += length;
        }
        if (position != bytes.Length) throw new InvalidDataException("Trailing prefab material data.");
        return keys;
    }
}
