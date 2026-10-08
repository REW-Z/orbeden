using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>一个裁剪层。形状用本地矩形描述，Alpha 层另有 UV 与阈值。</summary>
internal struct UIClipLayer
{
    /// <summary>层拥有者的运行时 ID，用作网格键。</summary>
    internal int ownerId;

    /// <summary>层在栈中的位置，用作网格键的片段编号。</summary>
    internal int slot;

    /// <summary>形状内容版本；形状变化时推进，网格据此重新上传。</summary>
    internal ulong revision;

    /// <summary>遮罩方式。</summary>
    internal UIMaskMode mode;

    /// <summary>本地空间矩形，形状与采样范围都由它给出。</summary>
    internal UIRect rect;

    /// <summary>Alpha 层的 UV 范围。</summary>
    internal vector2 uvMin;
    internal vector2 uvMax;

    /// <summary>Alpha 层的命中阈值。</summary>
    internal float threshold;

    /// <summary>Alpha 层的纹理；矩形层为空。</summary>
    internal Texture2D? texture;

    /// <summary>画布空间到本层本地空间的逆矩阵。</summary>
    internal matrix4x4 inverse;

    /// <summary>本层本地空间到画布空间的矩阵；生成裁剪命令用。</summary>
    internal matrix4x4 forward;

    /// <summary>矩阵可逆；不可逆的层整段不可见、不可命中。</summary>
    internal bool invertible;
}

/// <summary>
/// 一层裁剪的不可变快照。命中记录持有它，输入阶段据此判断点是否穿过全部裁剪层；
/// 同一段裁剪里的记录共用一份，不逐条复制。
/// </summary>
public sealed class UIClipSnapshot
{
    /// <summary>没有任何裁剪的快照；恒为可见。</summary>
    public static readonly UIClipSnapshot Empty = new([], null);

    private readonly UIClipLayer[] layers;
    private readonly UITextureAlphaCache? alpha;

    internal UIClipSnapshot(UIClipLayer[] layers, UITextureAlphaCache? alpha)
    {
        this.layers = layers;
        this.alpha = alpha;
    }

    /// <summary>层数。</summary>
    public int Depth => layers.Length;

    /// <summary>
    /// 判断画布空间的一点是否可见。矩形层判包含，Alpha 层按 UV 双线性采样；
    /// 任意一层不可逆、未通过阈值，或不在矩形内都算不可命中。
    /// </summary>
    public bool TestPoint(vector2 canvasPoint)
    {
        float cumulative = 1.0f;
        for (int index = 0; index < layers.Length; ++index)
        {
            UIClipLayer layer = layers[index];
            //奇异矩阵视为不可见，空 Alpha 纹理覆盖率为零。
            if (!layer.invertible) return false;
            if (layer.mode == UIMaskMode.ImageAlpha && (layer.texture == null || !layer.texture.IsAlive)) return false;
            if (layer.rect.Width <= 0.0f || layer.rect.Height <= 0.0f) return false;

            vector2 local = UIMatrix.TransformPoint(layer.inverse, canvasPoint);
            if (layer.mode == UIMaskMode.Rectangle)
            {
                if (!layer.rect.Contains(local)) return false;
                continue;
            }

            float u = layer.uvMin.x + (local.x - layer.rect.min.x) / layer.rect.Width * (layer.uvMax.x - layer.uvMin.x);
            float v = layer.uvMin.y + (local.y - layer.rect.min.y) / layer.rect.Height * (layer.uvMax.y - layer.uvMin.y);
            cumulative *= alpha?.Sample(layer.texture, u, v) ?? 0.0f;
            if (cumulative < layer.threshold) return false;
        }
        return true;
    }
}

/// <summary>
/// 裁剪栈。保存每层的局部矩形、逆矩阵、UV 与阈值：帧构建器按入栈出栈生成裁剪命令，
/// 命中检测用同一份数据算累计覆盖率。累计覆盖率为各层乘积，每层累计值达到该层阈值才允许命中。
/// </summary>
public sealed class UIClipStack
{
    //形状缓存的键：拥有者与栈内位置共同决定一个稳定的槽位。
    private readonly record struct SlotKey(int OwnerId, int Slot);

    private struct SlotState
    {
        internal UIRect rect;
        internal vector2 uvMin;
        internal vector2 uvMax;
        internal ulong revision;
        internal bool valid;
    }

    private readonly List<UIClipLayer> layers = [];
    private readonly Dictionary<SlotKey, SlotState> slots = [];
    private readonly UITextureAlphaCache alpha = new();
    private UIClipSnapshot snapshot = UIClipSnapshot.Empty;
    private bool snapshotDirty;

    /// <summary>当前层数。</summary>
    public int Depth => layers.Count;

    /// <summary>栈是否为空。</summary>
    public bool IsEmpty => layers.Count == 0;

    /// <summary>Alpha 采样缓存；ImageAlpha 层与命中检测共用。</summary>
    public UITextureAlphaCache AlphaCache => alpha;

    /// <summary>按 Mask 组件入栈；形状取该节点解析后的矩形。</summary>
    public void Push(Mask mask)
    {
        ArgumentNullException.ThrowIfNull(mask);
        UINode? node = UIWorldContext.Current?.FindNode(mask.EnsId);
        UILayout? layout = node?.Layout;
        if (layout == null)
        {
            //没有布局就没有形状，按零覆盖率入栈，整段子树不可见。
            PushLayer(new UIClipLayer
            {
                ownerId = mask.InstanceId,
                slot = layers.Count,
                mode = UIMaskMode.ImageAlpha,
                texture = null,
                threshold = 0.0f,
                invertible = false,
            });
            return;
        }

        PushNodeLayer(new UIClipLayer
        {
            ownerId = mask.InstanceId,
            slot = layers.Count,
            mode = mask.GetMode(),
            rect = layout.GetResolvedRect(),
            uvMin = mask.GetUvMin(),
            uvMax = mask.GetUvMax(),
            threshold = mask.GetHitTestThreshold(),
            texture = mask.GetMode() == UIMaskMode.ImageAlpha ? mask.GetTexture() : null,
        }, node);
    }

    /// <summary>按布局矩形入栈一个矩形裁剪；ScrollBox 用它只裁剪 content 子树。</summary>
    public void PushRectangle(UILayout layout)
    {
        ArgumentNullException.ThrowIfNull(layout);
        UINode? node = UIWorldContext.Current?.FindNode(layout.EnsId);
        PushNodeLayer(new UIClipLayer
        {
            ownerId = layout.InstanceId,
            slot = layers.Count,
            mode = UIMaskMode.Rectangle,
            rect = layout.GetResolvedRect(),
            uvMin = new vector2(0.0f, 0.0f),
            uvMax = new vector2(1.0f, 1.0f),
            threshold = 0.0f,
            texture = null,
        }, node);
    }

    /// <summary>出栈一层；空栈时无副作用。</summary>
    public void Pop()
    {
        if (layers.Count == 0) return;
        layers.RemoveAt(layers.Count - 1);
        snapshotDirty = true;
    }

    /// <summary>当前层的不可变快照；命中记录持有它，栈变化后自动重建。</summary>
    public UIClipSnapshot Current
    {
        get
        {
            if (snapshotDirty)
            {
                snapshot = layers.Count == 0
                    ? UIClipSnapshot.Empty
                    : new UIClipSnapshot([.. layers], alpha);
                snapshotDirty = false;
            }
            return snapshot;
        }
    }

    /// <summary>判断画布空间的一点是否穿过当前全部裁剪层。</summary>
    public bool TestPoint(vector2 canvasPoint) => Current.TestPoint(canvasPoint);

    /// <summary>清空全部层。</summary>
    public void Clear()
    {
        layers.Clear();
        snapshotDirty = true;
    }

    /// <summary>取栈顶层的形状描述；帧构建器据此生成裁剪命令。空栈时返回假。</summary>
    internal bool TryPeekTop(out UIClipLayer layer)
    {
        if (layers.Count == 0)
        {
            layer = default;
            return false;
        }
        layer = layers[^1];
        return true;
    }

    //入栈并结算形状版本：同一槽位的形状没变就复用上一版，网格不必重传。
    private void PushLayer(UIClipLayer layer)
    {
        SlotKey key = new(layer.ownerId, layer.slot);
        if (slots.TryGetValue(key, out SlotState state) && state.valid
            && state.rect.min.x == layer.rect.min.x && state.rect.min.y == layer.rect.min.y
            && state.rect.size.x == layer.rect.size.x && state.rect.size.y == layer.rect.size.y
            && state.uvMin.x == layer.uvMin.x && state.uvMin.y == layer.uvMin.y
            && state.uvMax.x == layer.uvMax.x && state.uvMax.y == layer.uvMax.y)
        {
            layer.revision = state.revision;
        }
        else
        {
            layer.revision = state.valid ? state.revision + 1 : 1;
            slots[key] = new SlotState
            {
                rect = layer.rect,
                uvMin = layer.uvMin,
                uvMax = layer.uvMax,
                revision = layer.revision,
                valid = true,
            };
        }

        layers.Add(layer);
        snapshotDirty = true;
    }

    //补齐层的矩阵信息：节点不在 UI 树里或矩阵奇异都算不可见。
    private void PushNodeLayer(UIClipLayer layer, UINode? node)
    {
        if (node != null)
        {
            layer.forward = UIMatrix.Compose(node);
            layer.invertible = UIMatrix.TryInvertAffine(layer.forward, out layer.inverse);
        }
        PushLayer(layer);
    }
}
