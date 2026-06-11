# Rendering Pipeline

本文档描述当前渲染系统的每帧流程、ForwardPipeline 内部 pass、核心类关系，以及 CPU 资源到 GPU 对象的上传关系。

## 每帧渲染流程

```mermaid
flowchart TD
    A[Application::Tick] --> B[BeginFrame: 输入清理 / PollEvents]
    B --> C[Gameplay Systems Update]
    C --> D[InitBuiltInSystems]
    D --> E[RenderSystem::Render]

    E --> F[RenderSceneBuilder::Build]
    F --> F1[SpaceCache::Update]
    F --> F2[收集 Camera -> RenderCamera]
    F --> F3[收集 DirectionalLight -> RenderDirectionalLight]
    F --> F4[收集 StaticMeshRenderer -> RenderItem]
    F --> F5[拷贝 World::renderSettings -> RenderScene]

    E --> G{RenderScene 有 Camera?}
    G -- 否 --> H[跳过渲染]
    G -- 是 --> I[RenderBackend::BeginFrame]

    I --> J[遍历 RenderCamera]
    J --> K[SceneCuller::Cull]
    K --> L[RenderItemSorter::Sort]
    L --> M[ForwardPipeline::Render]
    M --> J

    J --> N[RenderBackend::EndFrame]
    N --> O[Application::EndFrame / Present]
```

## ForwardPipeline Pass 流程

```mermaid
flowchart TD
    A[ForwardPipeline::Render] --> B[选择主方向光]
    B --> C{有 castShadows 的 DirectionalLight?}

    C -- 是 --> D[计算 Light ViewProjection]
    D --> E[EnsureShadowResources]
    E --> E1[CreateDepthTexture]
    E --> E2[CreateRenderTarget / FBO]
    E --> F[Shadow Pass]
    F --> F1[绑定 shadow_depth shader]
    F --> F2[绘制 scene.items 中 castShadows=true 的物体]
    F --> F3[写入 shadow depth texture]

    C -- 否 --> G[跳过 Shadow Pass]
    F3 --> H[Main Pass]
    G --> H

    H --> H1[BeginPass 到默认 framebuffer]
    H1 --> I[Skybox Pass]
    I --> I1{World.renderSettings.skyboxEnabled?}
    I1 -- 是 --> I2[上传/绑定 Skybox Cubemap]
    I2 --> I3[绘制 skybox cube]
    I1 -- 否 --> J[跳过 skybox]

    I3 --> K[Mesh Draw]
    J --> K

    K --> K1[GpuResourceManager::GetMaterial]
    K1 --> K2[绑定 shader program]
    K2 --> K3[设置 Blinn-Phong uniforms]
    K3 --> K4[绑定 diffuse texture / shadow map]
    K4 --> K5[绑定 vertexInput]
    K5 --> K6[DrawIndexed]
```

## 核心类关系

```mermaid
classDiagram
    class Application {
        World world
        List~IEngineSystem*~ systems
        RenderSystem* renderSystem
        Tick()
        Run()
    }

    class World {
        RenderSettings renderSettings
        ForEachComponent()
        CreateEns()
        CreateObject()
    }

    class RenderSettings {
        Ref~Skybox~ skybox
        bool skyboxEnabled
        color4 ambientColor
    }

    class RenderSystem {
        OpenGLRenderBackend backend
        GpuResourceManager resources
        ForwardPipeline forwardPipeline
        RenderSceneBuilder sceneBuilder
        SpaceCache spaceCache
        SceneCuller culler
        RenderItemSorter sorter
        RenderScene scene
        VisibleSet visibleSet
        Render()
    }

    class RenderSceneBuilder {
        Build()
    }

    class RenderScene {
        RenderSettings renderSettings
        List~RenderCamera~ cameras
        List~RenderDirectionalLight~ directionalLights
        List~RenderItem~ items
    }

    class VisibleSet {
        RenderCamera camera
        List~RenderItem~ items
    }

    class ForwardPipeline {
        Render()
        RenderShadowPass()
        RenderSkybox()
    }

    class GpuResourceManager {
        GetMesh()
        GetMaterial()
        GetShader()
        GetTexture()
        GetSkybox()
    }

    class RenderBackend {
        CreateVertexBuffer()
        CreateIndexBuffer()
        CreateVertexInput()
        CreateTexture()
        CreateDepthTexture()
        CreateCubeTexture()
        CreateRenderTarget()
        CreateShaderProgram()
        BeginPass()
        DrawIndexed()
    }

    class OpenGLRenderBackend {
        VBO/EBO/VAO
        Texture2D
        DepthTexture
        Cubemap
        FBO
        Program
    }

    Application --> World
    Application --> RenderSystem
    World --> RenderSettings
    RenderSystem --> RenderSceneBuilder
    RenderSystem --> RenderScene
    RenderSystem --> ForwardPipeline
    RenderSystem --> GpuResourceManager
    RenderSystem --> RenderBackend
    RenderBackend <|-- OpenGLRenderBackend
    RenderSceneBuilder --> World
    RenderSceneBuilder --> RenderScene
    ForwardPipeline --> GpuResourceManager
    ForwardPipeline --> RenderBackend
```

## 资源上传关系

```mermaid
flowchart LR
    A[ResourceManager / AssetPipeline] --> B[CPU Resources]
    B --> B1[Mesh]
    B --> B2[Material]
    B --> B3[MaterialShader]
    B --> B4[Texture2D]
    B --> B5[Skybox]

    B1 --> C[GpuResourceManager]
    B2 --> C
    B3 --> C
    B4 --> C
    B5 --> C

    C --> D[RenderBackend]
    D --> D1[GpuVertexBufferID]
    D --> D2[GpuIndexBufferID]
    D --> D3[GpuVertexInputID]
    D --> D4[GpuTextureID]
    D --> D5[GpuCubeTextureID]
    D --> D6[GpuShaderProgramID]
    D --> D7[GpuDepthTextureID]
    D --> D8[GpuRenderTargetID]

    D1 --> E[OpenGL VBO]
    D2 --> F[OpenGL EBO]
    D3 --> G[OpenGL VAO]
    D4 --> H[OpenGL Texture2D]
    D5 --> I[OpenGL Cubemap]
    D6 --> J[OpenGL Program]
    D7 --> K[OpenGL Depth Texture]
    D8 --> L[OpenGL FBO]
```

## 一句话总结

`World` 保存组件和全局环境，`RenderSceneBuilder` 将其整理成每帧 `RenderScene`，`SceneCuller` 和 `RenderItemSorter` 得到当前相机的 `VisibleSet`，`ForwardPipeline` 执行 shadow / skybox / main pass，`GpuResourceManager` 缓存 CPU 资源到 GPU ID 的上传结果，最终由 `RenderBackend` 抽象落到具体图形 API。

