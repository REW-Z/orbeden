using System.Globalization;

namespace Orbeden;

/// <summary>持久化监听器所在的语言域。</summary>
public enum OrbEventDomain { Native = 1, Managed = 2 }

/// <summary>接收事件参数的运行时监听器；参数仅在调用期间有效。</summary>
public delegate void OrbEventCallback(ReadOnlySpan<InteropValue> arguments);

/// <summary>事件的持久化调用配置；目标键在场景复制时随对象引用重映射。</summary>
public sealed class OrbEventCall
{
    public string TargetKey { get; set; } = string.Empty;
    public OrbEventDomain Domain { get; set; } = OrbEventDomain.Managed;
    public string TargetType { get; set; } = string.Empty;
    public int Occurrence { get; set; }
    public string Method { get; set; } = string.Empty;
    public bool Enabled { get; set; } = true;
    public bool UseArguments { get; set; } = true;

    //复制调用配置
    internal OrbEventCall Copy() => (OrbEventCall)MemberwiseClone();

    /// <summary>按语言域和精确参数签名查找目标方法。</summary>
    public bool TryGetMethod(ReadOnlySpan<InteropValueKind> signature, out ComponentMethod method)
    {
        method = default;
        if (Occurrence < 0 || string.IsNullOrEmpty(TargetType) || string.IsNullOrEmpty(Method)) return false;
        Ens owner = Ens.Find(TargetKey);
        if (!owner.IsValid) return false;
        ComponentProxy? proxy = Domain switch
        {
            OrbEventDomain.Native => owner.GetNativeComponent(TargetType, Occurrence),
            OrbEventDomain.Managed => owner.GetManagedComponent(TargetType, Occurrence),
            _ => null,
        };
        return proxy != null && proxy.TryResolveMethod(Method, UseArguments ? signature : [], out method) == InteropStatus.Ok;
    }
}

/// <summary>可序列化的同步多播事件。持久化监听器先执行，运行时监听器后执行；调用开始时快照。</summary>
public class OrbEvent
{
    private readonly InteropValueKind[] parameterKinds;
    private readonly List<OrbEventCall> persistentCalls = [];
    private readonly List<OrbEventCallback> listeners = [];
    private readonly Dictionary<Delegate, OrbEventCallback> typedListeners = [];
    private int invocationDepth;

    /// <summary>创建无参事件。</summary>
    public OrbEvent() : this([]) { }

    /// <summary>创建具有精确跨语言参数签名的事件。</summary>
    public OrbEvent(params InteropValueKind[] signature)
    {
        ArgumentNullException.ThrowIfNull(signature);
        if (signature.Any(kind => !Enum.IsDefined(kind) || kind is InteropValueKind.Empty or InteropValueKind.Array))
            throw new ArgumentException("Unsupported event parameter kind.", nameof(signature));
        parameterKinds = (InteropValueKind[])signature.Clone();
    }

    public IReadOnlyList<InteropValueKind> ParameterKinds => Array.AsReadOnly(parameterKinds);
    public IReadOnlyList<OrbEventCall> PersistentCalls => persistentCalls.AsReadOnly();

    /// <summary>添加持久化监听器；UseArguments 为假时调用无参方法。</summary>
    public OrbEventCall AddPersistentCall(EnsId target, string targetType, string method,
        OrbEventDomain domain = OrbEventDomain.Managed, int occurrence = 0, bool useArguments = true)
    {
        if (occurrence < 0) throw new ArgumentOutOfRangeException(nameof(occurrence));
        OrbEventCall listener = new()
        {
            TargetKey = target.IsNull ? string.Empty : Ens.FromId(target).ResourceKey,
            TargetType = targetType, Method = method, Domain = domain,
            Occurrence = occurrence, UseArguments = useArguments,
        };
        persistentCalls.Add(listener);
        return listener;
    }

    /// <summary>移除指定持久化监听器。</summary>
    public bool RemovePersistentCall(OrbEventCall listener) => persistentCalls.Remove(listener);

    /// <summary>添加运行时监听器，重复注册无副作用。</summary>
    public void Subscribe(OrbEventCallback listener)
    {
        ArgumentNullException.ThrowIfNull(listener);
        if (!listeners.Contains(listener)) listeners.Add(listener);
    }

    /// <summary>移除运行时监听器。</summary>
    public void Unsubscribe(OrbEventCallback listener) => listeners.Remove(listener);

    /// <summary>添加无参运行时监听器。</summary>
    public void Subscribe(Action listener) => SubscribeTyped(listener, _ => listener());

    /// <summary>移除无参运行时监听器。</summary>
    public void Unsubscribe(Action listener) => UnsubscribeTyped(listener);

    /// <summary>清空运行时监听器，保留持久化配置。</summary>
    public void ClearSubscriptions() { listeners.Clear(); typedListeners.Clear(); }

    //登记类型化监听器
    protected void SubscribeTyped(Delegate listener, OrbEventCallback callback)
    {
        ArgumentNullException.ThrowIfNull(listener);
        if (!typedListeners.TryAdd(listener, callback)) return;
        listeners.Add(callback);
    }

    //移除类型化监听器
    protected void UnsubscribeTyped(Delegate listener)
    {
        if (typedListeners.Remove(listener, out OrbEventCallback? callback)) listeners.Remove(callback);
    }

    /// <summary>同步派发事件，隔离各监听器异常并限制递归深度。</summary>
    public void Dispatch(params InteropValue[] arguments) => Dispatch(arguments.AsSpan());

    /// <summary>同步派发具有精确参数签名的事件。</summary>
    public void Dispatch(ReadOnlySpan<InteropValue> arguments)
    {
        if (arguments.Length != parameterKinds.Length) throw new ArgumentException("Event argument count mismatch.", nameof(arguments));
        for (int index = 0; index < arguments.Length; ++index)
            if (arguments[index].Kind != parameterKinds[index]) throw new ArgumentException("Event argument kind mismatch.", nameof(arguments));
        if (invocationDepth >= 32) throw new InvalidOperationException("OrbEvent invocation depth exceeded.");

        //快照全部监听器
        OrbEventCall[] persistent = persistentCalls.Select(listener => listener.Copy()).ToArray();
        OrbEventCallback[] runtime = listeners.ToArray();
        ++invocationDepth;
        try
        {
            foreach (OrbEventCall listener in persistent)
            {
                if (!listener.Enabled) continue;
                try
                {
                    if (!listener.TryGetMethod(parameterKinds, out ComponentMethod method))
                    { Console.Error.WriteLine($"OrbEvent: missing listener {listener.TargetType}.{listener.Method}."); continue; }
                    InteropStatus status = method.Invoke(listener.UseArguments ? arguments : [], out _);
                    if (status != InteropStatus.Ok) Console.Error.WriteLine($"OrbEvent: {listener.Method}: {status}.");
                }
                catch (Exception exception) { Console.Error.WriteLine($"OrbEvent: {exception}"); }
            }
            foreach (OrbEventCallback listener in runtime)
            {
                try { listener(arguments); }
                catch (Exception exception) { Console.Error.WriteLine($"OrbEvent: {exception}"); }
            }
        }
        finally { --invocationDepth; }
    }

    /// <summary>序列化签名和持久化监听器，运行时代理不写入场景。</summary>
    public string Serialize()
    {
        List<string> entries = ["1", FormatValues(parameterKinds.Select(kind => ((uint)kind).ToString(CultureInfo.InvariantCulture)))];
        foreach (OrbEventCall listener in persistentCalls)
            entries.Add(FormatValues([listener.TargetKey, ((int)listener.Domain).ToString(CultureInfo.InvariantCulture),
                listener.TargetType, listener.Occurrence.ToString(CultureInfo.InvariantCulture), listener.Method,
                listener.Enabled ? "1" : "0", listener.UseArguments ? "1" : "0"]));
        return FormatValues(entries);
    }

    /// <summary>完整验证配置后替换持久化监听器；签名和运行时订阅保持原值。</summary>
    public bool Deserialize(string text)
    {
        if (!TryParseValues(text, out string[] entries) || entries.Length < 2 || entries[0] != "1"
            || !TryParseValues(entries[1], out string[] signature) || signature.Length != parameterKinds.Length) return false;
        for (int index = 0; index < signature.Length; ++index)
            if (!uint.TryParse(signature[index], NumberStyles.None, CultureInfo.InvariantCulture, out uint kind)
                || kind != (uint)parameterKinds[index]) return false;
        List<OrbEventCall> parsed = [];
        foreach (string entry in entries.Skip(2))
        {
            if (!TryParseValues(entry, out string[] fields) || fields.Length != 7
                || !int.TryParse(fields[1], out int domain) || domain is not (1 or 2)
                || !int.TryParse(fields[3], NumberStyles.None, CultureInfo.InvariantCulture, out int occurrence)
                || fields[5] is not ("0" or "1") || fields[6] is not ("0" or "1")) return false;
            parsed.Add(new() { TargetKey = fields[0], Domain = (OrbEventDomain)domain, TargetType = fields[2],
                Occurrence = occurrence, Method = fields[4], Enabled = fields[5] == "1", UseArguments = fields[6] == "1" });
        }
        persistentCalls.Clear();
        persistentCalls.AddRange(parsed);
        return true;
    }

    /// <summary>从持久化文本创建事件。</summary>
    public static OrbEvent Parse(string text)
    {
        if (!TryParseValues(text, out string[] entries) || entries.Length < 2 || !TryParseValues(entries[1], out string[] signature))
            throw new FormatException("Invalid OrbEvent data.");
        OrbEvent result = new(signature.Select(item => (InteropValueKind)uint.Parse(item, CultureInfo.InvariantCulture)).ToArray());
        if (!result.Deserialize(text)) throw new FormatException("Invalid OrbEvent data.");
        return result;
    }

    //编码 UTF-8 长度前缀数组
    private static string FormatValues(IEnumerable<string> values)
    {
        string[] entries = values.ToArray();
        System.Text.StringBuilder text = new(entries.Length.ToString(CultureInfo.InvariantCulture) + ":");
        foreach (string entry in entries) text.Append(InteropText.GetUtf8ByteCount(entry).ToString(CultureInfo.InvariantCulture)).Append(':').Append(entry);
        return text.ToString();
    }

    //解析 UTF-8 长度前缀数组
    private static bool TryParseValues(string text, out string[] values)
    {
        values = [];
        byte[] bytes = InteropText.EncodeUtf8(text);
        int position = 0;
        if (!ReadLength(bytes, ref position, out int count) || count > (bytes.Length - position) / 2) return false;
        string[] parsed = new string[count];
        for (int index = 0; index < count; ++index)
        {
            if (!ReadLength(bytes, ref position, out int length) || length > bytes.Length - position) return false;
            parsed[index] = InteropText.DecodeUtf8(bytes, position, length);
            position += length;
        }
        if (position != bytes.Length) return false;
        values = parsed;
        return true;
    }

    //读取非负长度
    private static bool ReadLength(byte[] bytes, ref int position, out int length)
    {
        length = 0;
        int begin = position;
        while (position < bytes.Length && bytes[position] != (byte)':')
        {
            int digit = bytes[position++] - (byte)'0';
            if (digit is < 0 or > 9 || length > (int.MaxValue - digit) / 10) return false;
            length = length * 10 + digit;
        }
        if (position == begin || position >= bytes.Length) return false;
        ++position;
        return true;
    }
}

/// <summary>具有一个类型化参数的可序列化事件。</summary>
public sealed class OrbEvent<T> : OrbEvent
{
    /// <summary>创建类型化事件。</summary>
    public OrbEvent() : base(GetParameterKind()) { }

    /// <summary>添加类型化运行时监听器。</summary>
    public void Subscribe(Action<T> listener) => SubscribeTyped(listener, arguments =>
    {
        if (!ManagedTypeMetadataCache.TryFromInterop(arguments[0], typeof(T), out object? value))
            throw new ArgumentException("Unsupported event argument.");
        listener((T)value!);
    });

    /// <summary>移除类型化运行时监听器。</summary>
    public void Unsubscribe(Action<T> listener) => UnsubscribeTyped(listener);

    /// <summary>派发类型化事件。</summary>
    public void Dispatch(T argument)
    {
        if (!ManagedTypeMetadataCache.TryToInterop(argument, typeof(T), out InteropValue value))
            throw new ArgumentException("Unsupported event argument.", nameof(argument));
        base.Dispatch(value);
    }

    //获取精确参数类型
    private static InteropValueKind GetParameterKind()
    {
        if (!ManagedTypeMetadataCache.TryGetKind(typeof(T), out InteropValueKind kind)
            || kind is InteropValueKind.Empty or InteropValueKind.Array || typeof(OrbEvent).IsAssignableFrom(typeof(T)))
            throw new ArgumentException($"Unsupported event parameter: {typeof(T)}.");
        return kind;
    }
}
