namespace Orbeden;

/// <summary>
/// 托管帧系统：由 ScriptSystem 的固定阶段直接驱动，独立于游戏脚本的 Update 门控。
/// 输入与渲染准备两阶段不受模拟暂停影响；编辑模式只执行渲染准备。
/// 核心只保留本接口与注册表，具体实现（如 UI 上下文）由上层程序集提供。
/// </summary>
public interface IManagedFrameSystem
{
    /// <summary>世界附着后调用一次；此时可以建立索引并缓存身份。</summary>
    void AttachWorld(ulong worldRevision, bool editorMode);

    /// <summary>输入阶段；在 FixedUpdate 之前执行，编辑模式不调用。</summary>
    void ProcessInput(float deltaTime);

    /// <summary>渲染准备阶段；在全部 LateUpdate 之后、渲染消费之前执行。</summary>
    void PrepareRender(float deltaTime);

    /// <summary>世界分离前调用；逆序释放，之后不得再访问世界对象。</summary>
    void DetachWorld();
}
