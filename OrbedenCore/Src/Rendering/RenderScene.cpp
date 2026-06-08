#include "Rendering/RenderScene.h"

void RenderScene::Clear()
{
    cameras.clear();
    items.clear();
}

void VisibleSet::Clear()
{
    camera = RenderCamera();
    items.clear();
}

