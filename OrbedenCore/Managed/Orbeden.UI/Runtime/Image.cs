using System;

namespace Orbeden;

/// <summary>图片绘制方式。</summary>
public enum UIImageMode : uint
{
    /// <summary>整块拉伸，不保宽高比。</summary>
    Simple = 0,

    /// <summary>四角与四边保形，只有中心拉伸。</summary>
    NineSlice = 1,
}

/// <summary>
/// 图片图形。空纹理时使用引擎白纹理，用来画纯色矩形；九宫格按源区域像素切分边框。
/// </summary>
public class Image : UIVisual
{
    /// <summary>九宫格每个轴上的分割数：四个边界给出三列或三行。</summary>
    internal const int NineSliceEdgeCount = 4;

    [SerializeField] private Texture2D? texture;
    [SerializeField] private UIImageMode mode = UIImageMode.Simple;
    [SerializeField] private vector2 uvMin = new(0.0f, 0.0f);
    [SerializeField] private vector2 uvMax = new(1.0f, 1.0f);
    [SerializeField] private float borderLeft;
    [SerializeField] private float borderRight;
    [SerializeField] private float borderBottom;
    [SerializeField] private float borderTop;

    /// <summary>创建图片组件包装。</summary>
    public Image(Ens ens) : base(ens)
    {
    }

    /// <summary>图片纹理；空表示使用引擎白纹理。</summary>
    public Texture2D? GetTexture() => texture;

    /// <summary>设置图片纹理；变化只影响材质。</summary>
    public void SetTexture(Texture2D? value)
    {
        if (ReferenceEquals(texture, value)) return;
        texture = value;
    }

    /// <summary>绘制方式。</summary>
    public UIImageMode GetMode() => mode;

    /// <summary>设置绘制方式；变化需要重建几何。</summary>
    public void SetMode(UIImageMode value)
    {
        if (mode == value) return;
        mode = value;
        SetVerticesDirty();
    }

    /// <summary>UV 左下角。</summary>
    public vector2 GetUvMin() => uvMin;

    /// <summary>设置 UV 左下角；分量夹紧到 [0,1] 且不超过 uvMax。</summary>
    public void SetUvMin(vector2 value)
    {
        vector2 clamped = ClampUv(value);
        clamped.x = MathF.Min(clamped.x, uvMax.x);
        clamped.y = MathF.Min(clamped.y, uvMax.y);
        if (Same(clamped, uvMin)) return;
        uvMin = clamped;
        SetVerticesDirty();
    }

    /// <summary>UV 右上角。</summary>
    public vector2 GetUvMax() => uvMax;

    /// <summary>设置 UV 右上角；分量夹紧到 [0,1] 且不小于 uvMin。</summary>
    public void SetUvMax(vector2 value)
    {
        vector2 clamped = ClampUv(value);
        clamped.x = MathF.Max(clamped.x, uvMin.x);
        clamped.y = MathF.Max(clamped.y, uvMin.y);
        if (Same(clamped, uvMax)) return;
        uvMax = clamped;
        SetVerticesDirty();
    }

    /// <summary>左边框，单位为所选源区域像素。</summary>
    public float GetBorderLeft() => borderLeft;

    /// <summary>设置左边框；负值与非有限值拒绝写入。</summary>
    public void SetBorderLeft(float value) => SetBorder(ref borderLeft, value);

    /// <summary>右边框，单位为所选源区域像素。</summary>
    public float GetBorderRight() => borderRight;

    /// <summary>设置右边框；负值与非有限值拒绝写入。</summary>
    public void SetBorderRight(float value) => SetBorder(ref borderRight, value);

    /// <summary>下边框，单位为所选源区域像素。</summary>
    public float GetBorderBottom() => borderBottom;

    /// <summary>设置下边框；负值与非有限值拒绝写入。</summary>
    public void SetBorderBottom(float value) => SetBorder(ref borderBottom, value);

    /// <summary>上边框，单位为所选源区域像素。</summary>
    public float GetBorderTop() => borderTop;

    /// <summary>设置上边框；负值与非有限值拒绝写入。</summary>
    public void SetBorderTop(float value) => SetBorder(ref borderTop, value);

    /// <summary>测量宽度为所选源区域像素宽度；空纹理退回布局的尺寸增量。</summary>
    public override float MeasureWidth()
    {
        if (texture == null) return base.MeasureWidth();
        return MathF.Abs(uvMax.x - uvMin.x) * texture.width;
    }

    /// <summary>测量高度为所选源区域像素高度；空纹理退回布局的尺寸增量。</summary>
    public override float MeasureHeight(float availableWidth)
    {
        if (texture == null) return base.MeasureHeight(availableWidth);
        return MathF.Abs(uvMax.y - uvMin.y) * texture.height;
    }

    /// <summary>
    /// 把九宫格的一个轴切成四个边界。源轴以 UV 给出、边框以源像素给出，
    /// 目标轴以逻辑像素给出；源边框先按源像素长度同比缩小，目标边框再按目标长度同比缩小，
    /// 因此两个轴都不会出现反向或重叠的边。textureSize 是源轴对应的纹理尺寸。
    /// </summary>
    public static void ComputeNineSliceAxis(float targetMin, float targetSize,
        float sourceMin, float sourceSize, float textureSize, float borderNear, float borderFar,
        Span<float> targetEdges, Span<float> sourceEdges)
    {
        float sourcePixels = MathF.Max(0.0f, sourceSize * textureSize);
        ClampBorders(ref borderNear, ref borderFar, sourcePixels);
        float pixelsPerUnit = textureSize > 0.0f ? textureSize : 1.0f;
        sourceEdges[0] = sourceMin;
        sourceEdges[1] = sourceMin + borderNear / pixelsPerUnit;
        sourceEdges[2] = sourceMin + sourceSize - borderFar / pixelsPerUnit;
        sourceEdges[3] = sourceMin + sourceSize;

        //目标侧沿用同一份像素边框，再按目标长度缩一次。
        float targetNear = borderNear;
        float targetFar = borderFar;
        ClampBorders(ref targetNear, ref targetFar, targetSize);
        targetEdges[0] = targetMin;
        targetEdges[1] = targetMin + targetNear;
        targetEdges[2] = targetMin + targetSize - targetFar;
        targetEdges[3] = targetMin + targetSize;
    }

    /// <summary>生成图片网格。</summary>
    protected override void PopulateMesh(UIMeshBuilder mesh)
    {
        UIRect rect = GetLayout()?.GetResolvedRect() ?? new UIRect(new vector2(0.0f, 0.0f), new vector2(0.0f, 0.0f));
        if (mode == UIImageMode.NineSlice)
        {
            PopulateNineSlice(mesh, rect);
            return;
        }
        //Simple 不保宽高比：整块矩形对应整块 UV 范围。
        mesh.AddQuad(rect, uvMin, uvMax, new color(1.0f, 1.0f, 1.0f, 1.0f));
    }

    /// <summary>逐片段把纹理接进绘制状态；空纹理留给引擎白纹理。</summary>
    protected internal override void ModifyDrawState(ref UIDrawState state)
    {
        if (texture != null) state.texture = texture;
        state.materialKind = UIMaterialKind.ImageStraight;
    }

    //九宫格：四个边界把两个轴各切成三段，共 3×3 个四边形。
    private void PopulateNineSlice(UIMeshBuilder mesh, UIRect rect)
    {
        Span<float> targetX = stackalloc float[NineSliceEdgeCount];
        Span<float> targetY = stackalloc float[NineSliceEdgeCount];
        Span<float> sourceX = stackalloc float[NineSliceEdgeCount];
        Span<float> sourceY = stackalloc float[NineSliceEdgeCount];

        float textureWidth = texture?.width ?? 0.0f;
        float textureHeight = texture?.height ?? 0.0f;

        ComputeNineSliceAxis(rect.min.x, rect.size.x, uvMin.x, uvMax.x - uvMin.x, textureWidth,
            borderLeft, borderRight, targetX, sourceX);
        ComputeNineSliceAxis(rect.min.y, rect.size.y, uvMin.y, uvMax.y - uvMin.y, textureHeight,
            borderBottom, borderTop, targetY, sourceY);

        //先把 4×4 顶点建出来，再连 3×3 个四边形，避免逐格重复顶点。
        //边界由 ClampBorders 保证非递减，因此不会出现反向缠绕；退化格只是零面积三角形。
        int start = mesh.Vertices.Count;
        color white = new(1.0f, 1.0f, 1.0f, 1.0f);
        for (int row = 0; row < NineSliceEdgeCount; ++row)
        {
            for (int column = 0; column < NineSliceEdgeCount; ++column)
            {
                mesh.AddVertex(new vector3(targetX[column], targetY[row], 0.0f),
                    new vector2(sourceX[column], sourceY[row]), white);
            }
        }

        for (int row = 0; row < NineSliceEdgeCount - 1; ++row)
        {
            for (int column = 0; column < NineSliceEdgeCount - 1; ++column)
            {
                int lowerLeft = start + row * NineSliceEdgeCount + column;
                int lowerRight = lowerLeft + 1;
                int upperLeft = lowerLeft + NineSliceEdgeCount;
                int upperRight = upperLeft + 1;
                mesh.AddTriangle(lowerLeft, lowerRight, upperRight);
                mesh.AddTriangle(lowerLeft, upperRight, upperLeft);
            }
        }
    }

    //按剩余空间同比缩小一对边框，保证两边之和不超过可用长度。
    private static void ClampBorders(ref float near, ref float far, float extent)
    {
        if (near < 0.0f) near = 0.0f;
        if (far < 0.0f) far = 0.0f;
        if (extent <= 0.0f)
        {
            near = 0.0f;
            far = 0.0f;
            return;
        }
        float total = near + far;
        if (total <= extent) return;
        float scale = extent / total;
        near *= scale;
        far *= scale;
    }

    private void SetBorder(ref float target, float value)
    {
        if (!float.IsFinite(value) || value < 0.0f || target == value) return;
        target = value;
        SetVerticesDirty();
    }

    private static vector2 ClampUv(vector2 value) =>
        new(Math.Clamp(float.IsFinite(value.x) ? value.x : 0.0f, 0.0f, 1.0f),
            Math.Clamp(float.IsFinite(value.y) ? value.y : 0.0f, 0.0f, 1.0f));

    private static bool Same(vector2 left, vector2 right) => left.x == right.x && left.y == right.y;
}
