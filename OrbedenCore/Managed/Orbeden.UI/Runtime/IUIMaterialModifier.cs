namespace Orbeden;

/// <summary>
/// 材质修改器。挂在图形同一 Ens 上、处于启用状态的托管组件实现它，
/// 在图形自身的绘制状态与显式材质覆盖之后按组件顺序运行一次。
/// 参数变化时调用目标图形的 SetMaterialDirty()；动画效果在更新参数后同样标脏。
/// </summary>
public interface IUIMaterialModifier
{
    /// <summary>
    /// 修改本片段的绘制状态：材质、纹理、颜色与距离参数都可以换。
    /// 矩阵、裁剪与软遮罩这些保留参数由提交器写入，修改器改不动；
    /// 需要独立参数时克隆一份运行时材质并写进 state.material，不要改写共享资源。
    /// </summary>
    void ModifyMaterial(ref UIDrawState state);
}
