using System;

namespace Orbeden;

/// <summary>画布渲染方式。</summary>
public enum CanvasRenderMode : uint
{
    /// <summary>直接合成到显示目标上，不参与场景深度。</summary>
    Overlay = 0,

    /// <summary>在世界空间中绘制，参与场景深度遮挡。</summary>
    WorldSpace = 1,

    /// <summary>绘制到独立纹理，由场景材质采样。</summary>
    Offscreen = 2,
}

/// <summary>画布缩放方式。</summary>
public enum CanvasScaleMode : uint
{
    /// <summary>逻辑坐标与像素一一对应，只乘 scaleFactor。</summary>
    ConstantPixel = 0,

    /// <summary>按参考分辨率与显示区域的比值缩放。</summary>
    ReferenceResolution = 1,
}

/// <summary>
/// 画布：UI 子树的根，决定整个子树的缩放、排序与目标。与 UILayout 同节点共存。
/// 无画布的 UI 保留数据但不绘制；嵌套画布子树报错并停止提交。
/// </summary>
public class Canvas : UIElement
{
    /// <summary>新建世界空间画布的默认尺寸（逻辑单位）。</summary>
    public const float DefaultWorldSpaceWidth = 800.0f;
    /// <summary>新建世界空间画布的默认高度（逻辑单位）。</summary>
    public const float DefaultWorldSpaceHeight = 600.0f;
    /// <summary>新建世界空间画布的默认 Transform 缩放：一个逻辑单位折合这么多世界单位。</summary>
    public const float DefaultWorldSpaceTransformScale = 0.01f;

    [SerializeField] private CanvasRenderMode renderMode = CanvasRenderMode.Overlay;
    [SerializeField] private CanvasScaleMode scaleMode = CanvasScaleMode.ConstantPixel;
    [SerializeField] private vector2 referenceResolution = new(1920.0f, 1080.0f);
    [SerializeField] private float matchWidthOrHeight = 0.5f;
    [SerializeField] private float scaleFactor = 1.0f;
    [SerializeField] private vector2 outputSize = new(512.0f, 512.0f);
    [SerializeField] private int sortOrder;
    [SerializeField] private int drawLayer = 1;

    //离屏输出纹理是运行期资源，不参与持久化；画布销毁后若无人持有由资源回收处理。
    private Texture2D? outputTexture;

    /// <summary>创建画布组件包装。</summary>
    public Canvas(Ens ens) : base(ens)
    {
    }

    /// <summary>渲染方式。</summary>
    public CanvasRenderMode GetRenderMode() => renderMode;

    /// <summary>设置渲染方式；变化后需要重新解析根矩形与提交目标。</summary>
    public void SetRenderMode(CanvasRenderMode value)
    {
        if (renderMode == value) return;
        renderMode = value;
        MarkCanvasDirty();
    }

    /// <summary>缩放方式。</summary>
    public CanvasScaleMode GetScaleMode() => scaleMode;

    /// <summary>设置缩放方式。</summary>
    public void SetScaleMode(CanvasScaleMode value)
    {
        if (scaleMode == value) return;
        scaleMode = value;
        MarkCanvasDirty();
    }

    /// <summary>参考分辨率，各轴至少 1。</summary>
    public vector2 GetReferenceResolution() => referenceResolution;

    /// <summary>设置参考分辨率；任一轴小于 1 或非有限时拒绝写入。</summary>
    public void SetReferenceResolution(vector2 value)
    {
        if (!float.IsFinite(value.x) || !float.IsFinite(value.y) || value.x < 1.0f || value.y < 1.0f) return;
        if (referenceResolution.x == value.x && referenceResolution.y == value.y) return;
        referenceResolution = value;
        MarkCanvasDirty();
    }

    /// <summary>宽高匹配权重，0 取宽度、1 取高度，范围 [0,1]。</summary>
    public float GetMatchWidthOrHeight() => matchWidthOrHeight;

    /// <summary>设置宽高匹配权重；分量夹紧到 [0,1]。</summary>
    public void SetMatchWidthOrHeight(float value)
    {
        float clamped = Math.Clamp(float.IsFinite(value) ? value : 0.5f, 0.0f, 1.0f);
        if (matchWidthOrHeight == clamped) return;
        matchWidthOrHeight = clamped;
        MarkCanvasDirty();
    }

    /// <summary>附加缩放系数，下限 0.01。</summary>
    public float GetScaleFactor() => scaleFactor;

    /// <summary>设置附加缩放系数；小于 0.01 或非有限时拒绝写入。</summary>
    public void SetScaleFactor(float value)
    {
        if (!float.IsFinite(value) || value < 0.01f || scaleFactor == value) return;
        scaleFactor = value;
        MarkCanvasDirty();
    }

    /// <summary>离屏输出尺寸，各轴取整且至少 1。</summary>
    public vector2 GetOutputSize() => outputSize;

    /// <summary>设置离屏输出尺寸；各分量四舍五入取整，小于 1 或非有限时拒绝写入。</summary>
    public void SetOutputSize(vector2 value)
    {
        if (!float.IsFinite(value.x) || !float.IsFinite(value.y)) return;
        float width = MathF.Round(value.x);
        float height = MathF.Round(value.y);
        if (width < 1.0f || height < 1.0f) return;
        if (outputSize.x == width && outputSize.y == height) return;
        outputSize = new vector2(width, height);
        MarkCanvasDirty();
    }

    /// <summary>同屏多个画布的排序权重，升序在前。</summary>
    public int GetSortOrder() => sortOrder;

    /// <summary>设置排序权重。</summary>
    public void SetSortOrder(int value)
    {
        if (sortOrder == value) return;
        sortOrder = value;
        MarkCanvasDirty();
    }

    /// <summary>绘制层，与相机 drawLayerMask 匹配。</summary>
    public int GetDrawLayer() => drawLayer;

    /// <summary>设置绘制层。</summary>
    public void SetDrawLayer(int value)
    {
        if (drawLayer == value) return;
        drawLayer = value;
        MarkCanvasDirty();
    }

    /// <summary>按显示区域的像素尺寸计算缩放。</summary>
    public float ComputeScale(vector2 targetPixelSize) =>
        UILayoutMath.ComputeScale(scaleMode, scaleFactor, referenceResolution, matchWidthOrHeight, targetPixelSize);

    /// <summary>按显示区域的像素尺寸计算逻辑尺寸。</summary>
    public vector2 ComputeLogicalSize(vector2 targetPixelSize) =>
        UILayoutMath.ComputeLogicalSize(scaleMode, scaleFactor, referenceResolution, matchWidthOrHeight, targetPixelSize);

    /// <summary>
    /// 离屏画布的输出纹理；首次取用时按输出尺寸创建。尺寸变化只重建 GPU 侧资源，
    /// 纹理对象身份不变，因此外部持有者不必重新取引用。非离屏画布返回空。
    /// </summary>
    public Texture2D? GetOutputTexture()
    {
        if (renderMode != CanvasRenderMode.Offscreen) return null;

        int width = Math.Max(1, (int)outputSize.x);
        int height = Math.Max(1, (int)outputSize.y);
        if (outputTexture == null || !outputTexture.IsAlive)
        {
            outputTexture = Texture2D.CreateRenderTarget(width, height);
            if (outputTexture == null)
            {
                Console.Error.WriteLine($"Canvas({EnsId.id}:{EnsId.version}) 输出纹理创建失败。");
                return null;
            }
            return outputTexture;
        }

        if (outputTexture.width != width || outputTexture.height != height)
        {
            //resize 保持对象身份：只换 GPU 附件，不换 Texture2D 本身。
            outputTexture.ResizeRenderTarget(width, height);
        }
        return outputTexture;
    }

    /// <summary>
    /// 注入一次指针事件。只对离屏画布有意义：它的画面不在窗口里，
    /// 指针由使用它的场景对象喂进来；事件在下一输入阶段被排空处理。
    /// </summary>
    public void InjectPointer(in UIPointerEvent input)
    {
        if (renderMode != CanvasRenderMode.Offscreen) return;
        UIPointerEvent routed = input;
        routed.viewId = GetOutputTexture() is Texture2D output ? unchecked((ulong)(uint)output.InstanceId) : 0;
        UIWorldContext.Current?.InjectPointer(routed);
    }

    //画布配置变化后重新解析根矩形并让整棵子树失效。
    private void MarkCanvasDirty()
    {
        UIWorldContext.MarkLayoutDirty(GetLayout());
        UIWorldContext.MarkCanvasDirty(this);
    }
}
