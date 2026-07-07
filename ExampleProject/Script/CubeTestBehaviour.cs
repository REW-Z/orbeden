using System.Collections.Generic;
using OrbedenCore.CSharp;

namespace ExampleGame;

/// <summary>挂在示例 Cube 上的脚本组件测试。</summary>
public sealed class CubeTestBehaviour : ScriptBehaviour
{
    [SerializeField]
    private string label = "Cube script component";
    [SerializeField]
    private bool animateScale = true;
    [SerializeField]
    private float pulseAmplitude = 0.18f;
    [SerializeField]
    private float pulseSpeed = 2.5f;
    [SerializeField]
    private vector3 debugOffset = new(0.0f, 0.0f, 0.0f);

    private vector3 baseScale;
    private float elapsedTime;
    private int updateCount;

    /// <summary>创建 Cube 脚本组件测试。</summary>
    public CubeTestBehaviour(Ens ens) : base(ens) {}

    /// <summary>Inspector 中显示当前运行状态。</summary>
    public string Status => $"{label}: {updateCount} updates";

    /// <summary>应用 world sidecar 中保存的序列化字段。</summary>
    internal void ApplySerializedValues(IReadOnlyDictionary<string, string> values)
    {
        if (ScriptValueReader.TryGetString(values, nameof(label), out string labelValue)) label = labelValue;
        if (ScriptValueReader.TryGetBool(values, nameof(animateScale), out bool animateScaleValue)) animateScale = animateScaleValue;
        if (ScriptValueReader.TryGetFloat(values, nameof(pulseAmplitude), out float pulseAmplitudeValue)) pulseAmplitude = pulseAmplitudeValue;
        if (ScriptValueReader.TryGetFloat(values, nameof(pulseSpeed), out float pulseSpeedValue)) pulseSpeed = pulseSpeedValue;
        if (ScriptValueReader.TryGetVector3(values, nameof(debugOffset), out vector3 debugOffsetValue)) debugOffset = debugOffsetValue;
    }

    /// <summary>脚本启动时调用。</summary>
    protected override void OnStart()
    {
        baseScale = Ens.Space.localScale;
        Console.WriteLine($"CubeTestBehaviour start: {label}");
    }

    /// <summary>脚本每帧更新时调用。</summary>
    protected override void OnUpdate(float deltaTime)
    {
        elapsedTime += deltaTime;
        updateCount++;

        if (animateScale)
        {
            float scale = 1.0f + MathF.Sin(elapsedTime * pulseSpeed) * pulseAmplitude;
            Ens.Space.localScale = new vector3(baseScale.x * scale, baseScale.y * scale, baseScale.z * scale);
        }

        if (debugOffset.x != 0.0f || debugOffset.y != 0.0f || debugOffset.z != 0.0f)
        {
            vector3 position = Ens.Space.localPosition;
            position.x += debugOffset.x * deltaTime;
            position.y += debugOffset.y * deltaTime;
            position.z += debugOffset.z * deltaTime;
            Ens.Space.localPosition = position;
        }
    }

    /// <summary>脚本结束时调用。</summary>
    protected override void OnEnd()
    {
        Ens.Space.localScale = baseScale;
        Console.WriteLine($"CubeTestBehaviour end: {label}");
    }
}
