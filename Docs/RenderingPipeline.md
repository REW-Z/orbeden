# Orbeden 渲染管线概览

Orbeden 当前使用单线程、立即提交式的 OpenGL Forward Renderer。渲染组件主动注册到持久 `RenderScene`，每帧增量刷新变换和资源版本，再为各相机执行裁剪、SubMesh 展开、排序和绘制。

## 核心模块

| 模块 | 职责 |
| --- | --- |
| `RenderSystem` | 初始化、帧调度、多相机、Viewport、RenderTarget、Overlay |
| `TransformCache` | 根据 Transform 通知更新脏子树 |
| `RenderScene` | 持久维护 Camera、Light、RendererEntry 注册 |
| `SceneCuller` | Layer Mask 与视锥裁剪 |
| `RenderItemSorter` | 不透明/透明队列排序 |
| `ForwardPipeline` | 阴影、天空盒、Forward Main Pass |
| `GpuResourceManager` | GPU 资源按需上传、缓存和释放 |
| `OpenGLRenderBackend` | OpenGL Pass、状态、Uniform 和 Draw 调用 |

## 单帧总流程

```mermaid
flowchart TD
    A["World / ECS"] --> B["CollectUnused\n回收 GPU 缓存"]
    B --> C["Update RenderScene\n处理注册与变换通知"]
    C --> D["Resolve Camera\nRenderTarget + Viewport"]
    D --> E{"有相机?"}
    E -- 否 --> F["默认窗口清黑"]
    E -- 是 --> G["共享 Shadow Pass\n每帧一次"]
    G --> H["按 Camera.depth 遍历"]
    H --> I["Cull RendererEntry\n→ 展开 RenderItem → Sort → Main Pass"]
    I --> J{"还有相机?"}
    J -- 是 --> H
    J -- 否 --> K["ImGui / Overlay"]
    F --> K
    K --> L["Present / Swap Buffers"]
```

### 1. 更新持久场景

`RenderScene` 在绑定 World 时完整收集一次已有组件，后续由 Camera、DirectionalLight 和 StaticMeshRenderer 的 Attach、Detach 与 enabled 变化维护注册：

- `TransformComponent` setter 和父级变化通过 `ITransformListener` 通知各 `TransformCache`，不再每帧扫描所有 Ens。
- `TransformCache` 只递归更新收到通知的子树，并把本次受影响的 Ens 提供给 `RenderScene`。
- Camera 生成 View、Projection、ViewProjection 和视锥快照，并按 `depth` 升序排列。
- DirectionalLight 复制光照与阴影参数。
- 每个 StaticMeshRenderer 对应一个持久 `RendererEntry`，共享变换、Mesh revision 和世界 AABB。
- 全局场景不保存扁平 SubMesh 列表；只有相机剔除后的 Renderer 才临时展开 `RenderItem`。

渲染读取期间发生的组件增删会排队到安全阶段执行。`RenderSceneHandle` 带版本，延迟删除不会误删复用槽位中的新组件。

### 2. Viewport 与 RenderTarget

Camera 使用归一化 Viewport，默认 `(0, 0, 1, 1)`。RenderSystem 根据窗口或离屏目标尺寸换算为像素区域，并重新计算相机宽高比和视锥。

- `renderTargetId = 0`：绘制到窗口。
- 有效非零 ID：绘制到离屏 FBO。
- 无效 ID 或零尺寸 Viewport：跳过该相机。

普通 RenderTarget 由颜色纹理、深度纹理和 FBO 组成；创建、Resize、项目 Reload 和 Shutdown 都由 RenderSystem 统一管理。

### 3. 共享阴影 Pass

Forward Pipeline 选择第一个开启阴影的方向光，每帧生成一次共享阴影图：

```mermaid
flowchart LR
    A["场景 Bounds 中心"] --> B["方向光 View"]
    B --> C["正交 Projection"]
    C --> D["1024×1024 深度 FBO"]
    D --> E["筛选 castShadows"]
    E --> F["光源视锥裁剪"]
    F --> G["Depth Shader DrawIndexed"]
```

阴影 Pass 只写深度，不创建颜色附件；生成的阴影图供所有相机共享。

### 4. 每相机裁剪与排序

裁剪规则：

1. `renderer.drawLayer & camera.drawLayerMask` 必须非零。
2. `RendererEntry.worldBounds` 必须与相机视锥相交。
3. `VisibleItem` 只保存 `rendererIndex + cameraDistance`。
4. 剔除完成后，按可见 Renderer 当前的 Mesh/SubMesh 生成相机临时 `RenderItem`。

排序规则：

- Opaque：Material → Mesh → 近到远。
- Transparent：远到近。

### 5. Forward Main Pass

```mermaid
flowchart TD
    A["绑定 Camera FBO / Viewport"] --> B["按 ClearMode 局部清屏"]
    B --> C{"SolidColor 且启用 Skybox?"}
    C -- 是 --> D["绘制 Skybox"]
    C -- 否 --> E["Opaque Pass"]
    D --> E
    E --> F["DepthTest On\nDepthWrite On\nBlend Off"]
    F --> G["Transparent Pass"]
    G --> H["DepthTest On\nDepthWrite Off\nBlend On"]
```

每个 `RenderItem` 会按 Shader 中的 Pass 声明顺序连续绘制。Pass 可独立配置 `DepthTest`、`DepthWrite`、`Blend` 和 `Cull`；`Auto` 每次从当前 Opaque/Transparent 管线基线解析，不继承前一个 Pass 的状态。

`ClearMode` 含义：

- `SolidColor`：清颜色和深度，并允许绘制天空盒。
- `DepthOnly`：只清深度，保留前一相机颜色。
- `None`：完全保留目标内容。

清屏使用 Scissor 限制在当前 Viewport，因此分屏相机不会互相清除画面。

绘制时，同一 Shader Program 的相机/灯光 Uniform 每个相机只设置一次。Material 参数会对每个 Pass Program 分别写入。

### 6. OrbShader 多 Pass

旧的单组 `vert/frag` 文件会自动成为名为 `Default` 的 Pass。多 Pass 文件使用以下格式：

```glsl
--------pass Outline
depthTest auto
depthWrite off
blend auto
cull front
--------vert
// vertex GLSL
--------frag
// fragment GLSL
```

Pass 名称区分大小写且必须唯一；分段和状态关键字不区分大小写。每个 Pass 必须各有一个 `vert` 和 `frag`，状态只能写在 Pass 头与第一个 Shader Stage 之间。支持的值为：

- `depthTest`、`depthWrite`、`blend`：`auto`、`on`、`off`。
- `cull`：`auto`、`none`、`front`、`back`。

## GPU 资源缓存

Mesh、Texture、Skybox、Shader、Material 在第一次使用时上传到 GPU。Shader 的所有 Pass 会作为一个整体编译和缓存，任一 Pass 编译失败都会使整个 Shader 无效。

缓存有效时直接返回；无缓存或版本变化时创建/刷新 GPU 对象。每帧开头的 `CollectUnused` 检查 CPU 对象是否仍存活，不再存活时删除 GPU 对象和缓存项。

Backend 还会缓存 Program、VAO、Texture Slot、Depth/Blend 状态和 Uniform Location，避免重复 OpenGL 调用。

## 当前边界

- 当前只有 OpenGL Backend。
- 实际只使用一个主方向光和一张共享阴影图。
- 阴影图固定为 `1024 × 1024`，尚未实现 CSM。
- 相机裁剪仍为线性扫描，没有 BVH 或 GPU Culling。
- 每个可见 SubMesh 对应一次 `DrawIndexed`，尚未实现 Instancing/Indirect Draw。
- RenderScene 在同一线程增量更新并立即消费。

## 主要源码

- 总调度：`OrbedenCore/Src/Rendering/RenderSystem.cpp`
- 持久场景：`OrbedenCore/Src/Rendering/RenderScene.cpp`
- 阴影与主 Pass：`OrbedenCore/Src/Rendering/ForwardPipeline.cpp`
- GPU 缓存：`OrbedenCore/Src/Rendering/GpuResourceManager.cpp`
- OpenGL Backend：`OrbedenCore/Src/Rendering/Backend/OpenGLRenderBackend.cpp`
