using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using Orbeden;
using OrbedenEditor;

namespace OrbedenEditor;

/// <summary>
/// 控件编辑器：交互配置加一张事件表。事件表一行就是一个绑定：目标用引用框选，
/// 组件与方法合成一个 Function 选择器，改任何一项都记一条事务；无效引用与无效方法只标出来，不自动删除。
/// </summary>
[CustomEditor(typeof(UIControl), true)]
public sealed class UIControlEditor : ComponentEditor
{
    //遍历期间不能改列表：删除请求先记下来，遍历结束后统一处理。
    private int pendingRemove = -1;

    //行身份按绑定对象发，不按列表下标：选择器是模态弹窗，开着的时候 Ctrl+Z 依然生效，
    //下标身份会让弹窗落到另一条绑定上。对象没了，行号跟着一起回收。
    private static readonly ConditionalWeakTable<UIEventBinding, RowToken> rowTokens = new();
    private static int nextRowToken = 1;

    private sealed class RowToken
    {
        internal int Value;
    }

    //Function 选择器的候选项与搜索词。只存字符串：换世界或换程序集之后残留的选项不会指向失效对象。
    private static readonly List<FunctionChoice> functionChoices = [];
    private static string functionSearch = string.Empty;

    private sealed record FunctionChoice(string Group, string Text, string TypeName, string Method);

    /// <summary>绘制控件检视面板。</summary>
    public override void OnDrawInspector()
    {
        DrawProperty("enabled");
        DrawProperty("interactable");
        DrawProperty("targetVisual");
        if (Target.Ens.GetComponent<TextField>() is TextField field && field.GetFont() == null)
            EditorGUI.TextWrapped("Font is empty: using the built-in default font (Cubic 11, Bitmap).");
        if (Target.Ens.GetComponent<Button>() is Button button)
        {
            if (button.GetTargetVisual() == null)
                EditorGUI.TextWrapped("Assign a Target Visual or add an Image to this node for button hit testing and state colors.");
            else if (!button.GetTargetVisual()!.GetRaycastTarget())
                EditorGUI.TextWrapped("Target Visual has Raycast Target disabled; check the node's hit-test graphic.");
            EditorGUI.TextWrapped("State colors multiply the target graphic's tint. Clicked fires on release over the button or keyboard submit.");
        }

        EditorGUI.Label("Navigation (explicit references win)");
        DrawProperty("navigationUp");
        DrawProperty("navigationDown");
        DrawProperty("navigationLeft");
        DrawProperty("navigationRight");

        EditorGUI.Label("State Colors");
        DrawProperty("normalColor");
        DrawProperty("hoverColor");
        DrawProperty("pressedColor");
        DrawProperty("disabledColor");

        EditorGUI.BeginDisabled(Targets.Count != 1);
        DrawEventTable();
        EditorGUI.EndDisabled();
        if (Targets.Count != 1) EditorGUI.TextWrapped("Select one control to edit event bindings.");
        if (Target.Ens.GetComponent<Button>() == null) DrawDefaultInspector();
    }

    //绘制事件绑定卡片并记录修改历史
    private void DrawEventTable()
    {
        UIControl? control = Target.Ens.GetComponent<UIControl>();
        if (control == null) return;

        List<UIEventBinding> bindings = control.GetBindings();
        EditorGUI.Label($"Events ({bindings.Count})");

        for (int index = 0; index < bindings.Count; ++index) DrawRow(bindings, index);

        //删除统一在遍历之后处理：遍历中改列表会让下标失效。
        if (pendingRemove >= 0 && pendingRemove < bindings.Count)
        {
            int at = pendingRemove;
            pendingRemove = -1;
            UIEventBinding removed = bindings[at];
            bindings.RemoveAt(at);
            ApplyBindingChange("Remove UI Event",
                () => bindings.Insert(at, removed), () => bindings.Remove(removed));
        }

        if (!EditorGUI.Button("Add Event Binding")) return;
        UIEventBinding added = control.AddBinding();
        added.SetTarget(Target.EnsId);
        added.SetEventId(UIEventIds.Clicked);
        ApplyBindingChange("Add UI Event",
            () => bindings.Remove(added), () => bindings.Add(added));
    }

    private void DrawRow(List<UIEventBinding> bindings, int index)
    {
        UIEventBinding binding = bindings[index];
        EditorGUI.PushId(RowId(binding));
        try
        {
            EditorGUI.Separator();
            bool enabled = binding.IsEnabled();
            if (EditorGUI.Checkbox("Enabled", ref enabled))
            {
                bool previous = binding.IsEnabled();
                binding.SetEnabled(enabled);
                ApplyBindingChange("UI Event Enabled",
                    () => binding.SetEnabled(previous), () => binding.SetEnabled(enabled));
            }
            EditorGUI.Label("Event");
            DrawEventColumn(binding);
            DrawTargetRow(binding);
            DrawFunctionRow(binding);
            if (EditorGUI.Button("Remove")) pendingRemove = index;

            //无效引用与方法只提示，不自动删除：作者可能正要改目标。
            string? problem = DescribeProblem(binding);
            if (problem != null) EditorGUI.Label(problem);
        }
        finally
        {
            EditorGUI.PopId();
        }
    }

    //绑定对象到行号：同一帧里每一行都有稳定且互不相同的 ID，撤销改动列表时弹窗不会漂到别的行上。
    private static string RowId(UIEventBinding binding)
    {
        if (!rowTokens.TryGetValue(binding, out RowToken? token))
        {
            token = new RowToken { Value = nextRowToken++ };
            rowTokens.Add(binding, token);
        }
        return "ui_event_" + token.Value;
    }

    //事件列：下拉里列出全部已登记事件。
    private void DrawEventColumn(UIEventBinding binding)
    {
        if (!EditorGUI.BeginCombo("##event", UIEventIds.GetName(binding.GetEventId())))
        {
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

    //目标列：引用框选节点，支持层级拖拽、搜索选择器、清空与双击定位。
    private void DrawTargetRow(UIEventBinding binding)
    {
        EnsId id = binding.GetTarget();
        Ens owner = Ens.FromId(id);
        //悬空引用写成占位串，引用框显示成 Missing，而不是冒充"没设目标"。
        string key = owner.IsValid ? owner.ResourceKey : id.IsNull ? string.Empty : "Missing Ens " + id;
        int objectId = 0;
        if (!EditorObjectField.Draw("Target", "Ens", ref key, ref objectId, ensHandle: true)) return;
        CommitTarget(binding, key.Length == 0 ? EnsId.Null : Ens.Find(key).Id);
    }

    //功能列：一个字段显示"[C#] 脚本 / 方法"，点开是按脚本分组的选择器；组件类型随方法一起写定。
    private void DrawFunctionRow(UIEventBinding binding)
    {
        string method = binding.GetMethod();
        string text = method.Length == 0 ? "No Function" : DescribeFunction(binding, method);
        EditorGUI.Label("Function");
        EditorGUI.SameLine();
        //动作码：1 单击、3 省略号、4 双击（EditorGuiReferenceField）。
        int action = NativeEditorGUI.ReferenceField("CSharpScript", text, "function");
        switch (action)
        {
        case 2:
            CommitFunction(binding, binding.GetTargetType(), string.Empty);
            break;
        case 4:
            Ens owner = Ens.FromId(binding.GetTarget());
            if (owner.IsValid) EnsPanel.Ping(owner.ResourceKey);
            break;
        case 1:
        case 3:
            CollectFunctionChoices(binding);
            functionSearch = string.Empty;
            NativeEditorGUI.OpenPopup("ui_function_picker");
            break;
        }

        if (!NativeEditorGUI.BeginPopup("ui_function_picker")) return;
        try
        {
            EditorGUI.InputText("Search##ui_function_search", ref functionSearch);
            //把事件带的载荷写出来：列表里只剩下能接住它的方法，作者得知道接的是哪一种。
            InteropValueKind payload = UIEventIds.GetPayloadKind(binding.GetEventId());
            EditorGUI.Label($"{UIEventIds.GetName(binding.GetEventId())} payload: {PayloadName(payload)}");
            float width = 440;
            bool visible = NativeEditorGUI.BeginChild("##ui_function_choices", ref width, 300);
            try
            {
                if (visible) DrawFunctionChoices(binding);
            }
            finally { NativeEditorGUI.EndChild(); }
            if (EditorGUI.Button("Cancel##ui_function_cancel")) NativeEditorGUI.ClosePopup();
        }
        finally { EditorGUI.EndPopup(); }
    }

    //选择器内容：先 (None)，再按脚本分组列可绑定方法；重名方法与同名脚本都靠 ## 后缀区分。
    private static void DrawFunctionChoices(UIEventBinding binding)
    {
        if (EditorGUI.Selectable("(None)##ui_function_none", binding.GetMethod().Length == 0))
        {
            CommitFunction(binding, binding.GetTargetType(), string.Empty);
            NativeEditorGUI.ClosePopup();
            return;
        }

        string printedGroup = string.Empty;
        foreach (FunctionChoice choice in functionChoices)
        {
            if (!MatchesSearch(choice)) continue;
            //组头跟着第一条可见项出现，搜索滤空的分组不留空标题。
            if (choice.Group != printedGroup)
            {
                printedGroup = choice.Group;
                EditorGUI.Label(printedGroup);
            }
            bool selected = binding.GetMethod() == choice.Method && binding.GetTargetType() == choice.TypeName;
            if (!EditorGUI.Selectable($"{choice.Text}##{choice.TypeName}.{choice.Method}", selected)) continue;
            CommitFunction(binding, choice.TypeName, choice.Method);
            NativeEditorGUI.ClosePopup();
            return;
        }
    }

    private static bool MatchesSearch(FunctionChoice choice) =>
        choice.Text.Contains(functionSearch, StringComparison.OrdinalIgnoreCase);

    //按目标节点重建候选：只收托管脚本，且方法的参数种类必须与事件载荷一致。
    //C++ 脚本没有托管宿主句柄，ComponentProxy.FromComponent 返回空（ComponentProxy.cs），列出来运行期也调不动。
    private static void CollectFunctionChoices(UIEventBinding binding)
    {
        functionChoices.Clear();
        Ens owner = Ens.FromId(binding.GetTarget());
        if (!owner.IsValid) return;
        InteropValueKind payload = UIEventIds.GetPayloadKind(binding.GetEventId());

        List<string> visitedTypes = [];
        foreach (Script script in owner.GetComponents<Script>())
        {
            if (script == null || !NativeBindingRuntime.IsManagedScript(script.GetType())) continue;
            string typeName = script.GetType().FullName ?? string.Empty;
            if (typeName.Length == 0 || visitedTypes.Contains(typeName)) continue;
            visitedTypes.Add(typeName);

            string group = "[C#] " + ShortName(typeName);
            List<string> visitedMethods = [];
            foreach (BindableMethod candidate in ComponentProxy.DescribeMethods(script))
            {
                //重名只留一个，而且是运行期真的会调到的那个：DescribeMethods 按名字与参数个数排序，
                //第一个签名合格的正是运行期取的那个；它接的参数与载荷对不上，整个名字都不列。
                if (!IsBindableMethod(candidate) || visitedMethods.Contains(candidate.Name)) continue;
                visitedMethods.Add(candidate.Name);
                if (!MatchesPayload(candidate, payload)) continue;
                functionChoices.Add(new FunctionChoice(group, $"{group} / {Signature(candidate)}", typeName, candidate.Name));
            }
        }
    }

    //无参方法任何事件都能接；带参数的必须与事件载荷同种类，否则调用时只会拿到默认值。
    private static bool MatchesPayload(BindableMethod method, InteropValueKind payload) =>
        method.ParameterKinds.Count == 0 || method.ParameterKinds[0] == payload;

    //方法签名在界面上的写法：带参数的把种类写出来，作者得知道要接的是哪一种。
    private static string Signature(BindableMethod method) =>
        method.ParameterKinds.Count == 0 ? method.Name : $"{method.Name}({method.ParameterKinds[0]})";

    //字段显示名与运行期解析一致：按类型全名匹配脚本，再按名字取第一个签名合格的重载。
    private static string DescribeFunction(UIEventBinding binding, string method)
    {
        if (TryFindBoundMethod(binding, out Script? script, out BindableMethod found))
            return $"[C#] {ShortName(script!.GetType().FullName ?? string.Empty)} / {Signature(found)}";
        string typeName = binding.GetTargetType();
        string fallback = typeName.Length == 0 ? method : $"[C#] {ShortName(typeName)} / {method}";
        return fallback + " (missing)";
    }

    //取运行期会选中的那个方法：先按类型全名匹配脚本，再按名字取第一个签名合格的重载。
    private static bool TryFindBoundMethod(UIEventBinding binding, out Script? script, out BindableMethod method)
    {
        script = null;
        method = default;
        string name = binding.GetMethod();
        if (name.Length == 0) return false;

        string typeName = binding.GetTargetType();
        Ens owner = Ens.FromId(binding.GetTarget());
        if (!owner.IsValid) return false;
        foreach (Script candidate in owner.GetComponents<Script>())
        {
            if (candidate == null) continue;
            if (typeName.Length != 0 && (candidate.GetType().FullName ?? string.Empty) != typeName) continue;
            foreach (BindableMethod match in ComponentProxy.DescribeMethods(candidate))
            {
                if (match.Name != name || !IsBindableMethod(match)) continue;
                script = candidate;
                method = match;
                return true;
            }
        }
        return false;
    }

    private static string PayloadName(InteropValueKind kind) =>
        kind == InteropValueKind.Empty ? "none" : kind.ToString();

    //能当事件回调的方法：返回空、参数不超过一个且是载荷支持的种类。
    private static bool IsBindableMethod(BindableMethod method)
    {
        if (method.ReturnKind != InteropValueKind.Empty) return false;
        if (method.ParameterKinds.Count > 1) return false;
        return method.ParameterKinds.Count == 0 || IsSupportedParameter(method.ParameterKinds[0]);
    }

    private static bool IsSupportedParameter(InteropValueKind kind) =>
        kind is InteropValueKind.Bool or InteropValueKind.Float32 or InteropValueKind.Int32
            or InteropValueKind.Vector2 or InteropValueKind.String;

    //取类型名去掉命名空间后的短名；嵌套类型用 + 分隔，也要切掉。
    private static string ShortName(string typeName)
    {
        int separator = Math.Max(typeName.LastIndexOf('.'), typeName.LastIndexOf('+'));
        return separator >= 0 ? typeName[(separator + 1)..] : typeName;
    }

    //整份 id 加 version 一起写：World::GetEns 校验版本号，只改 id 的目标永远解析不到。
    private static void CommitTarget(UIEventBinding binding, EnsId next)
    {
        EnsId previous = binding.GetTarget();
        if (previous.id == next.id && previous.version == next.version) return;
        binding.SetTarget(next);
        ApplyBindingChange("UI Event Target",
            () => binding.SetTarget(previous), () => binding.SetTarget(next));
    }

    //组件类型与方法是一次选择的两个部分，合成同一条撤销。
    private static void CommitFunction(UIEventBinding binding, string typeName, string method)
    {
        string previousType = binding.GetTargetType();
        string previousMethod = binding.GetMethod();
        if (previousType == typeName && previousMethod == method) return;
        binding.SetTargetType(typeName);
        binding.SetMethod(method);
        ApplyBindingChange("UI Event Function",
            () => { binding.SetTargetType(previousType); binding.SetMethod(previousMethod); },
            () => { binding.SetTargetType(typeName); binding.SetMethod(method); });
    }

    //事件标识的改动同样记一条撤销。
    private static void CommitEventId(UIEventBinding binding, int next)
    {
        int previous = binding.GetEventId();
        if (previous == next) return;
        binding.SetEventId(next);
        ApplyBindingChange("UI Event",
            () => binding.SetEventId(previous), () => binding.SetEventId(next));
    }

    //绑定的改动不进属性文档，所以脏标记得自己补：不然保存提示不亮。
    private static void ApplyBindingChange(string label, Action undo, Action redo)
    {
        EditorApplication.MarkWorldDirty();
        EditorPropertyHistory.RecordAction(label,
            () => { undo(); EditorApplication.MarkWorldDirty(); },
            () => { redo(); EditorApplication.MarkWorldDirty(); });
    }

    //无效引用与方法：只报出来，交给作者决定。目标为空也报——运行期拿不到目标就解析不了。
    private static string? DescribeProblem(UIEventBinding binding)
    {
        EnsId target = binding.GetTarget();
        if (target.IsNull) return "No target node set.";
        if (!Ens.FromId(target).IsValid) return "Target node no longer exists.";
        if (binding.GetMethod().Length == 0) return "No function set.";
        if (!binding.IsResolvable()) return "No method on the target matches the signature.";
        return DescribeParameterMismatch(binding);
    }

    //参数种类必须与事件载荷一致：不一致时那个参数只会拿到默认值，等于绑错了。换事件后也会在这里报出来。
    private static string? DescribeParameterMismatch(UIEventBinding binding)
    {
        if (!TryFindBoundMethod(binding, out _, out BindableMethod method) || method.ParameterKinds.Count == 0) return null;
        InteropValueKind parameter = method.ParameterKinds[0];
        InteropValueKind payload = UIEventIds.GetPayloadKind(binding.GetEventId());
        if (parameter == payload) return null;
        return $"Function takes {parameter}, but {UIEventIds.GetName(binding.GetEventId())} carries {PayloadName(payload)}.";
    }
}
