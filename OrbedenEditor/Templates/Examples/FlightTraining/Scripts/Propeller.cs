using Orbeden;

namespace AviatorSage;

/// <summary>示例脚本。</summary>
public sealed class ExamplePropeller : Script
{
    public Ens root = null;
    private NativeMethod<float>? getThrottle;
    private float rotationAngle;

    public ExamplePropeller(Ens ens) : base(ens) { }

    private void OnStart()
    {
        var engineScript = root.GetNativeComponent("Engine");
        engineScript.TryBindMethod<float>("GetThrottle", out getThrottle);
    }
    private void OnUpdate(float deltaTime)
    {
        if (getThrottle == null) return;
        float throttleValue = getThrottle();

        float rotationSpeed = 360f * 30f * throttleValue;

        this.rotationAngle += rotationSpeed * deltaTime;
        this.Ens.GetComponent<Transform>()?.localEulerAngles = new vector3(0, rotationAngle, 0);
    }
}
