#include "Rendering/RenderScene.h"

void RenderScene::Clear()
{
    renderSettings = RenderSettings();
    cameras.clear();
    directionalLights.clear();
    items.clear();
}

void VisibleSet::Clear()
{
    camera = RenderCamera();
    items.clear();
}
