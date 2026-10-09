using Orbeden;

namespace OrbedenEditor;

/// <summary>通过统一属性事务编辑原生与托管组件的 OrbEvent。</summary>
internal static class OrbEventEditor
{
    private sealed record FunctionChoice(string Label, string TypeName, OrbEventDomain Domain,
        int Occurrence, string Method, bool UseArguments);

    //绘制事件持久化调用列表
    internal static bool Draw(string label, PropertyValue property, out InteropValue value)
    {
        value = property.Value;
        if (!value.TryGet(out string text)) return false;
        OrbEvent eventValue;
        try { eventValue = OrbEvent.Parse(text); }
        catch (Exception exception) when (exception is FormatException or ArgumentException or OverflowException)
        { EditorGUI.TextWrapped($"{label}: invalid event data."); return false; }

        bool changed = false;
        EditorGUI.PushId(property.Name);
        EditorGUI.BeginDisabled(property.HasMultipleDifferentValues);
        try
        {
            EditorGUI.Label($"{label} ({eventValue.PersistentCalls.Count})");
            OrbEventCall? removed = null;
            for (int index = 0; index < eventValue.PersistentCalls.Count; ++index)
            {
                OrbEventCall call = eventValue.PersistentCalls[index];
                EditorGUI.PushId(index.ToString());
                try
                {
                    EditorGUI.Separator();
                    bool enabled = call.Enabled;
                    if (EditorGUI.Checkbox("Enabled", ref enabled)) { call.Enabled = enabled; changed = true; }
                    string key = call.TargetKey;
                    int objectId = 0;
                    if (EditorObjectField.Draw("Target", "Ens", ref key, ref objectId, ensHandle: true))
                    { call.TargetKey = key; changed = true; }
                    changed |= DrawFunction(eventValue, call);
                    if (EditorGUI.Button("Remove Call")) removed = call;
                    if (call.TargetKey.Length == 0) EditorGUI.TextWrapped("Choose a target node.");
                    else if (!Ens.Find(call.TargetKey).IsValid) EditorGUI.TextWrapped("Target node is missing.");
                    else if (!call.TryGetMethod(eventValue.ParameterKinds.ToArray(), out _))
                        EditorGUI.TextWrapped("The target method does not match the event signature.");
                }
                finally { EditorGUI.PopId(); }
            }
            if (removed != null) { eventValue.RemovePersistentCall(removed); changed = true; }
            if (EditorGUI.Button("Add Call"))
            { eventValue.AddPersistentCall(EnsId.Null, string.Empty, string.Empty); changed = true; }
        }
        finally { EditorGUI.EndDisabled(); EditorGUI.PopId(); }
        if (changed) value = InteropValue.From(eventValue.Serialize());
        return changed;
    }

    //选择语言域、组件实例和精确方法签名
    private static bool DrawFunction(OrbEvent eventValue, OrbEventCall call)
    {
        List<FunctionChoice> choices = CollectFunctions(eventValue, call.TargetKey);
        FunctionChoice? selected = choices.Find(choice => call.Domain == choice.Domain && call.TargetType == choice.TypeName
            && call.Occurrence == choice.Occurrence && call.Method == choice.Method && call.UseArguments == choice.UseArguments);
        string display = selected?.Label ?? (call.Method.Length == 0 ? "No Function"
            : $"[{(call.Domain == OrbEventDomain.Native ? "C++" : "C#")}] {call.TargetType}[{call.Occurrence}] / {call.Method} (Missing)");
        if (!EditorGUI.BeginCombo("Function", display)) return false;
        try
        {
            if (EditorGUI.Selectable("No Function", call.Method.Length == 0))
            {
                call.TargetType = string.Empty;
                call.Occurrence = 0;
                call.Method = string.Empty;
                call.UseArguments = false;
                return true;
            }
            foreach (FunctionChoice choice in choices)
            {
                if (!EditorGUI.Selectable(choice.Label, choice == selected)) continue;
                call.Domain = choice.Domain;
                call.TargetType = choice.TypeName;
                call.Occurrence = choice.Occurrence;
                call.Method = choice.Method;
                call.UseArguments = choice.UseArguments;
                return true;
            }
        }
        finally { EditorGUI.EndCombo(); }
        return false;
    }

    //枚举目标组件实例上可接收事件参数或无参调用的方法
    private static List<FunctionChoice> CollectFunctions(OrbEvent eventValue, string targetKey)
    {
        List<FunctionChoice> choices = [];
        Ens owner = Ens.Find(targetKey);
        if (!owner.IsValid) return choices;
        Dictionary<(OrbEventDomain, string), int> occurrences = [];
        foreach (Component component in owner.GetComponents<Component>())
        {
            Type type = component.GetType();
            bool managed = NativeBindingRuntime.IsManagedScript(type);
            string typeName = managed ? type.FullName ?? type.Name : NativeBindingRuntime.GetNativeTypeName(type) ?? type.Name;
            OrbEventDomain domain = managed ? OrbEventDomain.Managed : OrbEventDomain.Native;
            var key = (domain, typeName);
            int occurrence = occurrences.GetValueOrDefault(key);
            occurrences[key] = occurrence + 1;
            foreach (BindableMethod method in ComponentProxy.DescribeMethods(component))
            {
                if (method.ReturnKind != InteropValueKind.Empty) continue;
                bool passArguments = method.ParameterKinds.SequenceEqual(eventValue.ParameterKinds);
                if (!passArguments && method.ParameterKinds.Count != 0) continue;
                string signature = string.Join(", ", method.ParameterKinds);
                string label = $"[{(managed ? "C#" : "C++")}] {typeName}[{occurrence}] / {method.Name}({signature})";
                choices.Add(new(label, typeName, domain, occurrence, method.Name, passArguments));
            }
        }
        return choices;
    }
}
