using System.Collections.Generic;
using OrbedenCore.CSharp;

namespace ExampleGame;

/// <summary>示例托管脚本行为。</summary>
public sealed class SampleBehaviour : ScriptBehaviour
{
    [SerializeField]
    private vector3 startPosition;
    [SerializeField]
    private float totalTime;
    [SerializeField]
    private float elapsedTime;
    [SerializeField]
    private int reportCount;

    /// <summary>创建示例托管脚本行为。</summary>
    public SampleBehaviour(Ens ens) : base(ens) {}

    /// <summary>应用 world sidecar 中保存的序列化字段。</summary>
    internal void ApplySerializedValues(IReadOnlyDictionary<string, string> values)
    {
        if (ScriptValueReader.TryGetVector3(values, nameof(startPosition), out vector3 startPositionValue)) startPosition = startPositionValue;
        if (ScriptValueReader.TryGetFloat(values, nameof(totalTime), out float totalTimeValue)) totalTime = totalTimeValue;
        if (ScriptValueReader.TryGetFloat(values, nameof(elapsedTime), out float elapsedTimeValue)) elapsedTime = elapsedTimeValue;
        if (ScriptValueReader.TryGetInt(values, nameof(reportCount), out int reportCountValue)) reportCount = reportCountValue;
    }

    /// <summary>脚本启动时调用。</summary>
    protected override void OnStart()
    {
        startPosition = Ens.Space.localPosition;
        Console.WriteLine($"SampleBehaviour start: Ens({EnsId.id}, {EnsId.version})");
    }

    /// <summary>脚本每帧更新时调用。</summary>
    protected override void OnUpdate(float deltaTime)
    {
        totalTime += deltaTime;
        SpaceComponent space = Ens.Space;
        vector3 position = startPosition;
        position.y += MathF.Sin(totalTime) * 0.25f;
        space.localPosition = position;

        StaticMeshRenderer? renderer = Ens.GetComponent<StaticMeshRenderer>();
        if (renderer != null)
        {
            renderer.castShadows = true;
            renderer.receiveShadows = true;
        }

        elapsedTime += deltaTime;
        if (elapsedTime < 2.0f) return;

        elapsedTime = 0.0f;
        reportCount++;
        Console.WriteLine($"SampleBehaviour update report: {reportCount}");
    }

    /// <summary>脚本结束时调用。</summary>
    protected override void OnEnd()
    {
        Console.WriteLine("SampleBehaviour end");
    }
}
