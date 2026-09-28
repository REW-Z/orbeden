#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/EngineTypes.h"
#include "Runtime/Object/Object.h"

#include <string>

struct GpuShader;
class GpuResourceManager;

//Shader纹理维度，当前 GLSL 前端 v1 只反射 sampler2D
enum class ShaderTextureDimension
{
    Texture2D,
};

//Shader Pass 布尔状态，Auto 表示使用渲染管线基线
enum class ShaderPassToggle
{
    Auto,
    On,
    Off,
};

//Shader 几何 ABI 契约，决定一个 Pass 编译哪些几何变体。
//数值写入 Cooked 载荷，序号固定：0 是已删除的旧值，不允许重新解释为 Standard。
enum class ShaderGeometryContract : uint32
{
    //已删除的旧值：没有几何 ABI 的 Shader 不再受支持，读到 0 一律拒绝
    Removed = 0,
    //接入几何 ABI，编译 uniform、实例绘制，以及展开几何（supportsExpandedGeometry 关闭时除外）
    Standard = 1,
    //粒子用几何 ABI，在 Standard 之上再加拖尾实例
    Particle = 2,
};

//Shader Pass 固定功能状态
struct ShaderPassState
{
public:
    ShaderPassToggle depthTest = ShaderPassToggle::Auto;
    ShaderPassToggle depthWrite = ShaderPassToggle::Auto;
    ShaderPassToggle blend = ShaderPassToggle::Auto;
    CullMode cull = CullMode::Auto;
};

//Shader 的一个有序绘制 Pass
struct ShaderPass
{
public:
    std::string name = "Default";
    ShaderPassState state;
    //未声明 Geometry 时按 Standard 处理
    ShaderGeometryContract geometryContract = ShaderGeometryContract::Standard;
    //是否编译展开几何变体。依赖模型空间顶点动画的 Shader 用 expandedGeometry off 关闭，
    //关闭后该 Pass 不参与静态与动态合批，只走单绘制与实例绘制。
    bool supportsExpandedGeometry = true;
    ORBEDEN_BIND_ACCESSORS(Direct, None)
    std::string vertexSource;
    ORBEDEN_BIND_ACCESSORS(Direct, None)
    std::string fragmentSource;
};

//Shader 暴露给 Material 的纹理槽
struct ShaderTextureSlot
{
public:
    std::string name;
    std::string displayName;
    ShaderTextureDimension dimension = ShaderTextureDimension::Texture2D;
};

//Shader 暴露给 Material 的颜色槽
struct ShaderColorSlot
{
public:
    std::string name;
    std::string displayName;
    color defaultValue = { 1.0f, 1.0f, 1.0f, 1.0f };
};

//Shader 暴露给 Material 的浮点槽
struct ShaderFloatSlot
{
public:
    std::string name;
    std::string displayName;
    float32 defaultValue = 0.0f;
};

//CPU着色器资源，保存源码但不编译GPU程序
class Shader : public Object
{
    OBJECT_TYPE_DECLARE(Shader)

private:
    friend class GpuResourceManager;

    //GPU Shader 由资源管理器持有。
    GpuShader* gpuShader = nullptr;
    bool gpuDirty = true;

    //清除 GPU 刷新标记
    void ClearDirty();

public:
    std::string name;
    //材质未覆盖时使用的绘制队列
    DrawQueue drawQueue = DrawQueue::Opaque;
    std::string vertexPath;
    std::string fragmentPath;
    ORBEDEN_BIND_ACCESSORS(Direct, None)
    std::string vertexSource;
    ORBEDEN_BIND_ACCESSORS(Direct, None)
    std::string fragmentSource;
    ORBEDEN_BIND_ACCESSORS(Direct, ReplacePasses)
    List<ShaderPass> passes;
    ORBEDEN_BIND_ACCESSORS(Direct, None)
    List<ShaderTextureSlot> textureSlots;
    ORBEDEN_BIND_ACCESSORS(Direct, None)
    List<ShaderColorSlot> colorSlots;
    ORBEDEN_BIND_ACCESSORS(Direct, None)
    List<ShaderFloatSlot> floatSlots;

    //创建带运行时路径的 Shader，并刷新源码反射。
    static Shader* CreateFromSource(const std::string& name, const std::string& vertex, const std::string& fragment);

    //从 GLSL 源码刷新材质槽反射结果
    bool ReflectSlotsFromSource();

    //替换 GLSL 源码并刷新反射结果
    void ReplaceSource(const std::string& vertex, const std::string& fragment);

    //替换有序 Pass 列表并刷新兼容源码和材质槽
    bool ReplacePasses(const List<ShaderPass>& value);

    //获取 Pass 数量
    uint32 GetPassCount() const;

    //获取指定 Pass
    const ShaderPass* GetPass(uint32 index) const;

    //判断 GPU 数据是否需要刷新
    bool IsDirty() const;

    //标记 GPU 数据需要刷新
    void MarkDirty();
};
