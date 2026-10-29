using Orbeden;

namespace AviatorSage;

/// <summary>襟翼动作：按襟翼位置绕选定本地轴偏转。</summary>
public sealed class FlapSurface : Script
{
    public Ens root = null!;
    public float maxAngle = 30.0f;
    public float responseSpeed = 180.0f;
    public SurfaceAxis axis = SurfaceAxis.X;
    public bool reverse = false;

    private NativeMethod<float>? getFlapPosition;
    private Transform? transform;
    private vector3 baseEuler;
    private float angle;

    public FlapSurface(Ens ens) : base(ens) { }

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
            controller.TryBindMethod<float>("GetFlapPosition", out getFlapPosition);
        }
    }

    private void OnUpdate(float deltaTime)
    {
        if (getFlapPosition == null || transform == null)
        {
            return;
        }

        float target = getFlapPosition() * maxAngle * (reverse ? -1.0f : 1.0f);
        float step = MathF.Max(responseSpeed, 0.0f) * MathF.Max(deltaTime, 0.0f);
        angle += MathF.Max(-step, MathF.Min(step, target - angle));
        transform.localEulerAngles = SurfaceAxisEuler.Apply(baseEuler, axis, angle);
    }
}
