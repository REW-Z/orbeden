using Orbeden;

namespace AviatorSage;

/// <summary>方向舵动作：按偏航输入绕选定本地轴偏转。</summary>
public sealed class RudderSurface : Script
{
    public Ens root = null!;
    public float maxAngle = 25.0f;
    public float responseSpeed = 180.0f;
    public SurfaceAxis axis = SurfaceAxis.Z;
    public bool reverse = false;

    private NativeMethod<float>? getYawInput;
    private Transform? transform;
    private vector3 baseEuler;
    private float angle;

    public RudderSurface(Ens ens) : base(ens) { }

    private void OnStart()
    {
        transform = Ens.GetComponent<Transform>();
        if (transform != null)
        {
            baseEuler = transform.localEulerAngles;
        }

        ComponentProxy? controller = root?.GetNativeComponent("FlightController");
        if (controller != null)
        {
            controller.TryBindMethod<float>("GetYawInput", out getYawInput);
        }
    }

    private void OnUpdate(float deltaTime)
    {
        if (getYawInput == null || transform == null)
        {
            return;
        }

        float target = getYawInput() * maxAngle * (reverse ? -1.0f : 1.0f);
        float step = MathF.Max(responseSpeed, 0.0f) * MathF.Max(deltaTime, 0.0f);
        angle += MathF.Max(-step, MathF.Min(step, target - angle));
        transform.localEulerAngles = SurfaceAxisEuler.Apply(baseEuler, axis, angle);
    }
}
