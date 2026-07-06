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
