using System;

namespace Orbeden;

/// <summary>遮罩方式。</summary>
public enum UIMaskMode : uint
{
    /// <summary>用节点矩形裁剪。</summary>
    Rectangle = 0,

    /// <summary>按纹理 Alpha 裁剪。</summary>
    ImageAlpha = 1,
}

/// <summary>
/// 遮罩组件。作用于同节点图形与整棵子树，自己不输出颜色；
/// 只描述形状，裁剪与覆盖率的执行在 UIClipStack 与原生渲染器。
/// </summary>
public class Mask : UIElement
{
    [SerializeField] private UIMaskMode mode = UIMaskMode.Rectangle;
    [SerializeField] private Texture2D? texture;
    [SerializeField] private vector2 uvMin = new(0.0f, 0.0f);
    [SerializeField] private vector2 uvMax = new(1.0f, 1.0f);
    [SerializeField] private float hitTestThreshold = 0.1f;

    /// <summary>创建遮罩组件包装。</summary>
    public Mask(Ens ens) : base(ens)
    {
    }

    /// <summary>遮罩方式。</summary>
    public UIMaskMode GetMode() => mode;

    /// <summary>设置遮罩方式。</summary>
    public void SetMode(UIMaskMode value)
    {
        if (mode == value) return;
        mode = value;
        NotifyConfigurationChanged();
    }

    /// <summary>Alpha 纹理；矩形模式不使用。</summary>
    public Texture2D? GetTexture() => texture;

    /// <summary>设置 Alpha 纹理；渲染目标没有 CPU 像素，直接拒绝。</summary>
    public void SetTexture(Texture2D? value)
    {
        if (ReferenceEquals(texture, value)) return;
        if (value != null && value.IsRenderTarget())
        {
            Console.Error.WriteLine("Mask: 不接受渲染目标作为 Alpha 纹理。");
            return;
        }
        texture = value;
        NotifyConfigurationChanged();
    }

    /// <summary>UV 左下角。</summary>
    public vector2 GetUvMin() => uvMin;

    /// <summary>设置 UV 左下角；夹紧到 [0,1] 且不超过 uvMax。</summary>
    public void SetUvMin(vector2 value)
    {
        vector2 clamped = ClampUv(value);
        clamped.x = MathF.Min(clamped.x, uvMax.x);
        clamped.y = MathF.Min(clamped.y, uvMax.y);
        if (Same(clamped, uvMin)) return;
        uvMin = clamped;
        NotifyConfigurationChanged();
    }

    /// <summary>UV 右上角。</summary>
    public vector2 GetUvMax() => uvMax;

    /// <summary>设置 UV 右上角；夹紧到 [0,1] 且不小于 uvMin。</summary>
    public void SetUvMax(vector2 value)
    {
        vector2 clamped = ClampUv(value);
        clamped.x = MathF.Max(clamped.x, uvMin.x);
        clamped.y = MathF.Max(clamped.y, uvMin.y);
        if (Same(clamped, uvMax)) return;
        uvMax = clamped;
        NotifyConfigurationChanged();
    }

    /// <summary>命中判定的覆盖率阈值。</summary>
    public float GetHitTestThreshold() => hitTestThreshold;

    /// <summary>设置命中阈值；限于 [0,1]，非有限值拒绝写入。</summary>
    public void SetHitTestThreshold(float value)
    {
        if (!float.IsFinite(value)) return;
        float clamped = Math.Clamp(value, 0.0f, 1.0f);
        if (hitTestThreshold == clamped) return;
        hitTestThreshold = clamped;
        NotifyConfigurationChanged();
    }

    /// <summary>配置错误描述；非空时该节点与子树按不可见处理，检视面板据此报错。</summary>
    public string GetDiagnostic()
    {
        if (mode != UIMaskMode.ImageAlpha) return string.Empty;
        if (texture == null) return "Mask ImageAlpha mode has no texture; coverage is zero.";
        if (texture.IsRenderTarget()) return "Mask does not accept render-target textures; coverage is zero.";
        if (texture.pixels == null || texture.pixels.Length == 0) return "Mask texture has no CPU pixels; coverage is zero.";
        return string.Empty;
    }

    //配置变化：整棵子树的裁剪与命中都要重算。
    private void NotifyConfigurationChanged()
    {
        SetLayoutDirty();
        if (UIWorldContext.Current != null) UIWorldContext.NotifyMaskChanged(this);
    }

    private static vector2 ClampUv(vector2 value) =>
        new(float.IsFinite(value.x) ? Math.Clamp(value.x, 0.0f, 1.0f) : 0.0f,
            float.IsFinite(value.y) ? Math.Clamp(value.y, 0.0f, 1.0f) : 0.0f);

    private static bool Same(vector2 left, vector2 right) => left.x == right.x && left.y == right.y;
}
