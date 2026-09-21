using Orbeden;

namespace OrbedenEditor;

/// <summary>绘制 Ens 层级并处理选择、移动和预制体投放。</summary>
internal sealed class EnsPanel : EditorPanel
{
    private readonly Dictionary<EnsId, List<Ens>> children = [];
    private Action? pendingDrop;
    private string status = string.Empty;
    private EnsId selectedEns = EnsId.Null;
    private EnsId renamingEns = EnsId.Null;
    private string renameBuffer = string.Empty;
    private bool renameFocusRequested;
    private bool panelFocused;

    private sealed record Position(string Parent, string Before, vector3 Translation, quaternion Rotation, vector3 Scale);

    private const string InvalidNameMessage = "Ens name cannot be empty or contain control characters.";

    public override EditorPanelInfo Info => new("ens_view", "EnsView", true,
        new vector2(320, 420), PanelDockPlacement.Left, 0.22f, 200);

    /// <summary>绘制层级并在遍历结束后提交投放。</summary>
    protected override void DrawContent(EditorPanelContext context)
    {
        //F2 与 Delete 由选择系统按焦点派发到这里，重命名还要认得当前选中的是谁
        panelFocused = NativeEditorGUI.IsWindowFocused();
        selectedEns = context.SelectedEns;
        EditorSelection.Report(Info.Id, panelFocused, !selectedEns.IsNull);
        if (EditorApplication.IsPlaying) EndRename();

        children.Clear();
        pendingDrop = null;
        foreach (EnsId id in EditorNativeComponents.GetWorldEns())
        {
            Ens ens = Ens.FromId(id);
            if (!ens.IsValid) continue;
            EnsId parent = ens.Transform.GetParent();
            if (!children.TryGetValue(parent, out List<Ens>? siblings)) children[parent] = siblings = [];
            siblings.Add(ens);
        }
        if (children.TryGetValue(EnsId.Null, out List<Ens>? roots))
            foreach (Ens ens in roots) DrawNode(ens, context);
        else EditorGUI.Label("No Ens objects.");

        if (!string.IsNullOrEmpty(status)) EditorGUI.Label(status);

        int state = NativeEditorGUI.FillRemainingArea();
        if ((state & 1) != 0 && (state & 2) == 0) EditorNativeComponents.SelectEns(EnsId.Null);
        //空白处的右键菜单以 World 根为上下文
        if (EditorGUI.BeginPopupContextWindow("##ens_background_menu"))
        {
            try { DrawContextMenu(Ens.Null); }
            finally { EditorGUI.EndPopup(); }
        }
        DrawDropTarget(Ens.Null, 0);
        Action? action = pendingDrop;
        pendingDrop = null;
        action?.Invoke();
    }

    //绘制节点并记录选择或投放操作
    private void DrawNode(Ens ens, EditorPanelContext context)
    {
        bool hasChildren = children.TryGetValue(ens.Id, out List<Ens>? descendants);
        bool renaming = renamingEns.Equals(ens.Id);
        //重命名时把名称让给输入框。隐藏标签后节点宽度仍是「箭头+标签宽」，输入框正好从原名称的位置开始
        int state = NativeEditorGUI.TreeNode((renaming ? string.Empty : ens.Name) + "##ens_" + ens.ResourceKey,
            context.SelectedEnsList.Contains(ens.Id), !hasChildren, true);
        if (renaming)
        {
            EditorGUI.SameLine();
            DrawRenameInput(ens);
        }
        if ((state & 2) != 0) EditorNativeComponents.SelectEns(ens.Id, (state & 4) != 0);
        DrawDropTarget(ens, NativeEditorGUI.GetDropPlacement());
        NativeEditorGUI.DragSource(1, ens.ResourceKey);
        if (EditorGUI.BeginPopupContextItem("##ens_menu_" + ens.ResourceKey))
        {
            try { DrawContextMenu(ens); }
            finally { EditorGUI.EndPopup(); }
        }
        if ((state & 1) == 0) return;
        try
        {
            if (descendants != null)
                foreach (Ens child in descendants) DrawNode(child, context);
        }
        finally { NativeEditorGUI.TreePop(); }
    }

    /// <summary>面板隐藏后不再持有选择。</summary>
    public override void OnHidden() => EditorSelection.Clear(Info.Id);

    /// <summary>F2 触发：就地重命名当前选中的 Ens。</summary>
    public override void OnRenameRequested()
    {
        if (EditorApplication.IsPlaying) return;
        BeginRename(selectedEns);
    }

    /// <summary>Delete 触发：删除当前选中的 Ens。</summary>
    public override void OnDeleteRequested()
    {
        if (EditorApplication.IsPlaying) return;
        //Ens 删除可撤销，所以不弹确认；Project 的资源删除进回收站且不可撤销，那边才确认
        DeleteEns(Ens.FromId(selectedEns));
    }

    //进入就地重命名，输入框落在原来名称的位置
    private void BeginRename(EnsId id)
    {
        Ens ens = Ens.FromId(id);
        if (!ens.IsValid) return;
        renamingEns = id;
        renameBuffer = ens.Name;
        renameFocusRequested = true;
        EditorApplication.RequestRepaint();
    }

    //退出就地重命名
    private void EndRename()
    {
        renamingEns = EnsId.Null;
        renameBuffer = string.Empty;
        renameFocusRequested = false;
    }

    //绘制就地重命名输入框：回车或失焦应用，Esc 放弃
    private void DrawRenameInput(Ens ens)
    {
        int result = EditorGUI.RenameInput("##ens_rename", ref renameBuffer, ref renameFocusRequested);
        if (result == 0) return;
        if (result == 3) { EndRename(); return; }   //Esc，文本已由框架还原

        string name = renameBuffer.Trim();
        if (!IsValidName(name))
        {
            status = InvalidNameMessage;
            //失焦时人的注意力已经走了，只能放弃；回车则留在原地接着改
            if (result == 2) EndRename();
            else renameFocusRequested = true;
            return;
        }

        ApplyRename(ens, name);
        EndRename();
    }

    //改名并记一次撤销事务
    private static void ApplyRename(Ens ens, string name)
    {
        if (string.Equals(ens.Name, name, StringComparison.Ordinal)) return;

        string key = ens.ResourceKey;
        string previous = ens.Name;
        ens.Name = name;
        EditorPropertyHistory.PushAction("Rename Ens",
            () => SetEnsName(key, previous),
            () => SetEnsName(key, name));
    }

    //按稳定 ID 写回名称，对象已经不在了就跳过
    private static void SetEnsName(string key, string name)
    {
        Ens ens = Ens.Find(key);
        if (ens.IsValid) ens.Name = name;
    }

    //名称会作为属性写进 world XML，空白名与不可见字符读回来会被规范化，直接拒绝
    private static bool IsValidName(string name)
    {
        if (string.IsNullOrWhiteSpace(name) || name.Length > 128) return false;
        foreach (char value in name)
            if (char.IsControl(value)) return false;
        return true;
    }

    //绘制节点右键菜单，末段留给扩展项
    private void DrawContextMenu(Ens target)
    {
        bool canModify = !EditorApplication.IsPlaying;
        string label = target.IsValid ? $"Create Empty Ens under {target.Name}" : "Create Empty Ens";
        if (EditorGUI.MenuItem(label, canModify)) CreateEmptyEns(target);
        if (EditorGUI.MenuItem("Rename", canModify && target.IsValid)) BeginRename(target.Id);
        if (EditorGUI.MenuItem("Duplicate", canModify && target.IsValid)) DuplicateEns(target);
        if (EditorGUI.MenuItem("Delete", canModify && target.IsValid)) DeleteEns(target);
        EnsContextMenuRegistry.Draw(new EnsContext(target.Id, target.IsValid ? target.ResourceKey : string.Empty,
            target.IsValid ? target.Name : string.Empty, target.IsValid), value => status = value);
    }

    //在目标节点下创建空 Ens，撤销删除、重做按快照恢复
    private void CreateEmptyEns(Ens parent)
    {
        Ens created = EditorAssetsNative.CreateEns("Ens");
        if (!created.IsValid) return;
        if (parent.IsValid && !EditorNativeComponents.MoveEns(created.Id, parent.Id, EnsId.Null, true))
        {
            EditorAssetsNative.DestroyEnsTree(created.Id);
            return;
        }
        string snapshot = EditorAssetsNative.CaptureEns(created.Id);
        string key = created.ResourceKey;
        string parentKey = parent.IsValid ? parent.ResourceKey : string.Empty;
        EditorPropertyHistory.PushAction("Create Ens",
            () =>
            {
                Ens value = Ens.Find(key);
                if (value.IsValid) EditorAssetsNative.DestroyEnsTree(value.Id);
            },
            () => RestoreSnapshot(snapshot, parentKey));

        //建完立刻进入重命名，和主流编辑器一样不用先想好名字
        BeginRename(created.Id);
    }

    //在同级位置复制子树，撤销删除副本、重做按快照恢复
    private static void DuplicateEns(Ens source)
    {
        string snapshot = EditorAssetsNative.CaptureEns(source.Id);
        if (snapshot.Length == 0) return;
        EnsId parent = source.Transform.GetParent();
        Ens copy = EditorAssetsNative.InstantiatePrefab(snapshot, true, parent, source.Id);
        if (!copy.IsValid) return;
        string key = copy.ResourceKey;
        string parentKey = parent.IsNull ? string.Empty : Ens.FromId(parent).ResourceKey;
        EditorPropertyHistory.PushAction("Duplicate Ens",
            () =>
            {
                Ens value = Ens.Find(key);
                if (value.IsValid) EditorAssetsNative.DestroyEnsTree(value.Id);
            },
            () => RestoreSnapshot(snapshot, parentKey));
    }

    //删除子树，撤销按快照放回原父节点
    private static void DeleteEns(Ens ens)
    {
        if (!ens.IsValid) return;
        EnsId parent = ens.Transform.GetParent();
        string parentKey = parent.IsNull ? string.Empty : Ens.FromId(parent).ResourceKey;
        string snapshot = EditorAssetsNative.CaptureEns(ens.Id);
        string key = ens.ResourceKey;
        if (snapshot.Length == 0 || !EditorAssetsNative.DestroyEnsTree(ens.Id)) return;
        EditorPropertyHistory.PushAction("Delete Ens",
            () => RestoreSnapshot(snapshot, parentKey),
            () =>
            {
                Ens value = Ens.Find(key);
                if (value.IsValid) EditorAssetsNative.DestroyEnsTree(value.Id);
            });
    }

    //按快照把子树放回指定父节点
    private static void RestoreSnapshot(string snapshot, string parentKey)
    {
        Ens parent = parentKey.Length == 0 ? Ens.Null : Ens.Find(parentKey);
        if (parentKey.Length != 0 && !parent.IsValid)
            throw new InvalidOperationException("Ens hierarchy target no longer exists.");
        if (!EditorAssetsNative.InstantiatePrefab(snapshot, true, parent.Id, EnsId.Null).IsValid)
            throw new InvalidOperationException("Cannot restore Ens subtree.");
    }

    //判断目标位置并在释放后排队提交
    private void DrawDropTarget(Ens target, int placement)
    {
        string key = NativeEditorGUI.ReadDrag(out int kind);
        if (key.Length == 0) return;
        EnsId parent = EnsId.Null, before = EnsId.Null;
        if (target.IsValid)
        {
            if (placement == 0) parent = target.Id;
            else
            {
                parent = target.Transform.GetParent();
                if (placement < 0) before = target.Id;
                else if (children.TryGetValue(parent, out List<Ens>? siblings))
                {
                    int index = siblings.FindIndex(ens => ens.Id.Equals(target.Id));
                    if (index >= 0 && index + 1 < siblings.Count) before = siblings[index + 1].Id;
                }
            }
        }
        Ens source = kind == 1 ? Ens.Find(key) : Ens.Null;
        bool valid = !EditorApplication.IsPlaying && (kind == 2
            ? key.EndsWith(".prefab", StringComparison.Ordinal)
            : source.IsValid && !source.Id.Equals(parent) && !source.Id.Equals(before));
        if (kind == 1)
        {
            for (Ens ancestor = Ens.FromId(parent); valid && ancestor.IsValid; ancestor = Ens.FromId(ancestor.Transform.GetParent()))
                if (ancestor.Id.Equals(source.Id)) valid = false;
        }
        if (!NativeEditorGUI.AcceptDrag(valid, placement)) return;
        pendingDrop = kind == 2 ? () => EditorPrefabActions.Instantiate(key, parent, before, false, default)
            : () => Move(source, parent, before);
    }

    //捕获节点的稳定层级位置和局部变换
    private static Position CapturePosition(Ens ens)
    {
        Transform transform = ens.Transform;
        EnsId parent = transform.GetParent();
        string before = string.Empty;
        bool found = false;
        foreach (EnsId id in EditorNativeComponents.GetWorldEns())
        {
            Ens sibling = Ens.FromId(id);
            if (!sibling.IsValid || !sibling.Transform.GetParent().Equals(parent)) continue;
            if (found) { before = sibling.ResourceKey; break; }
            if (id.Equals(ens.Id)) found = true;
        }
        return new Position(parent.IsNull ? string.Empty : Ens.FromId(parent).ResourceKey, before,
            transform.GetLocalPosition(), transform.GetLocalRotation(), transform.GetLocalScale());
    }

    //恢复层级顺序与变换并标记场景修改
    private static void RestorePosition(string key, Position position)
    {
        Ens ens = Ens.Find(key);
        Ens parent = position.Parent.Length == 0 ? Ens.Null : Ens.Find(position.Parent);
        Ens before = position.Before.Length == 0 ? Ens.Null : Ens.Find(position.Before);
        if (!ens.IsValid || (position.Parent.Length != 0 && !parent.IsValid)
            || (position.Before.Length != 0 && !before.IsValid)
            || !EditorNativeComponents.MoveEns(ens.Id, parent.Id, before.Id, false))
            throw new InvalidOperationException("Cannot restore Ens hierarchy position.");
        ens.Transform.SetLocalPosition(position.Translation);
        ens.Transform.SetLocalRotation(position.Rotation);
        ens.Transform.SetLocalScale(position.Scale);
    }

    //移动节点并记录一次撤销事务
    private static void Move(Ens ens, EnsId parent, EnsId before)
    {
        Position previous = CapturePosition(ens);
        if (!EditorNativeComponents.MoveEns(ens.Id, parent, before, true)) return;
        Position current = CapturePosition(ens);
        string key = ens.ResourceKey;
        EditorPropertyHistory.PushAction("Move Ens",
            () => RestorePosition(key, previous), () => RestorePosition(key, current));
    }
}
