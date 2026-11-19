using System.Text;

namespace Orbeden;

/// <summary>世界加载操作状态。</summary>
public enum WorldLoadState
{
    Preparing, WaitingToCommit, Committing, Succeeded, Failed, Cancelled
}

/// <summary>查询应用拥有的世界加载结果。</summary>
public sealed class WorldLoadOperation
{
    private readonly ulong id;
    private WorldLoadState state = WorldLoadState.Preparing;
    private string error = string.Empty;

    //记录本次加载身份
    internal WorldLoadOperation(ulong value) { id = value; }

    public WorldLoadState State
    {
        get
        {
            if (state < WorldLoadState.Succeeded)
            {
                state = World.GetLoadState(id);
                if (state >= WorldLoadState.Succeeded) error = World.GetLoadError(id);
            }
            return state;
        }
    }
    public bool IsDone => State >= WorldLoadState.Succeeded;
    public bool Succeeded => State == WorldLoadState.Succeeded;
    public string Error { get { _ = State; return error; } }
}

/// <summary>当前应用的 World 加载入口。</summary>
public static unsafe class World
{
    private static WorldBindApi api;

    /// <summary>每个实例可选着色参数的 float 数量。</summary>
    public const int DrawParameterFloatCount = 32;

    //注册直接实例绘制的托管签名
    static World()
    {
        NativeBindingRuntime.Register(typeof(World), "World.DrawMeshInstanced", 1, null);
    }

    /// <summary>提交世界空间实例供下一次渲染使用。实例缓冲可在返回后复用。可选参数按每实例 32 个 float 排列，提交失败返回 false。</summary>
    public static bool DrawMeshInstanced(Mesh mesh, Material material, ReadOnlySpan<MeshInstanceData> instances,
        ReadOnlySpan<float> drawParameters = default, uint subMeshIndex = 0, InstanceDrawOptions? options = null)
    {
        InstanceDrawOptions settings = options ?? InstanceDrawOptions.Default;
        var draw = (delegate* unmanaged[Cdecl]<int, int, MeshInstanceData*, int, float*, int, uint, uint, byte, byte, EnsId, byte>)
            NativeBindingRuntime.GetFunction(typeof(World), 0);
        fixed (MeshInstanceData* data = instances)
        fixed (float* parameters = drawParameters)
        {
            return draw(NativeBindingRuntime.GetObjectId(mesh), NativeBindingRuntime.GetObjectId(material),
                data, instances.Length, parameters, drawParameters.Length, subMeshIndex, settings.drawLayer,
                (byte)(settings.castShadows ? 1 : 0), (byte)(settings.receiveShadows ? 1 : 0), settings.camera) != 0;
        }
    }

    //连接世界加载函数表
    internal static void InitializeNativeApi(WorldBindApi value) { api = value; }

    /// <summary>同步准备世界，生命周期回调内的切换在安全点提交。</summary>
    public static WorldLoadOperation Load(string key) => StartLoad(key, false);

    /// <summary>后台读取世界并在主线程安全点提交。</summary>
    public static WorldLoadOperation LoadAsync(string key) => StartLoad(key, true);

    //提交世界加载请求
    private static WorldLoadOperation StartLoad(string key, bool asynchronous)
    {
        ArgumentNullException.ThrowIfNull(key);
        if (api.LoadWorld == null) return new WorldLoadOperation(0);
        byte[] bytes = InteropText.EncodeUtf8(key);
        fixed (byte* text = bytes)
            return new WorldLoadOperation(api.LoadWorld(text, bytes.Length, asynchronous ? (byte)1 : (byte)0));
    }

    //查询操作状态
    internal static WorldLoadState GetLoadState(ulong id) =>
        id != 0 && api.GetWorldLoadState != null ? (WorldLoadState)api.GetWorldLoadState(id) : WorldLoadState.Cancelled;

    //读取操作失败原因
    internal static string GetLoadError(ulong id)
    {
        if (id == 0 || api.GetWorldLoadError == null) return "World runtime is unavailable.";
        int size = api.GetWorldLoadError(id, null, 0);
        if (size <= 0) return string.Empty;
        byte[] bytes = new byte[size];
        fixed (byte* text = bytes)
        {
            int count = api.GetWorldLoadError(id, text, size);
            return InteropText.DecodeUtf8(bytes, 0, Math.Min(count, size));
        }
    }
}
