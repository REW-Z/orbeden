#include "Rendering/RenderScene.h"

void RenderScene::Clear()
{
    //重置全局渲染设置和本帧收集的相机、灯光、绘制项。
    renderSettings = RenderSettings();

    cameras.clear();
    directionalLights.clear();
    items.clear();
}

void VisibleSet::Clear()
{
    //清除上一相机的快照和可见项，供剔除阶段重新填充。
    camera = RenderCamera();

    items.clear();
}
