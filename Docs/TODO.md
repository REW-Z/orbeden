# 待办与已知隐患

记录尚未处理的问题。每条给出：现象、成因（带代码位置）、影响、候选方案。
处理完请删除对应条目，或移入提交说明。

---

## 场景存盘会把运行时生成的网格引用写死成会话内 id

**现象**

存过一次盘的 `.world` 里，某些 `Ref<Mesh>` 字段的值变成
`world://runtime/Mesh/<随机 uuid>`，而不是资源路径。实测样本：
`Content/Examples/FlightTraining/Scenes/main.world` 里 Terrain 的 `StaticMeshRenderer.mesh`
写成 `world://runtime/Mesh/574aa20a-8164-4feb-aaac-bed879f69890`，
而模板里同一字段是 `Examples/FlightTraining/Meshes/ground.obj//Mesh/Main`。

**成因**

1. `HeightField::RebuildRenderMesh()`（`OrbedenCore/Src/Runtime/Object/HeightField.cpp:210`）
   通过 `Object::CreateInstance<Mesh>()` 在运行时造一个网格，写进同 Ens 的 `StaticMeshRenderer.mesh`。
   该网格是世界自有的运行时对象，实例 id 由 `Object::CreateRuntimeInstancePath()`
   （`OrbedenCore/Src/Runtime/Object/Object.cpp:923`）用 `GenerateUuidText()` 生成——**每次运行都不同**。
2. `Ref<T>` 只存目标对象的实例 id（`OrbedenCore/Src/Runtime/Object/Object.h`），
   序列化时写出的就是它，于是随机 uuid 落盘。
3. 重新载入时 `LoadResourceRefsFromObject()`（`OrbedenCore/Src/Runtime/WorldSerializer.cpp:600`）
   显式跳过 `world://` 前缀的引用，所以这个 id 永远不会被解析成资源。

注意这与资源引用无关：`ResourceManager::RegisterObject()`（`OrbedenCore/Src/ResourceManager/ResourceManager.cpp:164`）
要求对象实例 id 必须等于资源 key，且拒绝 `world://` 前缀，因此常规资源引用（Mesh/Material/Shader 的
资源路径）存盘后仍是资源路径，不受影响。出问题的只有**运行时生成**、由组件自己重建的对象。

**影响**

- 存盘后地形网格等字段留下一个换会话即失效的 id；虽然 `RebuildRenderMesh()` 随后会重新生成并覆盖，
  但文件里留的是垃圾值。
- 该值会被模板写回机制带到 `OrbedenEditor/Templates/Examples/`，进而污染之后所有新建项目。
- 判定为隐患而非当前故障：地形能靠 `HeightField` 重建，所以暂时看不到画面异常。
  一旦有别的组件（或脚本）依赖这类引用且没有重建路径，就是实际故障。

**候选方案**

1. 序列化时对「指向世界自有的运行时生成对象」的 `ObjectRef` 字段不落盘，由拥有它的组件重新生成。
   判据大致是 `Object::IsRuntimeInstancePath()` 且对象所有权为 `WorldOwned`/`OrphanOwned`。
   需要确认脚本主动引用运行时对象时是否也要保留。
2. 给这类对象一个跨会话稳定的实例路径（按所属组件与用途派生），使存盘的引用可解析。
   注意 `RegisterObject()` 拒绝 `world://`，所以这仍是世界自有对象，不是资源。
3. 在 `HeightField` 侧处理：生成的网格不进 `StaticMeshRenderer.mesh` 的序列化字段
   （例如拆成独立的非序列化成员或标记该字段不落盘）。

方案 1 覆盖面最广，但改动序列化语义，需要同时考虑其它组件的重建路径。

**复现**

打开含 `HeightField` 的场景 → 运行到地形生成 → 存盘 → 检查 `.world` 中的 `Ref<Mesh>` 字段。
