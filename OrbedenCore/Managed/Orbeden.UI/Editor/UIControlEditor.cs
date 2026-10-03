using System;
using System.Collections.Generic;
using Orbeden;
using OrbedenEditor;

namespace OrbedenEditor;

/// <summary>
/// 控件编辑器：交互配置加一张事件表。事件表一行就是一个绑定，
/// 四个字段改动一起提交为一条事务；无效引用与无效方法只标出来，不自动删除。
/// </summary>
[CustomEditor(typeof(UIControl), true)]
public sealed class UIControlEditor : ComponentEditor
{
    //事件表的列：事件、目标、目标组件、方法，外加一列删除。
    private const int ColumnCount = 4;

    //遍历期间不能改列表：删除请求先记下来，遍历结束后统一处理。
    private static int pendingRemove = -1;
    //每行的编辑缓冲；按下回车或失焦时提交。
    private readonly Dictionary<int, (string Target, string Component, string Method)> rowEdits = [];
    private int comboRow = -1;

    /// <summary>绘制控件检视面板。</summary>
    public override void OnDrawInspector()
    {
        DrawProperty("interactable");
        DrawProperty("targetVisual");

        EditorGUI.Label("导航（显式引用优先）");
        DrawProperty("navigationUp");
        DrawProperty("navigationDown");
        DrawProperty("navigationLeft");
        DrawProperty("navigationRight");

        EditorGUI.Label("状态色");
        DrawProperty("normalColor");
        DrawProperty("hoverColor");
        DrawProperty("pressedColor");
        DrawProperty("disabledColor");

        DrawEventTable();
        DrawDefaultInspector();
    }

    //事件表：每行四列，改动按行提交，一次改动记一条撤销。
    private void DrawEventTable()
    {
        UIControl? control = Target.Ens.GetComponent<UIControl>();
        if (control == null) return;

        List<UIEventBinding> bindings = control.GetBindings();
        EditorGUI.Label($"事件表（{bindings.Count}）");

        for (int index = 0; index < bindings.Count; ++index) DrawRow(bindings, index);

        //删除统一在遍历之后处理：遍历中改列表会让下标失效。
        if (pendingRemove >= 0 && pendingRemove < bindings.Count)
        {
            int at = pendingRemove;
            pendingRemove = -1;
            UIEventBinding removed = bindings[at];
            bindings.RemoveAt(at);
            EditorPropertyHistory.RecordAction("Remove UI Event",
                () => bindings.Insert(at, removed), () => bindings.Remove(removed));
        }

        //关闭事件下拉：下拉在绘制期间挂着，下一次点击才算选中。
        if (comboRow >= 0)
        {
            EditorGUI.EndCombo();
            comboRow = -1;
        }

        if (!EditorGUI.Button("添加事件绑定")) return;
        UIEventBinding added = control.AddBinding();
        added.SetTarget(Target.EnsId);
        added.SetEventId(UIEventIds.Clicked);
        EditorPropertyHistory.RecordAction("Add UI Event",
            () => bindings.Remove(added), () => bindings.Add(added));
    }

    private void DrawRow(List<UIEventBinding> bindings, int index)
    {
        UIEventBinding binding = bindings[index];
        EditorGUI.PushId($"ui_event_{index}");
        try
        {
            DrawEventColumn(binding, index);
            EditorGUI.SameLine();
            DrawTargetColumn(binding, index);
            EditorGUI.SameLine();
            DrawComponentColumn(binding, index);
            EditorGUI.SameLine();
            DrawMethodColumn(binding, index);
            EditorGUI.SameLine();
            if (EditorGUI.Button("删除")) pendingRemove = index;

            //无效引用与方法只提示，不自动删除：作者可能正要改目标。
            string? problem = DescribeProblem(binding);
            if (problem != null) EditorGUI.Label(problem);
        }
        finally
        {
            EditorGUI.PopId();
        }
    }

    //事件列：下拉里列出全部已登记事件。
    private void DrawEventColumn(UIEventBinding binding, int index)
    {
        if (!EditorGUI.BeginCombo($"##event{index}", UIEventIds.GetName(binding.GetEventId())))
        {
            EditorGUI.EndCombo();
            return;
        }

        for (int candidate = 0; candidate < UIEventIds.Count; ++candidate)
        {
            int eventId = UIEventIds.IdAt(candidate);
            if (!EditorGUI.Selectable(UIEventIds.GetName(eventId), eventId == binding.GetEventId())) continue;
            CommitEventId(binding, eventId);
        }
        EditorGUI.EndCombo();
    }

    //目标列：填目标 Ens 的稳定 ID；留空表示与控件同节点。
    private void DrawTargetColumn(UIEventBinding binding, int index)
    {
        (string target, string component, string method) = RowEdit(index, binding);
        if (EditorGUI.InputText($"##target{index}", ref target, 120.0f))
        {
            rowEdits[index] = (target, component, method);
            if (int.TryParse(target, out int parsed)) CommitTarget(binding, parsed);
        }
    }

    //组件列：目标组件在托管侧的类型全名；留空表示在全部脚本里找。
    private void DrawComponentColumn(UIEventBinding binding, int index)
    {
        (string target, string component, string method) = RowEdit(index, binding);
        if (!EditorGUI.InputText($"##component{index}", ref component, 160.0f)) return;
        rowEdits[index] = (target, component, method);
        string previous = binding.GetTargetType();
        if (previous == component) return;
        binding.SetTargetType(component);
        EditorPropertyHistory.RecordAction("UI Event Component",
            () => binding.SetTargetType(previous), () => binding.SetTargetType(component));
    }

    //方法列：目标能解析时给下拉，列出签名匹配的方法；解析不出来退回文本框手填。
    private void DrawMethodColumn(UIEventBinding binding, int index)
    {
        (string target, string component, string method) = RowEdit(index, binding);
        if (TryCollectMethods(binding, out List<BindableMethod>? candidates))
        {
            //下拉里只放能直接当事件回调的方法：返回空、参数不超过一个且是载荷支持的种类。
            if (!EditorGUI.BeginCombo($"##method{index}", method.Length == 0 ? "(未设置)" : method))
            {
                EditorGUI.EndCombo();
                return;
            }
            foreach (BindableMethod candidate in candidates!)
            {
                if (!EditorGUI.Selectable(Describe(candidate), candidate.Name == method)) continue;
                CommitMethod(binding, index, target, component, candidate.Name);
            }
            EditorGUI.EndCombo();
            return;
        }

        if (!EditorGUI.InputText($"##method{index}", ref method, 140.0f)) return;
        rowEdits[index] = (target, component, method);
        CommitMethod(binding, index, target, component, method);
    }

    //把绑定的目标解析成脚本实例，再按元数据列出可绑定的方法。
    private static bool TryCollectMethods(UIEventBinding binding, out List<BindableMethod>? methods)
    {
        methods = null;
        EnsId target = binding.GetTarget();
        Ens owner = target.IsNull ? Ens.Null : Ens.FromId(target);
        if (!owner.IsValid) return false;

        string typeName = binding.GetTargetType();
        foreach (Script script in owner.GetComponents<Script>())
        {
            if (script == null) continue;
            if (typeName.Length != 0 && !string.Equals(script.GetType().FullName, typeName, StringComparison.Ordinal)) continue;
            methods = [];
            foreach (BindableMethod candidate in ComponentProxy.DescribeMethods(script))
            {
                if (candidate.ReturnKind != InteropValueKind.Empty) continue;
                if (candidate.ParameterKinds.Count > 1) continue;
                if (candidate.ParameterKinds.Count == 1 && !IsSupportedParameter(candidate.ParameterKinds[0])) continue;
                methods.Add(candidate);
            }
            return true;
        }
        return false;
    }

    //下拉里的显示名：带参数的方法把参数种类写出来，重载就不会看起来一模一样。
    private static string Describe(BindableMethod method) =>
        method.ParameterKinds.Count == 0 ? method.Name : $"{method.Name}({method.ParameterKinds[0]})";

    private static bool IsSupportedParameter(InteropValueKind kind) =>
        kind is InteropValueKind.Bool or InteropValueKind.Float32 or InteropValueKind.Int32
            or InteropValueKind.Vector2 or InteropValueKind.String;

    //方法名改动记一条撤销；下拉与文本框两条路径共用。
    private void CommitMethod(UIEventBinding binding, int index, string target, string component, string method)
    {
        rowEdits[index] = (target, component, method);
        string previous = binding.GetMethod();
        if (previous == method) return;
        binding.SetMethod(method);
        EditorPropertyHistory.RecordAction("UI Event Method",
            () => binding.SetMethod(previous), () => binding.SetMethod(method));
    }

    //某一行的编辑缓冲；第一次取用时用绑定当前值填充。
    private (string Target, string Component, string Method) RowEdit(int index, UIEventBinding binding)
    {
        if (rowEdits.TryGetValue(index, out (string Target, string Component, string Method) existing))
        {
            return existing;
        }
        (string, string, string) created = (
            binding.GetTarget().IsNull ? string.Empty : binding.GetTarget().id.ToString(),
            binding.GetTargetType(),
            binding.GetMethod());
        rowEdits[index] = created;
        return created;
    }

    private void CommitTarget(UIEventBinding binding, int targetId)
    {
        EnsId previous = binding.GetTarget();
        EnsId next = previous;
        next.id = unchecked((uint)targetId);
        if (previous.id == next.id) return;
        binding.SetTarget(next);
        EditorPropertyHistory.RecordAction("UI Event Target",
            () => binding.SetTarget(previous), () => binding.SetTarget(next));
    }

    //无效引用与方法：只报出来，交给作者决定。
    private string? DescribeProblem(UIEventBinding binding)
    {
        if (binding.GetMethod().Length == 0) return "未设置方法。";
        EnsId target = binding.GetTarget();
        if (target.IsNull) return null;
        if (!Ens.FromId(target).IsValid) return "目标节点已不存在。";
        return binding.IsResolvable() ? null : "目标上找不到签名匹配的方法。";
    }

    //事件标识的改动同样记一条撤销。
    private static void CommitEventId(UIEventBinding binding, int next)
    {
        int previous = binding.GetEventId();
        if (previous == next) return;
        binding.SetEventId(next);
        EditorPropertyHistory.RecordAction("UI Event",
            () => binding.SetEventId(previous), () => binding.SetEventId(next));
    }
}
