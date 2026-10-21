using Orbeden;

namespace AviatorSage;

/// <summary>示例脚本。</summary>
public sealed class ExamplePropeller : Script
{
    public ExamplePropeller(Ens ens) : base(ens) { }

    private void OnUpdate(float deltaTime)
    {
        // this.Ens.GetComponent<Transform>().SetLocalRotation();
    }
}
