namespace Orbeden;

/// <summary>
/// 一个网格片段的绘制状态。片段按状态切分：状态变化就结束当前片段、开始新的一个。
/// 距离场材质才会用到 distanceRange；纹理颜色空间由资源自身决定，不在这里声明。
/// </summary>
public struct UIDrawState
{
    /// <summary>片段使用的纹理；空表示用引擎白纹理。</summary>
    public Texture2D? texture;

    /// <summary>材质种类。</summary>
    public UIMaterialKind materialKind;

    /// <summary>片段级别的颜色乘子。</summary>
    public color tint;

    /// <summary>距离场取值范围；只对 SDF 与 MSDF 有效。</summary>
    public float distanceRange;

    /// <summary>显式材质；空表示用引擎内置 UI 材质，外观与旧版一致。</summary>
    public Material? material;

    /// <summary>默认状态：白纹理、直通图片、白色、距离场范围 4。</summary>
    public static UIDrawState Default => new()
    {
        texture = null,
        materialKind = UIMaterialKind.ImageStraight,
        tint = new color(1.0f, 1.0f, 1.0f, 1.0f),
        distanceRange = 4.0f,
        material = null,
    };

    /// <summary>判断两个状态是否会产生相同的绘制批次。</summary>
    public readonly bool Matches(in UIDrawState other) =>
        ReferenceEquals(texture, other.texture)
        && ReferenceEquals(material, other.material)
        && materialKind == other.materialKind
        && tint.r == other.tint.r && tint.g == other.tint.g && tint.b == other.tint.b && tint.a == other.tint.a
        && distanceRange == other.distanceRange;
}

/// <summary>一个片段的索引区间与它的绘制状态。</summary>
public readonly struct UIMeshFragment
{
    /// <summary>片段在索引缓冲里的起始位置。</summary>
    public readonly int firstIndex;

    /// <summary>片段的索引数量。</summary>
    public readonly int indexCount;

    /// <summary>片段的绘制状态。</summary>
    public readonly UIDrawState state;

    /// <summary>创建片段描述。</summary>
    public UIMeshFragment(int firstIndex, int indexCount, in UIDrawState state)
    {
        this.firstIndex = firstIndex;
        this.indexCount = indexCount;
        this.state = state;
    }
}
