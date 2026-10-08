namespace Orbeden;

/// <summary>
/// 网格修改器。挂在图形同一 Ens 上、处于启用状态的托管组件实现它，
/// 在图形生成自身片段之后按组件顺序运行一次（顺序取组件的挂载顺序）。
/// 修改器改变自身参数后要调用图形的 SetVerticesDirty()，引擎不代为监视字段。
/// </summary>
public interface IUIMeshModifier
{
    /// <summary>
    /// 修改图形刚生成的网格。下标必须保持合法；片段顺序与片段状态由引擎复核，
    /// 多纹理文字的片段不能被压成一个默认材质片段。
    /// </summary>
    void ModifyMesh(UIMeshBuilder mesh);
}
