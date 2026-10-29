using Orbeden;

namespace AviatorSage;

/// <summary>副翼动作：按滚转输入绕选定本地轴偏转。</summary>
public sealed class AileronSurface : Script
{
    public Ens root = null!;
    public float maxAngle = 20.0f;
    public float responseSpeed = 180.0f;
    public SurfaceAxis axis = SurfaceAxis.X;
    public bool reverse = false;

    private NativeMethod<float>? getRollInput;
    private Transform? transform;
    private vector3 baseEuler;
    private float angle;

    public AileronSurface(Ens ens) : base(ens) { }

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
            controller.TryBindMethod<float>("GetRollInput", out getRollInput);
        }
    }

    private void OnUpdate(float deltaTime)
    {
        if (getRollInput == null || transform == null)
        {
            return;
        }

        float target = getRollInput() * maxAngle * (reverse ? -1.0f : 1.0f);
        float step = MathF.Max(responseSpeed, 0.0f) * MathF.Max(deltaTime, 0.0f);
        angle += MathF.Max(-step, MathF.Min(step, target - angle));
        transform.localEulerAngles = SurfaceAxisEuler.Apply(baseEuler, axis, angle);
    }
}
