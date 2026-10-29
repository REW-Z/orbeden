using Orbeden;

namespace AviatorSage;

/// <summary>升降舵动作：按俯仰输入绕选定本地轴偏转。</summary>
public sealed class ElevatorSurface : Script
{
    public Ens root = null!;
    public float maxAngle = 20.0f;
    public float responseSpeed = 180.0f;
    public SurfaceAxis axis = SurfaceAxis.X;
    public bool reverse = false;

    private NativeMethod<float>? getPitchInput;
    private Transform? transform;
    private vector3 baseEuler;
    private float angle;

    public ElevatorSurface(Ens ens) : base(ens) { }

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
            controller.TryBindMethod<float>("GetPitchInput", out getPitchInput);
        }
    }

    private void OnUpdate(float deltaTime)
    {
        if (getPitchInput == null || transform == null)
        {
            return;
        }

        float target = getPitchInput() * maxAngle * (reverse ? -1.0f : 1.0f);
        float step = MathF.Max(responseSpeed, 0.0f) * MathF.Max(deltaTime, 0.0f);
        angle += MathF.Max(-step, MathF.Min(step, target - angle));
        transform.localEulerAngles = SurfaceAxisEuler.Apply(baseEuler, axis, angle);
    }
}
