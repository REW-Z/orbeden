#include "FileSystem/PathDefines.h"
#include "ResourceManager/ResourceManager.h"
#include "Runtime/Reflection.h"
#include "Runtime/WorldSerializer.h"
#include "Runtime/Object/HeightField.h"
#include "Runtime/Object/StaticMeshRenderer.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

/// <summary>检查真实引擎运行结果，失败时返回非零退出码。</summary>
void Require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

/// <summary>读取保存的场景文件，检查持久化内容。</summary>
std::string ReadFile(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    Require(static_cast<bool>(input), "Cannot read saved world");
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

/// <summary>检查地形保存、完整重载、组件快照以及运行时对象释放。</summary>
int main() try
{
    PathDefines::SetContentRoot(std::filesystem::absolute("OrbedenEditor/Templates").generic_string());
    Reflection::RegisterGeneratedReflection();
    const std::string meshKey = "Examples/FlightTraining/Meshes/ground.obj//Mesh/Main";
    const std::string materialKey = "Examples/FlightTraining/Meshes/ground.obj//Material/GroundMaterial";
    World world;
    World::SetCurrentWorld(&world);
    Mesh* sourceMesh = ResourceManager::Load<Mesh>(meshKey);
    Material* sourceMaterial = ResourceManager::Load<Material>(materialKey);
    Require(sourceMesh && sourceMaterial, "Cannot import real ground resources");

    Ens* ens = world.CreateEns("Terrain");
    auto* renderer = ens->AddComponent<StaticMeshRenderer>();
    renderer->mesh.Set(sourceMesh);
    const std::string rendererSnapshot = WorldSerializer::CaptureComponent(renderer);
    Require(renderer->GetRenderMesh() == sourceMesh, "Ordinary source mesh is not rendered");
    auto* terrain = ens->AddComponent<HeightField>();
    Require(renderer->GetRenderMesh() == sourceMesh, "Pending terrain lost source mesh");
    terrain->rowCount = 3;
    terrain->columnCount = 3;
    terrain->noiseTextureSize = 16;
    terrain->material.Set(sourceMaterial);
    terrain->Regenerate();
    Mesh* generatedMesh = renderer->GetRenderMesh();
    Require(generatedMesh && generatedMesh != sourceMesh, "Terrain did not override render mesh");
    Require(renderer->mesh.Get() == sourceMesh, "Terrain overwrote persistent mesh");
    Require(WorldSerializer::CaptureComponent(renderer) == rendererSnapshot, "Terrain changed renderer snapshot");
    Require(terrain->GetSurfaceMaterial() && terrain->GetSurfaceMaterial() != sourceMaterial, "Runtime noise material was not created");
    const std::string oldMeshId = generatedMesh->GetInstanceId().GetPath();
    const std::string oldMaterialId = terrain->GetSurfaceMaterial()->GetInstanceId().GetPath();
    const std::string terrainSnapshot = WorldSerializer::CaptureComponent(terrain);
    uint32 generation = terrain->GetGeneration();
    terrain->Regenerate();
    Require(terrain->GetGeneration() > generation, "Terrain did not regenerate");
    Require(WorldSerializer::CaptureComponent(terrain) == terrainSnapshot, "Runtime generation changed persistent snapshot");
    Require(terrainSnapshot.find("ownsRuntimeMaterial") == std::string::npos
        && terrainSnapshot.find("meshPending") == std::string::npos
        && terrainSnapshot.find("name=\"generation\"") == std::string::npos, "Runtime state was serialized");
    std::cout << "PASS: real asset references, runtime geometry/material and stable snapshots\n";

    const std::string path = "Log/HeightFieldPersistence/saved.world";
    Require(WorldSerializer::SaveXml(world, path), "Cannot save terrain world");
    const std::string saved = ReadFile(path);
    Require(saved.find(meshKey) != std::string::npos && saved.find(materialKey) != std::string::npos, "Resource paths disappeared");
    Require(saved.find("world://runtime/") == std::string::npos && saved.find("runtimeMesh") == std::string::npos, "Runtime data leaked into saved world");

    //旧文件可能含这些缓存字段；加载时应忽略，不能覆盖本次生成状态。
    std::string legacy = saved;
    size_t fieldPosition = legacy.find("</Component>", legacy.find("<Component type=\"HeightField\""));
    Require(fieldPosition != std::string::npos, "Missing terrain component in save");
    legacy.insert(fieldPosition, "<Field name=\"generation\" type=\"uint32\" value=\"4294967295\" />\n"
        "<Field name=\"meshPending\" type=\"bool\" value=\"false\" />\n"
        "<Field name=\"ownsRuntimeMaterial\" type=\"bool\" value=\"true\" />\n");
    const std::string legacyPath = "Log/HeightFieldPersistence/legacy.world";
    { std::ofstream output(legacyPath, std::ios::binary); output << legacy; }
    world.Clear();
    ResourceManager::Shutdown();
    Require(!Object::FindObject(StringId(oldMeshId)) && !Object::FindObject(StringId(oldMaterialId)), "Old generated resources survived world clear");
    Require(WorldSerializer::LoadXml(world, legacyPath), "Cannot load saved terrain world");
    ens = nullptr;
    world.ForEachEns([&](Ens& value) { ens = &value; });
    Require(ens != nullptr, "Loaded world has no terrain entity");
    renderer = ens->GetComponent<StaticMeshRenderer>();
    terrain = ens->GetComponent<HeightField>();
    Require(renderer && terrain, "Loaded terrain components are missing");
    terrain->SyncPendingGeneration();
    Require(renderer->mesh.Get() && renderer->mesh.GetInstanceId().GetPath() == meshKey, "Source resource was not restored");
    Require(renderer->GetRenderMesh() && renderer->GetRenderMesh() != renderer->mesh.Get(), "Fresh session did not rebuild terrain");
    Require(renderer->GetRenderMesh()->GetInstanceId().GetPath() != oldMeshId, "Fresh session reused stale runtime identity");
    Require(terrain->GetSurfaceMaterial() && terrain->GetSurfaceMaterial() != terrain->material.Get(), "Legacy ownership flag broke material regeneration");
    Require(terrain->GetGeneration() != 4294967295u, "Legacy generation counter was applied");
    Require(WorldSerializer::SaveXml(world, path) && ReadFile(path) == saved, "Save/load/save changed persistent terrain data");
    std::cout << "PASS: fresh resource reload, legacy cache fields ignored and stable save/load/save\n";

    //移除地形恢复源网格；撤销恢复组件快照时仍能生成专属资源。
    const std::string restoredTerrain = WorldSerializer::CaptureComponent(terrain);
    std::string detachedMeshId = renderer->GetRenderMesh()->GetInstanceId().GetPath();
    Require(ens->RemoveComponent<HeightField>(), "Cannot detach terrain");
    Require(renderer->GetRenderMesh() == renderer->mesh.Get(), "Detach did not restore source mesh");
    Require(!Object::FindObject(StringId(detachedMeshId)), "Detach leaked generated mesh");
    terrain = static_cast<HeightField*>(WorldSerializer::RestoreComponent(*ens, restoredTerrain, 2));
    Require(terrain != nullptr, "Cannot restore terrain snapshot");
    terrain->SyncPendingGeneration();
    Require(renderer->GetRenderMesh() != renderer->mesh.Get(), "Restored terrain did not override mesh");
    Require(WorldSerializer::CaptureComponent(terrain) == restoredTerrain, "Restored terrain changed snapshot");
    Require(ens->RemoveComponent<HeightField>(), "Cannot detach restored terrain");
    std::cout << "PASS: detach releases resources and component snapshot restoration regenerates terrain\n";

    //空覆盖、失效覆盖和空源网格行为；普通 Ref 的序列化语义保持不变。
    sourceMesh = renderer->mesh.Get();
    auto* temporary = Object::CreateInstance<Mesh>();
    renderer->SetRuntimeMesh(temporary);
    renderer->SetEnabled(false);
    renderer->SetEnabled(true);
    Require(renderer->GetRenderMesh() == temporary, "Enable toggle lost override");
    Object::DeleteInstance(temporary);
    Require(renderer->GetRenderMesh() == sourceMesh, "Expired override did not restore source");
    renderer->mesh.Set(nullptr);
    temporary = Object::CreateInstance<Mesh>();
    renderer->SetRuntimeMesh(temporary);
    const std::string temporaryId = temporary->GetInstanceId().GetPath();
    Require(WorldSerializer::CaptureComponent(renderer).find(temporaryId) == std::string::npos, "Empty source serialized override");
    renderer->SetRuntimeMesh(nullptr);
    Require(renderer->GetRenderMesh() == nullptr, "Cleared override did not restore empty source");
    renderer->mesh.Set(temporary);
    Require(WorldSerializer::CaptureComponent(renderer).find(temporaryId) != std::string::npos, "Global Ref serialization semantics changed");
    world.Clear();
    ResourceManager::Shutdown();
    World::SetCurrentWorld(nullptr);
    PathDefines::Clear();
    std::cout << "PASS: expired/empty overrides, enable toggle and unchanged ordinary Ref semantics\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
