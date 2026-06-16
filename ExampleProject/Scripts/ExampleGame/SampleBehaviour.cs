using Orbeden;

namespace ExampleGame;

/// <summary>示例托管脚本行为。</summary>
public sealed class SampleBehaviour : ScriptBehaviour
{
    private float elapsedTime;
    private int reportCount;

    /// <summary>脚本启动时调用。</summary>
    protected override void OnStart()
    {
        Console.WriteLine($"SampleBehaviour start: Ens({Ens.id}, {Ens.version})");
    }

    /// <summary>脚本每帧更新时调用。</summary>
    protected override void OnUpdate(float deltaTime)
    {
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
