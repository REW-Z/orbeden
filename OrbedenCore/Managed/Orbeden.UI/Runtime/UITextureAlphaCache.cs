using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 遮罩纹理的 CPU Alpha 缓存。按纹理身份加内容版本缓存像素，采样规则与 UI shader 一致：
/// 双线性、边界外取零、UV 的 v 轴朝上（采样时按 t = 1 - v 取内存行）。
/// 通道语义与原生覆盖率池一致：R8 读 R，RGBA 读 A，RGB 视为全 1。
/// </summary>
public sealed class UITextureAlphaCache
{
    private sealed class Entry
    {
        internal byte[] pixels = [];
        internal int width;
        internal int height;
        internal int channels;
        internal ulong revision;
    }

    private readonly Dictionary<int, Entry> entries = [];

    /// <summary>采样一处 Alpha；无纹理、无 CPU 像素或参数非法时为零。</summary>
    public float Sample(Texture2D? texture, float u, float v)
    {
        Entry? entry = Resolve(texture);
        if (entry == null) return 0.0f;
        if (!float.IsFinite(u) || !float.IsFinite(v)) return 0.0f;
        //RGB 没有 Alpha 通道，整块按不透明处理。
        if (entry.channels == 3) return 1.0f;
        if (entry.width <= 0 || entry.height <= 0) return 0.0f;

        //UV 的 v 轴朝上：位图行 0 在图像顶部，所以按 1 - v 取行。
        float x = u * entry.width - 0.5f;
        float y = (1.0f - v) * entry.height - 0.5f;
        int x0 = (int)MathF.Floor(x);
        int y0 = (int)MathF.Floor(y);
        float fx = x - x0;
        float fy = y - y0;

        float top = Sample(entry, x0, y0) * (1.0f - fx) + Sample(entry, x0 + 1, y0) * fx;
        float bottom = Sample(entry, x0, y0 + 1) * (1.0f - fx) + Sample(entry, x0 + 1, y0 + 1) * fx;
        return top * (1.0f - fy) + bottom * fy;
    }

    /// <summary>丢弃一张纹理的缓存；纹理重新导入或内容变化后由持有者调用。</summary>
    public void Invalidate(Texture2D? texture)
    {
        if (texture == null) return;
        entries.Remove(texture.InstanceId);
    }

    /// <summary>丢弃全部缓存。</summary>
    public void Clear() => entries.Clear();

    //按对象身份与内容版本取缓存；版本变化时重新读取像素。
    private Entry? Resolve(Texture2D? texture)
    {
        if (texture == null || !texture.IsAlive) return null;
        int id = texture.InstanceId;
        ulong revision = texture.GetRevision();
        if (entries.TryGetValue(id, out Entry? entry) && entry.revision == revision) return entry;

        Entry created = new()
        {
            pixels = texture.pixels ?? [],
            width = texture.width,
            height = texture.height,
            channels = texture.channels,
            revision = revision,
        };
        entries[id] = created;
        return created;
    }

    //取一个 texel 的 Alpha；越界算零，与 shader 的边界外取零一致。
    private static float Sample(Entry entry, int x, int y)
    {
        if (x < 0 || y < 0 || x >= entry.width || y >= entry.height) return 0.0f;
        int pixelBytes = entry.channels;
        long offset = ((long)y * entry.width + x) * pixelBytes;
        if (pixelBytes != 1 && pixelBytes != 4) return 1.0f;
        if (offset < 0 || offset + pixelBytes > entry.pixels.Length) return 0.0f;
        //R8 读 R，RGBA 读 A。
        return entry.pixels[offset + (pixelBytes - 1)] / 255.0f;
    }
}
