using System;

using Orbeden;
namespace {{PROJECT_NAME}};

/// <summary>示例托管脚本行为。</summary>
public sealed class SampleBehaviour : Script
{
    [SerializeField]
    private vector3 startPosition;
    [SerializeField]
    private float totalTime;
    [SerializeField]
    private float elapsedTime;
    [SerializeField]
    private int reportCount;
    private ComponentProxy? nativeSample;
    private ComponentMethod nativePingMethod;
    private float interopTimer;

    /// <summary>创建示例托管脚本行为。</summary>
    public SampleBehaviour(Ens ens) : base(ens) {}


    /// <summary>脚本启动时调用。</summary>
    private void OnStart()
    {
        startPosition = Ens.Transform.GetLocalPosition();

        //按名称只解析一次；后续通过 ComponentMethod 调用 C++ public 方法。
        nativeSample = Ens.GetNativeComponent("SampleNativeBehaviour");
        if (nativeSample != null)
        {
            nativeSample.TryResolveMethod("ReceiveManagedPing", out nativePingMethod, InteropValueKind.Float32);
        }
        Console.WriteLine($"SampleBehaviour start: Ens({EnsId.id}, {EnsId.version})");
    }

    /// <summary>脚本每帧更新时调用。</summary>
    private void OnUpdate(float deltaTime)
    {
        totalTime += deltaTime;
        Transform transform = Ens.Transform;
        vector3 position = startPosition;
        position.y += MathF.Sin(totalTime) * 0.25f;
        transform.SetLocalPosition(position);

        StaticMeshRenderer? renderer = Ens.GetComponent<StaticMeshRenderer>();
        if (renderer != null)
        {
            renderer.castShadows = true;
            renderer.receiveShadows = true;
        }

        interopTimer += deltaTime;
        if (interopTimer >= 2.0f && nativePingMethod.IsValid)
        {
            interopTimer = 0.0f;
            nativePingMethod.Invoke(out _, InteropValue.From(totalTime));
        }

        elapsedTime += deltaTime;
        if (elapsedTime < 2.0f) return;

        elapsedTime = 0.0f;
        reportCount++;
        Console.WriteLine($"SampleBehaviour update report: {reportCount}");
    }

    /// <summary>供 C++ 示例脚本通过缓存的方法句柄调用。</summary>
    public void ReceiveNativePing(float value)
    {
        Console.WriteLine($"SampleBehaviour received C++ ping: {value:F2}");
    }

    /// <summary>脚本结束时调用。</summary>
    private void OnEnd()
    {
        Console.WriteLine("SampleBehaviour end");
    }
}
