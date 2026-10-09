using System;


namespace Orbeden;

/// <summary>
/// 一条持久化的事件绑定：保存"目标对象上的哪个方法"，派发时用精确签名匹配代理。
/// 方法名加参数类型必须完全对上，不做隐式转换；解析失败只停用这一条并报错，
/// 不影响同一控件上的其它绑定。
/// </summary>
public sealed class UIEventBinding
{
    //监听哪个事件，取 UIEventIds。
    [SerializeField] private int eventId;

    //目标对象所在的 Ens。
    [SerializeField] private EnsId target;

    //目标组件在托管侧的类型全名；空表示在目标的全部脚本里找。
    [SerializeField] private string targetType = string.Empty;

    //目标方法名。
    [SerializeField] private string method = string.Empty;

    //绑定是否启用；解析失败后会被置为 false。
    [SerializeField] private bool enabled = true;

    //解析结果：接收者包装用于存活检查，代理与已解析方法用于调用；
    //参数种类记下来，派发时按它把载荷装成 InteropValue。
    private Script? receiver;
    private ComponentProxy? proxy;
    private ComponentMethod compiled;
    private bool compiledValid;
    private InteropValueKind parameterKind = InteropValueKind.Empty;
    private bool failed;

    /// <summary>监听的事件标识。</summary>
    public int GetEventId() => eventId;

    /// <summary>设置监听的事件标识。</summary>
    public void SetEventId(int value) => eventId = value;

    /// <summary>目标所在 Ens。</summary>
    public EnsId GetTarget() => target;

    /// <summary>设置目标 Ens；换目标会让已解析的代理失效。</summary>
    public void SetTarget(EnsId value)
    {
        if (target.id == value.id && target.version == value.version) return;
        target = value;
        ResetResolved();
        failed = false;
    }

    /// <summary>目标组件的类型全名；空表示在目标的全部脚本里找。</summary>
    public string GetTargetType() => targetType;

    /// <summary>设置目标组件类型名。</summary>
    public void SetTargetType(string value)
    {
        string next = value ?? string.Empty;
        if (targetType == next) return;
        targetType = next;
        ResetResolved();
        failed = false;
    }

    /// <summary>目标方法名。</summary>
    public string GetMethod() => method;

    /// <summary>设置目标方法名；改名字会让已解析的代理失效。</summary>
    public void SetMethod(string value)
    {
        string next = value ?? string.Empty;
        if (method == next) return;
        method = next;
        ResetResolved();
        failed = false;
    }

    /// <summary>是否启用。</summary>
    public bool IsEnabled() => enabled;

    /// <summary>启用或停用这条绑定。</summary>
    public void SetEnabled(bool value)
    {
        if (enabled == value) return;
        enabled = value;
        if (!enabled) ResetResolved();
    }

    /// <summary>
    /// 目标与方法当前能否解析。只做只读检查，不改变绑定状态——
    /// 检视面板拿它提示"找不到方法"，不能因为看一眼就把绑定停用掉。
    /// </summary>
    public bool IsResolvable()
    {
        if (!enabled || string.IsNullOrEmpty(method)) return false;
        if (compiledValid && receiver != null && receiver.IsAlive) return true;
        return TryResolveReceiver(out _, out _);
    }

    /// <summary>按目标 Ens 解析接收者与方法；失败时停用这一条并报一次错。</summary>
    public bool Compile()
    {
        if (!enabled || failed) return compiledValid;
        if (string.IsNullOrEmpty(method))
        {
            failed = true;
            Console.Error.WriteLine("UIEventBinding: 绑定缺少方法名。");
            return false;
        }

        if (compiledValid && receiver != null)
        {
            //接收者还活着就沿用已解析的方法句柄。
            if (receiver.IsAlive) return true;
            ResetResolved();
        }

        if (!TryResolveReceiver(out Script? found, out BindableMethod? signature) || found == null || signature == null)
        {
            failed = true;
            Console.Error.WriteLine($"UIEventBinding: 目标上没有找到类型为 '{targetType}' 的组件，或没有签名匹配的方法 '{method}'。");
            return false;
        }

        //签名已经由元数据核对过，这里只是拿一个可复用的方法句柄。
        ComponentProxy? found2 = ComponentProxy.FromComponent(found);
        if (found2 == null
            || found2.TryResolveMethod(method, signature.ParameterKinds.ToArray(), out ComponentMethod resolved) != InteropStatus.Ok)
        {
            failed = true;
            Console.Error.WriteLine($"UIEventBinding: 无法为 '{method}' 建立方法句柄。");
            return false;
        }

        receiver = found;
        proxy = found2;
        compiled = resolved;
        compiledValid = true;
        parameterKind = signature.ParameterKinds.Count == 0 ? InteropValueKind.Empty : signature.ParameterKinds[0];
        failed = false;
        return true;
    }

    /// <summary>用一条事件调用绑定的方法；未解析成功或执行失败时返回假。</summary>
    public bool Invoke(in UIEvent entry)
    {
        if (!enabled || !compiledValid || proxy == null) return false;

        try
        {
            InteropStatus status = parameterKind == InteropValueKind.Empty
                ? compiled.Invoke([], out _)
                : compiled.Invoke([ConvertParameter(parameterKind, entry)], out _);
            //调用侧的失败也要报出来：只返回假的话，绑不上和调不动在面板上长得一模一样。
            if (status != InteropStatus.Ok)
                Console.Error.WriteLine($"UIEventBinding: 调用 '{method}' 失败（{status}）。");
            return status == InteropStatus.Ok;
        }
        catch (Exception exception)
        {
            //单个绑定的异常不阻断其它绑定。
            Console.Error.WriteLine($"UIEventBinding: 调用 '{method}' 失败。{exception}");
            return false;
        }
    }

    //清掉已解析的方法句柄，但保留失败标记之外的状态。
    private void ResetResolved()
    {
        receiver = null;
        proxy = null;
        compiled = default;
        compiledValid = false;
        parameterKind = InteropValueKind.Empty;
    }

    //在目标 Ens 上找托管组件：指定了类型名就按全名精确找，否则在脚本里找带该方法的第一个。
    //签名一律由类型元数据判定，运行期不做反射扫描。
    private bool TryResolveReceiver(out Script? found, out BindableMethod? signature)
    {
        found = null;
        signature = null;
        Ens owner = Ens.FromId(target);
        if (!owner.IsValid) return false;

        foreach (Script script in owner.GetComponents<Script>())
        {
            if (script == null) continue;
            if (targetType.Length != 0
                && !string.Equals(script.GetType().FullName, targetType, StringComparison.Ordinal)) continue;
            if (!TryMatchSignature(script, out BindableMethod? match)) continue;
            found = script;
            signature = match;
            return true;
        }
        return false;
    }

    //精确匹配：名字对上、返回为空（void）、参数不超过一个且种类是载荷支持的那几种。
    private bool TryMatchSignature(Script script, out BindableMethod? match)
    {
        match = null;
        foreach (BindableMethod candidate in ComponentProxy.DescribeMethods(script))
        {
            if (candidate.Name != method || candidate.ReturnKind != InteropValueKind.Empty) continue;
            if (candidate.ParameterKinds.Count > 1) continue;
            if (candidate.ParameterKinds.Count == 1 && !IsSupportedParameter(candidate.ParameterKinds[0])) continue;
            match = candidate;
            return true;
        }
        return false;
    }

    //把事件载荷按已解析的参数种类装成 InteropValue；种类在 Compile 时已经核对过。
    private static InteropValue ConvertParameter(InteropValueKind kind, in UIEvent entry) => kind switch
    {
        InteropValueKind.Bool => InteropValue.From(entry.payload.flagged),
        InteropValueKind.Float32 => InteropValue.From(entry.payload.value),
        InteropValueKind.Int32 => InteropValue.From(entry.payload.index),
        InteropValueKind.Vector2 => InteropValue.From(entry.payload.point),
        InteropValueKind.String => InteropValue.From(entry.payload.text ?? string.Empty),
        _ => default,
    };

    private static bool IsSupportedParameter(InteropValueKind kind) =>
        kind is InteropValueKind.Bool or InteropValueKind.Float32 or InteropValueKind.Int32
            or InteropValueKind.Vector2 or InteropValueKind.String;
}
