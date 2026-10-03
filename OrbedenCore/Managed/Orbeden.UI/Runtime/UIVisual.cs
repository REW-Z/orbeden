using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 可绘制图形的基类。网格只在几何或材质标脏时重建，不每帧调用 PopulateMesh；
/// 重建结果按片段缓存，提交时的颜色是自身 tint 与控件状态色的乘积。
/// </summary>
public abstract class UIVisual : UIElement, IUILayoutMeasure
{
    /// <summary>自身颜色，与控件状态色相乘后进入提交。</summary>
    [SerializeField] private color tint = new(1.0f, 1.0f, 1.0f, 1.0f);

    /// <summary>是否参与指针命中。文字默认不阻挡指针，图片默认阻挡。</summary>
    [SerializeField] private bool raycastTarget = true;

    /// <summary>显式材质；空表示用引擎内置 UI 材质。</summary>
    [SerializeField] private Material? material;

    //网格按视图变体分桶：同一份图形在世界空间下被多台相机看到时，
    //每台相机的字形尺寸不同，几何必须各存一份，不能互相顶掉。
    private readonly Dictionary<int, UIMeshBuilder> variantMeshes = [];
    private readonly Dictionary<int, ulong> variantRevisions = [];
    private readonly List<int> variantOrder = [];
    //本帧各变体的最终片段状态；与网格同寿命，材质修改器每帧只跑一次。
    private readonly Dictionary<int, List<UIDrawState>> variantDrawStates = [];
    //同 Ens 上实现两个扩展接口的组件；各自按挂载顺序运行，组件数量变化时重扫。
    private List<IUIMeshModifier>? meshModifiers;
    private List<IUIMaterialModifier>? materialModifiers;
    private int modifierScriptCount = -1;
    private bool geometryDirty = true;
    private bool rebuildFailed;
    private ulong meshRevision = 1;

    //控件状态色乘子：由 UIControl 写入，不改变上面的持久化 tint。
    private color stateMultiplier = new(1.0f, 1.0f, 1.0f, 1.0f);

    /// <summary>创建图形组件包装。</summary>
    protected UIVisual(Ens ens) : base(ens)
    {
    }

    /// <summary>自身颜色。</summary>
    public color GetTint() => tint;

    /// <summary>设置自身颜色；提交状态每帧重算，不需要脏标记。</summary>
    public void SetTint(color value)
    {
        if (Same(tint, value)) return;
        tint = value;
    }

    /// <summary>是否参与指针命中。</summary>
    public bool GetRaycastTarget() => raycastTarget;

    /// <summary>设置是否参与指针命中。</summary>
    public void SetRaycastTarget(bool value)
    {
        if (raycastTarget == value) return;
        raycastTarget = value;
        UIWorldContext.MarkInputDirty(this);
    }

    /// <summary>提交时使用的颜色：自身 tint 乘控件状态色。</summary>
    public color GetComposedTint() => Multiply(tint, stateMultiplier);

    /// <summary>显式材质；空表示用引擎内置 UI 材质。</summary>
    public Material? GetMaterial() => material;

    /// <summary>设置显式材质；只影响提交状态，不重建几何。</summary>
    public void SetMaterial(Material? value)
    {
        if (ReferenceEquals(material, value)) return;
        material = value;
    }

    /// <summary>标记顶点需要重建；同时让命中快照失效。</summary>
    public void SetVerticesDirty()
    {
        geometryDirty = true;
        rebuildFailed = false;
        UIWorldContext.MarkGeometryDirty(this);
    }

    /// <summary>判断当前是否有待重建内容。</summary>
    public bool IsDirty => geometryDirty;

    /// <summary>默认命中判定：解析矩形包含本地坐标点。零尺寸不命中。</summary>
    public virtual bool Raycast(vector2 localPoint)
    {
        UILayout? layout = GetLayout();
        return layout != null && layout.GetResolvedRect().Contains(localPoint);
    }

    /// <summary>默认期望宽度取自布局的尺寸增量；派生图形按自身内容重写。</summary>
    public virtual float MeasureWidth() => MathF.Max(0.0f, GetLayout()?.GetSizeDelta().x ?? 0.0f);

    /// <summary>默认期望高度取自布局的尺寸增量；派生图形按自身内容重写。</summary>
    public virtual float MeasureHeight(float availableWidth) => MathF.Max(0.0f, GetLayout()?.GetSizeDelta().y ?? 0.0f);

    /// <summary>生成网格；派生图形在这里调用 UIMeshBuilder 添加顶点与三角形。顶点颜色用白色。</summary>
    protected abstract void PopulateMesh(UIMeshBuilder mesh);

    /// <summary>逐片段修改绘制状态；派生图形在这里覆盖纹理、材质种类与距离场范围。</summary>
    protected internal virtual void ModifyDrawState(ref UIDrawState state)
    {
    }

    /// <summary>默认视图（不限定相机）的网格；只有重建过才有效。</summary>
    public UIMeshBuilder Mesh => GetMesh(0);

    /// <summary>取某个视图变体的网格；第一次取用时按需创建。</summary>
    public UIMeshBuilder GetMesh(int variant)
    {
        if (variantMeshes.TryGetValue(variant, out UIMeshBuilder? existing)) return existing;

        UIMeshBuilder created = new();
        variantMeshes.Add(variant, created);
        variantRevisions[variant] = 1;
        variantOrder.Add(variant);
        return created;
    }

    /// <summary>本帧重建过的变体，按首次出现顺序；帧构建按这个顺序提交。</summary>
    internal IReadOnlyList<int> Variants => variantOrder;

    /// <summary>外部依赖是否失效需要重建几何；派生图形在这里检查缓存代次，默认永不失效。</summary>
    protected internal virtual bool IsGeometryInvalidated() => false;

    /// <summary>几何是否需要重建。</summary>
    public bool NeedsGeometryRebuild => geometryDirty;

    /// <summary>上次重建是否失败；失败时该图形本帧不产出任何片段。</summary>
    public bool RebuildFailed => rebuildFailed;

    /// <summary>默认视图的网格内容版本；缓存据此判断是否需要重新上传。</summary>
    public ulong MeshRevision => GetMeshRevision(0);

    /// <summary>某个视图变体的网格内容版本。</summary>
    public ulong GetMeshRevision(int variant) =>
        variantRevisions.TryGetValue(variant, out ulong revision) ? revision : 0UL;

    //控件状态色由拥有它的控件写入，不影响持久化的 tint。
    internal void SetStateMultiplier(color value)
    {
        if (Same(stateMultiplier, value)) return;
        stateMultiplier = value;
    }

    //执行一次重建，逐视图变体各建一份；PopulateMesh 抛异常时清空当前图形并记录错误，不留下半套网格。
    internal void Rebuild()
    {
        geometryDirty = false;
        rebuildFailed = false;
        RefreshMeshModifiers();
        variantDrawStates.Clear();

        //本帧要出图的变体：屏幕与离屏画布只有一个（观察者 0），
        //世界空间画布按上一帧每一台看到它的相机各来一个。
        List<UIWorldContext.UIViewTarget> targets = UIWorldContext.Current?.CanvasViewTargets(GetCanvas())
            ?? FallbackTargets;
        foreach (UIWorldContext.UIViewTarget target in targets)
        {
            int variant = VariantOf(target.ViewerId);
            UIMeshBuilder mesh = GetMesh(variant);
            mesh.Clear();
            //视图相关的光栅参数在构建之前写入：文字按光栅缩放选位图字号，其它图形忽略它。
            //重建发生在帧构建之前，因此这里用的是上一帧发布的视图。
            mesh.ViewScale = target.RasterScale;
            mesh.ViewVariant = variant;
            try
            {
                //顺序固定：图形自身片段 → 网格修改器 → 校验 → 记版本。
                PopulateMesh(mesh);
                mesh.Complete();
                RunMeshModifiers(mesh);
                CheckMesh(mesh);
            }
            catch (Exception exception)
            {
                mesh.Clear();
                rebuildFailed = true;
                Console.Error.WriteLine(
                    $"UIVisual {GetType().Name}({EnsId.id}:{EnsId.version}) 重建失败：{exception}");
                continue;
            }
            variantRevisions[variant] = NextRevision(variantRevisions[variant]);
        }

        PruneVariants(targets);
        meshRevision = NextRevision(meshRevision);
    }

    //修改器集合只在组件数量变化时重扫；顺序取组件的挂载顺序，不做反射发现。
    private void RefreshMeshModifiers()
    {
        int count = ScriptRuntimeRegistry.GetScriptCount(EnsId);
        if (meshModifiers != null && modifierScriptCount == count) return;

        modifierScriptCount = count;
        meshModifiers ??= [];
        materialModifiers ??= [];
        meshModifiers.Clear();
        materialModifiers.Clear();
        foreach (Script script in ScriptRuntimeRegistry.GetScripts(EnsId))
        {
            if (!script.GetEnabled()) continue;
            if (script is IUIMeshModifier meshModifier) meshModifiers.Add(meshModifier);
            if (script is IUIMaterialModifier materialModifier) materialModifiers.Add(materialModifier);
        }
    }

    //依次运行网格修改器；单个修改器失败即整份产物作废，不留下半套几何。
    private void RunMeshModifiers(UIMeshBuilder mesh)
    {
        if (meshModifiers == null) return;
        foreach (IUIMeshModifier modifier in meshModifiers)
        {
            try
            {
                modifier.ModifyMesh(mesh);
                mesh.Complete();
            }
            catch (Exception exception)
            {
                throw new InvalidOperationException(
                    $"网格修改器 {modifier.GetType().Name} 执行失败。", exception);
            }
        }
    }

    /// <summary>
    /// 把一个变体的全部基础状态算成最终状态并缓存；每帧在排序与依赖收集之前调用一次，
    /// 因此材质修改器每帧只跑一次，依赖收集与提交读的是同一份结果。
    /// </summary>
    internal void PrepareDrawStates()
    {
        foreach (int variant in variantOrder)
        {
            if (!variantDrawStates.TryGetValue(variant, out List<UIDrawState>? states))
            {
                states = [];
                variantDrawStates[variant] = states;
            }
            states.Clear();
            foreach (UIMeshFragment fragment in variantMeshes[variant].Fragments)
            {
                UIDrawState state = fragment.state;
                ComposeDrawState(ref state);
                states.Add(state);
            }
        }
    }

    /// <summary>取某个变体某个片段的最终绘制状态；没有准备过这一帧时返回假。</summary>
    internal bool TryGetDrawState(int variant, int fragment, out UIDrawState state)
    {
        state = default;
        if (!variantDrawStates.TryGetValue(variant, out List<UIDrawState>? states)) return false;
        if ((uint)fragment >= (uint)states.Count) return false;
        state = states[fragment];
        return true;
    }

    //按顺序把一个片段的基础状态算成最终状态：图形规则 → 显式材质 → 材质修改器 → 颜色。
    private void ComposeDrawState(ref UIDrawState state)
    {
        ModifyDrawState(ref state);
        if (material != null) state.material = material;

        if (materialModifiers != null)
        {
            foreach (IUIMaterialModifier modifier in materialModifiers)
            {
                try
                {
                    modifier.ModifyMaterial(ref state);
                }
                catch (Exception exception)
                {
                    Console.Error.WriteLine(
                        $"材质修改器 {modifier.GetType().Name}({EnsId.id}:{EnsId.version}) 执行失败：{exception}");
                }
            }
        }

        state.tint = Multiply(state.tint, GetComposedTint());
    }

    //修改器可能破坏索引合法性：逐条核对，越界即视为本次重建失败。
    private static void CheckMesh(UIMeshBuilder mesh)
    {
        int vertexCount = mesh.Vertices.Count;
        int triangles = mesh.TriangleCount;
        for (int triangle = 0; triangle < triangles; ++triangle)
        {
            if (!mesh.TryGetTriangle(triangle, out int a, out int b, out int c))
                throw new InvalidOperationException($"网格索引不完整：三角形 {triangle}。");
            if ((uint)a >= (uint)vertexCount || (uint)b >= (uint)vertexCount || (uint)c >= (uint)vertexCount)
                throw new InvalidOperationException($"网格索引越界：三角形 {triangle} 引用了不存在的顶点。");
        }
    }

    //相机被删或场景里不再有多相机时，多余的变体连网格一起丢掉。
    //几何缓存那边不用管：连续两帧没人引用的网格由它自行释放。
    private void PruneVariants(List<UIWorldContext.UIViewTarget> targets)
    {
        if (variantMeshes.Count <= targets.Count) return;

        for (int index = variantOrder.Count - 1; index >= 0; --index)
        {
            int variant = variantOrder[index];
            bool keep = false;
            foreach (UIWorldContext.UIViewTarget target in targets)
            {
                if (VariantOf(target.ViewerId) != variant) continue;
                keep = true;
                break;
            }
            if (keep) continue;
            variantOrder.RemoveAt(index);
            variantMeshes.Remove(variant);
            variantRevisions.Remove(variant);
            variantDrawStates.Remove(variant);
        }
    }

    //没有上下文时的兜底目标：按默认比例建一份，界面不至于空着。
    private static readonly List<UIWorldContext.UIViewTarget> FallbackTargets = [UIWorldContext.UIViewTarget.Any];

    /// <summary>观察者标识到变体号的映射；0 表示不限定相机的那一份。</summary>
    internal static int VariantOf(ulong viewerId) => viewerId == 0 ? 0 : unchecked((int)(viewerId & 0x7FFF_FFFFUL));

    private static ulong NextRevision(ulong current)
    {
        ulong next = current + 1;
        return next == 0 ? 1UL : next;
    }

    //逐分量相乘；顶点色按约定是白色，因此乘完仍是提交颜色。
    private static color Multiply(color left, color right) => new(
        left.r * right.r, left.g * right.g, left.b * right.b, left.a * right.a);

    private static bool Same(color left, color right) =>
        left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;
}
