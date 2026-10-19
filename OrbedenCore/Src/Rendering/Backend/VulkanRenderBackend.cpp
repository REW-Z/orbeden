#include "Rendering/Backend/VulkanRenderBackend.h"

#include "Log/Log.h"
#include "Platform/GlfwWindow.h"
#include "Rendering/ShaderCompiler.h"
#include "Rendering/VertexLayout.h"
#include "Rendering/DepthDistributionShader.h"
#include "Profiler/Profiler.h"

#include <glad/vulkan.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace
{
    constexpr uint32 FrameCount = 3;
    constexpr VkDeviceSize UploadPageSize = 8 * 1024 * 1024;
    constexpr uint32 TimestampCapacity = 256;

    //检查 Vulkan 调用结果
    void Check(VkResult result, const char* operation)
    {
        if (result == VK_SUCCESS) return;
        std::string message = std::string(operation) + " failed: VkResult=" + std::to_string(result);
        Log::Error(message.c_str());
        throw std::runtime_error(message);
    }

    //计算指定对齐的字节位置
    VkDeviceSize Align(VkDeviceSize value, VkDeviceSize alignment)
    {
        return (value + alignment - 1) / alignment * alignment;
    }
}

//保存 Vulkan 设备、帧与资源
struct VulkanRenderBackend::State
{
    struct Buffer
    {
        VkBuffer handle = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        uint8* mapped = nullptr;
        VkDeviceSize offset = 0;
        VkDeviceSize capacity = 0;
        VkDeviceSize used = 0;
        bool owned = true;
        bool stream = false;
        List<uint8> contents;
        uint64 uploadSerial = 0;
    };
    struct VertexInput
    {
        GpuVertexInputDesc desc;
        Buffer* vertices = nullptr;
        Buffer* indices = nullptr;
        uint32 instanceStride = 0;
    };
    struct Image
    {
        VkImage handle = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
        VkAccessFlags2 access = VK_ACCESS_2_NONE;
        uint32 width = 0, height = 0, levels = 1, layers = 1;
        uint32 uploadChannels = 0;
        bool initialized = false;
        bool owned = true;
    };
    struct Target { uint32 color = 0, depth = 0; uint32 width = 0, height = 0; };
    struct GraphicsPipelineKey
    {
        GpuVertexLayout layout = GpuVertexLayout::Mesh;
        uint32 stride = 0;
        VkFormat color = VK_FORMAT_UNDEFINED, depth = VK_FORMAT_UNDEFINED;
        bool depthTest = true, depthWrite = true, blend = false, depthBias = false, offscreen = false;
        DepthCompare depthCompare = DepthCompare::Less;
        BlendMode blendMode = BlendMode::Alpha;
        CullMode cull = CullMode::None;
        PolygonMode polygonMode = PolygonMode::Fill;
        PrimitiveTopology topology = PrimitiveTopology::Triangles;

        //比较图形管线的固定状态
        bool operator==(const GraphicsPipelineKey&) const = default;
    };
    struct Program
    {
        struct ParameterBinding
        {
            uint32 offset = 0, size = 0;
            ShaderParameterType type = ShaderParameterType::Float;
        };
        CompiledShader compiled;
        VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE, compute = VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptors = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        std::unordered_map<std::string, VkPipeline> pipelines;
        Buffer parameterData;
        uint64 parameterSerial = 0, parameterRevision = 0;
        uint64 uploadedParameterRevision = 0, descriptorSerial = 0;
        List<uint8> parameterBytes;
        List<ParameterBinding> parameterBindings;
        List<const ShaderParameterValue*> samplerParameters;
        List<VkDescriptorImageInfo> imageInfos;
        std::unordered_map<std::string, VkDescriptorSet> descriptorSets;
        std::string descriptorKey, pipelineKey;
        bool parametersRegistered = false;
        GraphicsPipelineKey lastPipelineKey;
        VkPipeline lastPipeline = VK_NULL_HANDLE;
        VkDescriptorSet lastDescriptors = VK_NULL_HANDLE;
        VkBuffer lastParameterBuffer = VK_NULL_HANDLE;
        VkDescriptorBufferInfo lastStorage = {};
        uint64 textureBindingsRevision = 0, imageAccessRevision = 0;
        const ShaderParameterValue* framebufferParameter = nullptr;
        uint64 framebufferInfoRevision = 0, framebufferParameterRevision = 0;
    };
    struct DrawBindings
    {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkDescriptorSet descriptors = VK_NULL_HANDLE;
        uint32 parameterCount = 0, parameterOffset = 0;
        std::array<VkBuffer, 2> vertexBuffers = {};
        std::array<VkDeviceSize, 2> vertexOffsets = {};
        uint32 vertexCount = 0;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceSize indexOffset = 0;
        bool depthBiasSet = false;
        float32 depthBiasSlope = 0.0f, depthBiasConstant = 0.0f;
    };
    struct Frame
    {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer commands = VK_NULL_HANDLE;
        List<VkCommandBuffer> commandBuffers;
        VkFence fence = VK_NULL_HANDLE;
        List<VkDescriptorPool> descriptorPools;
        uint32 descriptorPoolIndex = 0;
        List<Buffer> uploads;
        List<std::function<void()>> retired;
        uint64 serial = 0;
        VkQueryPool timestamps = VK_NULL_HANDLE;
        std::array<int32, TimestampCapacity> timestampNames = {};
        uint32 timestampCount = 0;
        int32 droppedTimestamps = 0;
        int64 profileFrameIndex = -1;
        uint64 profileGeneration = 0;
        bool timestampsSubmitted = false;
    };
    struct Surface
    {
        IWindow* window = nullptr;
        VkSurfaceKHR handle = VK_NULL_HANDLE;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent2D extent = {};
        List<Image> images;
        List<VkSemaphore> finished;
        std::array<VkSemaphore, FrameCount> available = {};
        uint32 imageIndex = 0;
        bool acquired = false;
        bool waitPending = false;
        bool rebuild = false;
        uint32 depth = 0;
    };
    struct Distribution
    {
        Buffer bins;
        uint64 serial = 0;
        float32 nearPlane = 0.1f, farPlane = 1000.0f;
    };

    VulkanRenderBackend& backend;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32 queueFamily = 0;
    uint32 timestampValidBits = 0;
    VkPhysicalDeviceProperties properties = {};
    VkPhysicalDeviceMemoryProperties memoryProperties = {};
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    std::array<Frame, FrameCount> frames;
    uint32 frameIndex = FrameCount - 1;
    uint64 submittedSerial = 0, completedSerial = 0;
    bool recording = false, rendering = false, passActive = false;
    bool commandsPending = false;
    DrawBindings drawBindings;
    uint64 textureBindingsRevision = 0, imageAccessRevision = 0;
    RenderPassDesc pass;
    Image* passColor = nullptr;
    Image* passDepth = nullptr;
    bool passAttachmentsCached = false;
    vector3 framebufferInfo = {};
    uint64 framebufferInfoRevision = 1;
    uint32 currentSurface = 0;
    uint32 nextId = 1;
    uint32 currentProgram = 0, currentInput = 0, instanceBuffer = 0;
    Program* programBinding = nullptr;
    const VertexInput* inputBinding = nullptr;
    Buffer* instanceBinding = nullptr;
    usize instanceOffset = 0;
    bool scissorEnabled = false;
    VkRect2D scissor = {};
    std::unordered_map<uint32, Surface> surfaces;
    std::unordered_map<uint32, Buffer> buffers;
    std::unordered_map<uint32, Image> images;
    std::unordered_map<uint32, Target> targets;
    std::unordered_map<uint32, VertexInput> inputs;
    std::unordered_map<uint32, Program> programs;
    std::unordered_map<uint32, uint32> textures;
    std::unordered_map<uint32, Distribution> distributions;
    uint32 whiteTexture = 0, blackCube = 0, distributionProgram = 0;

    //绑定后端接口
    explicit State(VulkanRenderBackend& value) : backend(value) {}

    //写入帧内 GPU 时间检查点
    void WriteTimestamp(const char* name, bool final = false)
    {
        Frame& frame = frames[frameIndex];
        if (frame.profileFrameIndex < 0 || !frame.timestamps) return;
        if (frame.timestampCount >= TimestampCapacity - (final ? 0u : 1u))
        {
            ++frame.droppedTimestamps;
            return;
        }
        frame.timestampNames[frame.timestampCount] = Profiler::GetGpuNameId(name);
        vkCmdWriteTimestamp2(RecordCommands(), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            frame.timestamps, frame.timestampCount++);
    }

    //读取已完成帧的 GPU 检查点区间
    void ReadTimestamps(Frame& frame)
    {
        if (!frame.timestampsSubmitted) return;
        struct TimestampResult { uint64 ticks = 0, available = 0; };
        std::array<TimestampResult, TimestampCapacity> results;
        VkResult result = vkGetQueryPoolResults(device, frame.timestamps, 0, frame.timestampCount,
            sizeof(TimestampResult) * frame.timestampCount, results.data(), sizeof(TimestampResult),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (result != VK_NOT_READY) Check(result, "vkGetQueryPoolResults(timestamps)");
        std::array<ProfileGpuEvent, TimestampCapacity> events;
        int32 count = 0;
        float64 elapsed = 0;
        uint64 mask = timestampValidBits == 64 ? UINT64_MAX : (uint64{1} << timestampValidBits) - 1;
        if (result == VK_SUCCESS)
        {
            for (uint32 index = 1; index < frame.timestampCount; ++index)
            {
                if (!results[index - 1].available || !results[index].available) { count = 0; break; }
                uint64 ticks = (results[index].ticks - results[index - 1].ticks) & mask;
                float64 duration = static_cast<float64>(ticks) * properties.limits.timestampPeriod / 1000.0;
                events[count++] = { frame.timestampNames[index], elapsed, duration };
                elapsed += duration;
            }
        }
        Profiler::StoreGpuEvents(frame.profileGeneration, frame.profileFrameIndex,
            events.data(), count, frame.droppedTimestamps);
        frame.timestampsSubmitted = false;
    }

    //获取并标记当前录制的命令缓冲
    VkCommandBuffer RecordCommands()
    {
        commandsPending = true;
        return frames[frameIndex].commands;
    }

    //选择满足用途的内存类型
    uint32 FindMemory(uint32 mask, VkMemoryPropertyFlags flags)
    {
        for (uint32 index = 0; index < memoryProperties.memoryTypeCount; ++index)
            if ((mask & (1u << index)) && (memoryProperties.memoryTypes[index].propertyFlags & flags) == flags) return index;
        throw std::runtime_error("Vulkan memory type is unavailable.");
    }

    //分配缓冲与内存
    Buffer AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host)
    {
        Buffer result;
        result.capacity = size;
        VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        info.size = size;
        info.usage = usage;
        Check(vkCreateBuffer(device, &info, nullptr, &result.handle), "vkCreateBuffer");
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device, result.handle, &requirements);
        VkMemoryAllocateInfo allocation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = FindMemory(requirements.memoryTypeBits, host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        Check(vkAllocateMemory(device, &allocation, nullptr, &result.memory), "vkAllocateMemory(buffer)");
        Check(vkBindBufferMemory(device, result.handle, result.memory, 0), "vkBindBufferMemory");
        if (host) Check(vkMapMemory(device, result.memory, 0, size, 0, reinterpret_cast<void**>(&result.mapped)), "vkMapMemory");
        return result;
    }

    //释放缓冲与内存
    void DestroyBuffer(Buffer buffer)
    {
        if (!buffer.owned) return;
        if (buffer.mapped) vkUnmapMemory(device, buffer.memory);
        if (buffer.handle) vkDestroyBuffer(device, buffer.handle, nullptr);
        if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
    }

    //分配帧内上传区间
    Buffer Upload(const void* data, VkDeviceSize size, VkDeviceSize alignment = 16)
    {
        backend.BeginFrame();
        Frame& frame = frames[frameIndex];
        Buffer* page = nullptr;
        for (Buffer& candidate : frame.uploads)
            if (Align(candidate.used, alignment) + size <= candidate.capacity) { page = &candidate; break; }
        if (!page)
        {
            constexpr VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            page = &frame.uploads.emplace_back(AllocateBuffer(std::max(size, UploadPageSize), usage, true));
        }
        Buffer result = *page;
        result.offset = Align(page->used, alignment);
        result.mapped += result.offset;
        result.capacity = size;
        result.owned = false;
        page->used = result.offset + size;
        if (data) std::memcpy(result.mapped, data, static_cast<usize>(size));
        else std::memset(result.mapped, 0, static_cast<usize>(size));
        return result;
    }

    //延迟释放仍可能被 GPU 使用的对象
    void Retire(std::function<void()> release)
    {
        if (recording) frames[frameIndex].retired.push_back(std::move(release));
        else
        {
            Check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle(retire)");
            release();
        }
    }

    //刷新跨帧复用的流式缓冲快照
    void RefreshBuffer(Buffer& buffer)
    {
        if (!buffer.stream || buffer.uploadSerial == submittedSerial + 1 || buffer.contents.empty()) return;
        Buffer snapshot = Upload(buffer.contents.data(), buffer.used);
        snapshot.stream = true; snapshot.used = buffer.used; snapshot.capacity = buffer.capacity;
        snapshot.contents = std::move(buffer.contents); snapshot.uploadSerial = submittedSerial + 1;
        if (buffer.owned && buffer.handle)
        {
            Buffer previous = buffer;
            Retire([this, previous] { DestroyBuffer(previous); });
        }
        buffer = std::move(snapshot);
    }

    //结束当前渲染范围
    void PauseRendering()
    {
        if (!rendering) return;
        vkCmdEndRendering(RecordCommands());
        rendering = false;
    }

    //切换图像布局与访问依赖
    void UseImage(Image& image, VkImageLayout layout, VkPipelineStageFlags2 stage, VkAccessFlags2 access)
    {
        constexpr VkAccessFlags2 writes = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        if (image.layout == layout && image.stage == stage && image.access == access && !(access & writes)) return;
        backend.BeginFrame();
        PauseRendering();
        VkImageMemoryBarrier2 barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        barrier.srcStageMask = image.stage;
        barrier.srcAccessMask = image.access;
        barrier.dstStageMask = stage;
        barrier.dstAccessMask = access;
        barrier.oldLayout = image.layout;
        barrier.newLayout = layout;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.handle;
        barrier.subresourceRange = { image.aspect, 0, image.levels, 0, image.layers };
        VkDependencyInfo dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(RecordCommands(), &dependency);
        image.layout = layout;
        image.stage = stage;
        image.access = access;
        ++imageAccessRevision;
    }

    //创建采样图像与视图
    Image AllocateImage(uint32 width, uint32 height, VkFormat format, bool depth, uint32 layers = 1, uint32 levels = 1, bool linear = false, bool repeat = false)
    {
        Image result;
        result.width = width; result.height = height; result.layers = layers; result.levels = levels; result.format = format;
        result.aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        info.flags = layers == 6 ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = { width, height, 1 };
        info.mipLevels = levels; info.arrayLayers = layers;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | (depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        Check(vkCreateImage(device, &info, nullptr, &result.handle), "vkCreateImage");
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device, result.handle, &requirements);
        VkMemoryAllocateInfo allocation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = FindMemory(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        Check(vkAllocateMemory(device, &allocation, nullptr, &result.memory), "vkAllocateMemory(image)");
        Check(vkBindImageMemory(device, result.handle, result.memory, 0), "vkBindImageMemory");
        VkImageViewCreateInfo view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        view.image = result.handle; view.viewType = layers == 6 ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
        view.format = format; view.subresourceRange = { result.aspect, 0, levels, 0, layers };
        Check(vkCreateImageView(device, &view, nullptr, &result.view), "vkCreateImageView");
        VkSamplerCreateInfo sampler = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        sampler.magFilter = sampler.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        sampler.mipmapMode = linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.maxLod = static_cast<float32>(levels - 1);
        Check(vkCreateSampler(device, &sampler, nullptr, &result.sampler), "vkCreateSampler");
        return result;
    }

    //释放图像附属对象
    void DestroyImage(Image image)
    {
        if (image.sampler) vkDestroySampler(device, image.sampler, nullptr);
        if (image.view) vkDestroyImageView(device, image.view, nullptr);
        if (image.owned && image.handle) vkDestroyImage(device, image.handle, nullptr);
        if (image.memory) vkFreeMemory(device, image.memory, nullptr);
    }

    //创建或更新窗口交换链
    bool BuildSwapchain(Surface& surface)
    {
        uint32 width = static_cast<uint32>(std::max(surface.window->GetFramebufferWidth(), 0));
        uint32 height = static_cast<uint32>(std::max(surface.window->GetFramebufferHeight(), 0));
        if (!width || !height) return false;
        backend.WaitForIdle();
        ClearPassAttachments();
        VkSurfaceCapabilitiesKHR capabilities;
        Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface.handle, &capabilities), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        uint32 count = 0;
        Check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface.handle, &count, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR");
        List<VkSurfaceFormatKHR> formats(count);
        Check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface.handle, &count, formats.data()), "vkGetPhysicalDeviceSurfaceFormatsKHR");
        auto format = std::find_if(formats.begin(), formats.end(), [](const auto& value) { return (value.format == VK_FORMAT_B8G8R8A8_UNORM || value.format == VK_FORMAT_R8G8B8A8_UNORM) && value.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; });
        if (format == formats.end()) throw std::runtime_error("Vulkan presentation requires an UNORM SDR surface for the engine display encoding.");
        surface.format = format->format;
        surface.extent = capabilities.currentExtent.width != UINT32_MAX ? capabilities.currentExtent : VkExtent2D{ std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width), std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height) };
        Check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface.handle, &count, nullptr), "vkGetPhysicalDeviceSurfacePresentModesKHR");
        List<VkPresentModeKHR> modes(count);
        Check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface.handle, &count, modes.data()), "vkGetPhysicalDeviceSurfacePresentModesKHR");
        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
        if (!surface.window->GetVsync())
        {
            if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end()) mode = VK_PRESENT_MODE_MAILBOX_KHR;
            else if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end()) mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
        VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if ((capabilities.supportedUsageFlags & usage) != usage) throw std::runtime_error("Vulkan surface cannot support engine framebuffer copies.");
        VkSwapchainCreateInfoKHR info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
        info.surface = surface.handle;
        info.minImageCount = std::max(capabilities.minImageCount, FrameCount);
        if (capabilities.maxImageCount) info.minImageCount = std::min(info.minImageCount, capabilities.maxImageCount);
        info.imageFormat = surface.format; info.imageColorSpace = format->colorSpace;
        info.imageExtent = surface.extent; info.imageArrayLayers = 1; info.imageUsage = usage;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; info.preTransform = capabilities.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if (!(capabilities.supportedCompositeAlpha & info.compositeAlpha)) info.compositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(capabilities.supportedCompositeAlpha & (~capabilities.supportedCompositeAlpha + 1u));
        info.presentMode = mode; info.clipped = VK_TRUE; info.oldSwapchain = surface.swapchain;
        VkSwapchainKHR swapchain;
        Check(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain), "vkCreateSwapchainKHR");
        for (Image& image : surface.images) DestroyImage(image);
        for (VkSemaphore semaphore : surface.finished) vkDestroySemaphore(device, semaphore, nullptr);
        if (surface.swapchain) vkDestroySwapchainKHR(device, surface.swapchain, nullptr);
        surface.swapchain = swapchain;
        Check(vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr), "vkGetSwapchainImagesKHR");
        List<VkImage> handles(count);
        Check(vkGetSwapchainImagesKHR(device, swapchain, &count, handles.data()), "vkGetSwapchainImagesKHR");
        surface.images.assign(count, {});
        surface.finished.resize(count);
        VkSemaphoreCreateInfo semaphore = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        for (uint32 index = 0; index < count; ++index)
        {
            Image& image = surface.images[index]; image.handle = handles[index]; image.owned = false;
            image.width = surface.extent.width; image.height = surface.extent.height; image.format = surface.format;
            VkImageViewCreateInfo view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            view.image = image.handle; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = image.format;
            view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            Check(vkCreateImageView(device, &view, nullptr, &image.view), "vkCreateImageView(swapchain)");
            Check(vkCreateSemaphore(device, &semaphore, nullptr, &surface.finished[index]), "vkCreateSemaphore(present)");
        }
        if (surface.depth) backend.DeleteDepthTexture({ surface.depth });
        surface.depth = nextId++;
        images.emplace(surface.depth, AllocateImage(surface.extent.width, surface.extent.height, VK_FORMAT_D32_SFLOAT, true));
        surface.rebuild = false;
        return true;
    }

    //获取当前窗口可绘制图像
    Image* AcquireSurface()
    {
        backend.BeginFrame();
        Surface& surface = surfaces.at(currentSurface);
        if (surface.acquired) return &surface.images[surface.imageIndex];
        if (surface.rebuild || !surface.swapchain || surface.extent.width != static_cast<uint32>(std::max(surface.window->GetFramebufferWidth(), 0)) || surface.extent.height != static_cast<uint32>(std::max(surface.window->GetFramebufferHeight(), 0)))
            if (!BuildSwapchain(surface)) return nullptr;
        VkResult result = vkAcquireNextImageKHR(device, surface.swapchain, UINT64_MAX, surface.available[frameIndex], VK_NULL_HANDLE, &surface.imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) { surface.rebuild = true; return AcquireSurface(); }
        if (result == VK_SUBOPTIMAL_KHR) surface.rebuild = true;
        else Check(result, "vkAcquireNextImageKHR");
        surface.acquired = true; surface.waitPending = true;
        return &surface.images[surface.imageIndex];
    }

    //获取目标附件
    std::pair<Image*, Image*> GetAttachments(GpuRenderTargetID id)
    {
        if (!id.IsValid())
        {
            Image* color = AcquireSurface();
            return { color, color ? &images.at(surfaces.at(currentSurface).depth) : nullptr };
        }
        auto target = targets.find(id.id);
        if (target == targets.end()) return {};
        return { target->second.color ? &images.at(target->second.color) : nullptr, target->second.depth ? &images.at(target->second.depth) : nullptr };
    }

    //清空当前 Pass 的附件引用
    void ClearPassAttachments()
    {
        passColor = passDepth = nullptr;
        passAttachmentsCached = false;
    }

    //缓存当前 Pass 的附件与帧缓冲参数
    void CachePassAttachments()
    {
        if (passAttachmentsCached) return;
        auto [color, depth] = GetAttachments(pass.renderTarget);
        passColor = color; passDepth = depth;
        passAttachmentsCached = color || depth;
        Image* extent = color ? color : depth;
        vector3 info = { extent ? static_cast<float32>(extent->height) : 0.0f, pass.renderTarget.IsValid() ? 0.0f : 1.0f, 0.0f };
        if (info.x != framebufferInfo.x || info.y != framebufferInfo.y)
        {
            framebufferInfo = info;
            ++framebufferInfoRevision;
        }
    }

    //开始或恢复当前渲染范围
    bool StartRendering()
    {
        if (rendering) return true;
        if (!passActive || pass.width <= 0 || pass.height <= 0) return false;
        CachePassAttachments();
        Image* color = passColor;
        Image* depth = passDepth;
        if (!color && !depth) return false;
        if (color) UseImage(*color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        if (depth) UseImage(*depth, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        VkRenderingAttachmentInfo colorInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        VkRenderingAttachmentInfo depthInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        if (color)
        {
            colorInfo.imageView = color->view; colorInfo.imageLayout = color->layout;
            colorInfo.loadOp = color->initialized ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE; color->initialized = true;
        }
        if (depth)
        {
            depthInfo.imageView = depth->view; depthInfo.imageLayout = depth->layout;
            depthInfo.loadOp = depth->initialized ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE; depthInfo.clearValue.depthStencil.depth = 1.0f; depth->initialized = true;
        }
        Image& extent = color ? *color : *depth;
        VkRenderingInfo info = { VK_STRUCTURE_TYPE_RENDERING_INFO };
        info.renderArea.extent = { extent.width, extent.height }; info.layerCount = 1;
        if (color) { info.colorAttachmentCount = 1; info.pColorAttachments = &colorInfo; }
        if (depth) info.pDepthAttachment = &depthInfo;
        vkCmdBeginRendering(RecordCommands(), &info);
        rendering = true;
        VkViewport viewport = { static_cast<float32>(pass.x), static_cast<float32>(pass.y), static_cast<float32>(pass.width), static_cast<float32>(pass.height), 0.0f, 1.0f };
        if (!pass.renderTarget.IsValid()) { viewport.y = static_cast<float32>(extent.height) - viewport.y; viewport.height = -viewport.height; }
        vkCmdSetViewport(RecordCommands(), 0, 1, &viewport);
        VkRect2D rect = { { pass.x, pass.renderTarget.IsValid() ? pass.y : static_cast<int32>(extent.height) - pass.y - pass.height }, { static_cast<uint32>(pass.width), static_cast<uint32>(pass.height) } };
        vkCmdSetScissor(RecordCommands(), 0, 1, scissorEnabled ? &scissor : &rect);
        if (pass.clearMode != ClearMode::None)
        {
            VkClearAttachment attachments[2] = {};
            uint32 count = 0;
            if (color && (pass.clearMode == ClearMode::SolidColor || pass.clearMode == ClearMode::ColorOnly))
            {
                auto& clear = attachments[count++]; clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                clear.clearValue.color = { { pass.clearColor.r, pass.clearColor.g, pass.clearColor.b, pass.clearColor.a } };
            }
            if (depth && (pass.clearMode == ClearMode::SolidColor || pass.clearMode == ClearMode::DepthOnly))
            {
                auto& clear = attachments[count++]; clear.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT; clear.clearValue.depthStencil.depth = 1.0f;
            }
            VkClearRect clear = { rect, 0, 1 };
            if (count) vkCmdClearAttachments(RecordCommands(), count, attachments, 1, &clear);
            pass.clearMode = ClearMode::None;
        }
        return true;
    }

    //提交当前命令片段并继续录制
    void Flush()
    {
        if (!recording || !commandsPending) return;
        PauseRendering();
        Frame& frame = frames[frameIndex];
        Check(vkEndCommandBuffer(frame.commands), "vkEndCommandBuffer(flush)");
        List<VkSemaphoreSubmitInfo> waits;
        for (auto& [id, surface] : surfaces) if (surface.waitPending)
        {
            VkSemaphoreSubmitInfo wait = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
            wait.semaphore = surface.available[frameIndex]; wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            waits.push_back(wait); surface.waitPending = false;
        }
        VkCommandBufferSubmitInfo commands = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO }; commands.commandBuffer = frame.commands;
        VkSubmitInfo2 info = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
        info.waitSemaphoreInfoCount = static_cast<uint32>(waits.size()); info.pWaitSemaphoreInfos = waits.data();
        info.commandBufferInfoCount = 1; info.pCommandBufferInfos = &commands;
        Check(vkQueueSubmit2(queue, 1, &info, VK_NULL_HANDLE), "vkQueueSubmit2(flush)");
        Check(vkQueueWaitIdle(queue), "vkQueueWaitIdle(flush)");
        VkCommandBufferAllocateInfo allocation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        allocation.commandPool = frame.pool; allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocation.commandBufferCount = 1;
        Check(vkAllocateCommandBuffers(device, &allocation, &frame.commands), "vkAllocateCommandBuffers(flush)");
        frame.commandBuffers.push_back(frame.commands);
        VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO }; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Check(vkBeginCommandBuffer(frame.commands, &begin), "vkBeginCommandBuffer(flush)");
        commandsPending = false;
        drawBindings = {};
        completedSerial = submittedSerial;
    }

    //分配一组帧内资源描述
    VkDescriptorSet AllocateDescriptors(VkDescriptorSetLayout layout)
    {
        Frame& frame = frames[frameIndex];
        for (;;)
        {
            if (frame.descriptorPoolIndex == frame.descriptorPools.size())
            {
                VkDescriptorPoolSize sizes[] = { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1024 }, { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16384 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024 } };
                VkDescriptorPoolCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
                info.maxSets = 1024; info.poolSizeCount = 3; info.pPoolSizes = sizes;
                VkDescriptorPool pool;
                Check(vkCreateDescriptorPool(device, &info, nullptr, &pool), "vkCreateDescriptorPool");
                frame.descriptorPools.push_back(pool);
            }
            VkDescriptorSetAllocateInfo allocation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            allocation.descriptorPool = frame.descriptorPools[frame.descriptorPoolIndex]; allocation.descriptorSetCount = 1; allocation.pSetLayouts = &layout;
            VkDescriptorSet result;
            VkResult status = vkAllocateDescriptorSets(device, &allocation, &result);
            if (status == VK_SUCCESS) return result;
            if (status != VK_ERROR_OUT_OF_POOL_MEMORY && status != VK_ERROR_FRAGMENTED_POOL) Check(status, "vkAllocateDescriptorSets");
            ++frame.descriptorPoolIndex;
        }
    }

    //创建着色器模块与资源布局
    uint32 CreateProgram(CompiledShader compiled)
    {
        Program program;
        program.compiled = std::move(compiled);
        auto module = [&](const List<uint32>& code, VkShaderModule& output)
        {
            if (code.empty()) return;
            VkShaderModuleCreateInfo info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO }; info.codeSize = code.size() * sizeof(uint32); info.pCode = code.data();
            Check(vkCreateShaderModule(device, &info, nullptr, &output), "vkCreateShaderModule");
        };
        module(program.compiled.vertex, program.vertex); module(program.compiled.fragment, program.fragment); module(program.compiled.compute, program.compute);
        VkShaderStageFlags stages = program.compute ? VK_SHADER_STAGE_COMPUTE_BIT : VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        List<VkDescriptorSetLayoutBinding> bindings;
        if (program.compiled.parameterSize) bindings.push_back({ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, stages, nullptr });
        for (const auto& sampler : program.compiled.samplers) bindings.push_back({ sampler.binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, stages, nullptr });
        if (program.compute) bindings.push_back({ 31, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages, nullptr });
        VkDescriptorSetLayoutCreateInfo descriptors = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        descriptors.bindingCount = static_cast<uint32>(bindings.size()); descriptors.pBindings = bindings.data();
        Check(vkCreateDescriptorSetLayout(device, &descriptors, nullptr, &program.descriptors), "vkCreateDescriptorSetLayout");
        VkPipelineLayoutCreateInfo layout = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO }; layout.setLayoutCount = 1; layout.pSetLayouts = &program.descriptors;
        Check(vkCreatePipelineLayout(device, &layout, nullptr, &program.layout), "vkCreatePipelineLayout");
        uint32 id = nextId++;
        programs.emplace(id, std::move(program));
        return id;
    }

    //释放着色器模块与管线
    void DestroyProgram(Program program)
    {
        for (auto& [key, pipeline] : program.pipelines) vkDestroyPipeline(device, pipeline, nullptr);
        if (program.vertex) vkDestroyShaderModule(device, program.vertex, nullptr);
        if (program.fragment) vkDestroyShaderModule(device, program.fragment, nullptr);
        if (program.compute) vkDestroyShaderModule(device, program.compute, nullptr);
        if (program.layout) vkDestroyPipelineLayout(device, program.layout, nullptr);
        if (program.descriptors) vkDestroyDescriptorSetLayout(device, program.descriptors, nullptr);
    }

    //生成纹理的逐级缩小图
    void GenerateMipmaps(Image& image);
    //准备当前绘制的资源描述
    VkDescriptorSet PrepareDescriptors(Program& program, Buffer* storage = nullptr);
    //获取当前固定功能状态对应的图形管线
    VkPipeline GetGraphicsPipeline(Program& program, const GpuVertexInputDesc& input);
    //提交索引图元
    void Draw(uint32 indexStart, uint32 indexCount, uint32 instanceCount);
};

//创建后端状态
VulkanRenderBackend::VulkanRenderBackend() : state(std::make_unique<State>(*this)) {}

//释放后端状态
VulkanRenderBackend::~VulkanRenderBackend() { Shutdown(); }

//创建设备与主窗口表面
bool VulkanRenderBackend::Initialize(IWindow* window)
{
    auto& s = *state;
    auto platform = dynamic_cast<GlfwWindow*>(window);
    if (!platform || !platform->GetGlfwWindow() || window->GetGraphicsApi() != WindowGraphicsApi::Vulkan) return false;
    try
    {
        if (!gladLoaderLoadVulkan(nullptr, nullptr, nullptr)) throw std::runtime_error("Vulkan loader is unavailable.");
        uint32 count = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&count);
        if (!extensions || !count) throw std::runtime_error("GLFW Vulkan surface extensions are unavailable.");
        VkApplicationInfo application = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
        application.pApplicationName = "Orbeden"; application.pEngineName = "Orbeden"; application.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo instance = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        instance.pApplicationInfo = &application; instance.enabledExtensionCount = count; instance.ppEnabledExtensionNames = extensions;
        Check(vkCreateInstance(&instance, nullptr, &s.instance), "vkCreateInstance");
        if (!gladLoaderLoadVulkan(s.instance, nullptr, nullptr)) throw std::runtime_error("Vulkan instance entry points are unavailable.");
        State::Surface surface; surface.window = window;
        Check(glfwCreateWindowSurface(s.instance, platform->GetGlfwWindow(), nullptr, &surface.handle), "glfwCreateWindowSurface");
        s.surfaces.emplace(0, std::move(surface));
        Check(vkEnumeratePhysicalDevices(s.instance, &count, nullptr), "vkEnumeratePhysicalDevices");
        List<VkPhysicalDevice> candidates(count);
        Check(vkEnumeratePhysicalDevices(s.instance, &count, candidates.data()), "vkEnumeratePhysicalDevices");
        int32 bestScore = -1;
        for (VkPhysicalDevice candidate : candidates)
        {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(candidate, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_3) continue;
            VkPhysicalDeviceVulkan13Features features13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
            VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 }; features.pNext = &features13;
            vkGetPhysicalDeviceFeatures2(candidate, &features);
            if (!features13.dynamicRendering || !features13.synchronization2 || !features.features.fillModeNonSolid) continue;
            uint32 familyCount;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
            List<VkQueueFamilyProperties> families(familyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
            for (uint32 family = 0; family < familyCount; ++family)
            {
                VkBool32 present;
                Check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, family, s.surfaces.at(0).handle, &present), "vkGetPhysicalDeviceSurfaceSupportKHR");
                if (!present || (families[family].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) continue;
                int32 score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
                if (score > bestScore) { bestScore = score; s.physical = candidate; s.queueFamily = family; s.properties = properties; s.timestampValidBits = families[family].timestampValidBits; }
            }
        }
        if (!s.physical) throw std::runtime_error("Vulkan 1.3 device with graphics, compute, presentation and wireframe support was not found.");
        vkGetPhysicalDeviceMemoryProperties(s.physical, &s.memoryProperties);
        float32 priority = 1.0f;
        VkDeviceQueueCreateInfo queue = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        queue.queueFamilyIndex = s.queueFamily; queue.queueCount = 1; queue.pQueuePriorities = &priority;
        VkPhysicalDeviceVulkan13Features features13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        features13.dynamicRendering = VK_TRUE; features13.synchronization2 = VK_TRUE;
        VkPhysicalDeviceFeatures features = {}; features.fillModeNonSolid = VK_TRUE;
        const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        VkDeviceCreateInfo device = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        device.pNext = &features13; device.pEnabledFeatures = &features; device.queueCreateInfoCount = 1; device.pQueueCreateInfos = &queue;
        device.enabledExtensionCount = 1; device.ppEnabledExtensionNames = deviceExtensions;
        Check(vkCreateDevice(s.physical, &device, nullptr, &s.device), "vkCreateDevice");
        if (!gladLoaderLoadVulkan(s.instance, s.physical, s.device)) throw std::runtime_error("Vulkan device entry points are unavailable.");
        vkGetDeviceQueue(s.device, s.queueFamily, 0, &s.queue);
        VkPipelineCacheCreateInfo cache = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
        Check(vkCreatePipelineCache(s.device, &cache, nullptr, &s.pipelineCache), "vkCreatePipelineCache");
        for (auto& frame : s.frames)
        {
            VkCommandPoolCreateInfo pool = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
            pool.queueFamilyIndex = s.queueFamily; pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            Check(vkCreateCommandPool(s.device, &pool, nullptr, &frame.pool), "vkCreateCommandPool");
            VkFenceCreateInfo fence = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO }; fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            Check(vkCreateFence(s.device, &fence, nullptr, &frame.fence), "vkCreateFence");
        }
        VkSemaphoreCreateInfo semaphore = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        for (auto& available : s.surfaces.at(0).available) Check(vkCreateSemaphore(s.device, &semaphore, nullptr, &available), "vkCreateSemaphore(acquire)");
        s.BuildSwapchain(s.surfaces.at(0));
        SetMaxTextureSize(static_cast<int32>(s.properties.limits.maxImageDimension2D));
        uint8 white[] = { 255, 255, 255, 255 }, black[] = { 0, 0, 0, 255 };
        s.whiteTexture = CreateTexture({ 1, 1, 4, white }).id;
        GpuCubeTextureDesc cube; cube.width = cube.height = 1; cube.channels = 4;
        for (auto& face : cube.faces) face = black;
        s.blackCube = CreateCubeTexture(cube).id;
        Log::Info((std::string("Vulkan renderer: ") + s.properties.deviceName + " | Vulkan 1.3").c_str());
        return true;
    }
    catch (const std::exception& error)
    {
        Log::Error((std::string("Vulkan initialize failed: ") + error.what()).c_str());
        Shutdown();
        return false;
    }
}

//释放全部设备资源
void VulkanRenderBackend::Shutdown()
{
    if (!state) return;
    auto& s = *state;
    if (s.device)
    {
        s.PauseRendering();
        vkDeviceWaitIdle(s.device);
        for (auto& frame : s.frames)
        {
            s.ReadTimestamps(frame);
            if (frame.profileFrameIndex >= 0 && !frame.timestampsSubmitted && s.recording && &frame == &s.frames[s.frameIndex])
                Profiler::StoreGpuEvents(frame.profileGeneration, frame.profileFrameIndex, nullptr, 0, 0);
            for (auto& release : frame.retired) release();
            for (auto& upload : frame.uploads) s.DestroyBuffer(upload);
            for (auto pool : frame.descriptorPools) vkDestroyDescriptorPool(s.device, pool, nullptr);
            if (frame.pool) vkDestroyCommandPool(s.device, frame.pool, nullptr);
            if (frame.fence) vkDestroyFence(s.device, frame.fence, nullptr);
            if (frame.timestamps) vkDestroyQueryPool(s.device, frame.timestamps, nullptr);
        }
        for (auto& [id, distribution] : s.distributions) s.DestroyBuffer(distribution.bins);
        for (auto& [id, program] : s.programs) { s.DestroyProgram(std::move(program)); ReleaseShaderParameters({ id }); }
        for (auto& [id, buffer] : s.buffers) s.DestroyBuffer(buffer);
        for (auto& [id, image] : s.images) s.DestroyImage(image);
        for (auto& [id, surface] : s.surfaces)
        {
            for (auto& image : surface.images) s.DestroyImage(image);
            for (auto semaphore : surface.finished) vkDestroySemaphore(s.device, semaphore, nullptr);
            for (auto semaphore : surface.available) if (semaphore) vkDestroySemaphore(s.device, semaphore, nullptr);
            if (surface.swapchain) vkDestroySwapchainKHR(s.device, surface.swapchain, nullptr);
        }
        if (s.pipelineCache) vkDestroyPipelineCache(s.device, s.pipelineCache, nullptr);
        vkDestroyDevice(s.device, nullptr);
    }
    if (s.instance)
    {
        for (auto& [id, surface] : s.surfaces) if (surface.handle) vkDestroySurfaceKHR(s.instance, surface.handle, nullptr);
        vkDestroyInstance(s.instance, nullptr);
    }
    state = std::make_unique<State>(*this);
}

//开始一帧命令录制
void VulkanRenderBackend::BeginFrame()
{
    auto& s = *state;
    if (s.recording || !s.device) return;
    //回收已完成帧的 GPU 计时
    for (auto& completed : s.frames)
    {
        if (!completed.timestampsSubmitted) continue;
        VkResult status = vkGetFenceStatus(s.device, completed.fence);
        if (status == VK_SUCCESS) s.ReadTimestamps(completed);
        else if (status != VK_NOT_READY) Check(status, "vkGetFenceStatus(timestamps)");
    }
    s.frameIndex = (s.frameIndex + 1) % FrameCount;
    auto& frame = s.frames[s.frameIndex];
    Check(vkWaitForFences(s.device, 1, &frame.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences(frame)");
    s.ReadTimestamps(frame);
    s.completedSerial = std::max(s.completedSerial, frame.serial);
    for (auto& release : frame.retired) release();
    frame.retired.clear();
    for (auto& upload : frame.uploads) upload.used = 0;
    for (auto pool : frame.descriptorPools) Check(vkResetDescriptorPool(s.device, pool, 0), "vkResetDescriptorPool");
    frame.descriptorPoolIndex = 0;
    if (!frame.commandBuffers.empty()) vkFreeCommandBuffers(s.device, frame.pool, static_cast<uint32>(frame.commandBuffers.size()), frame.commandBuffers.data());
    frame.commandBuffers.clear();
    Check(vkResetCommandPool(s.device, frame.pool, 0), "vkResetCommandPool");
    VkCommandBufferAllocateInfo allocation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocation.commandPool = frame.pool; allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocation.commandBufferCount = 1;
    Check(vkAllocateCommandBuffers(s.device, &allocation, &frame.commands), "vkAllocateCommandBuffers");
    frame.commandBuffers.push_back(frame.commands);
    VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO }; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(frame.commands, &begin), "vkBeginCommandBuffer");
    s.recording = true;
    s.commandsPending = false;
    s.drawBindings = {};
    GetPipelineState() = {};
    //开始当前采集帧的 GPU 计时
    frame.timestampCount = 0;
    frame.droppedTimestamps = 0;
    frame.profileFrameIndex = Profiler::GetCaptureFrameIndex();
    frame.profileGeneration = Profiler::GetCaptureGeneration();
    if (s.timestampValidBits && frame.profileFrameIndex >= 0)
    {
        if (!frame.timestamps)
        {
            VkQueryPoolCreateInfo queries = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
            queries.queryType = VK_QUERY_TYPE_TIMESTAMP; queries.queryCount = TimestampCapacity;
            Check(vkCreateQueryPool(s.device, &queries, nullptr, &frame.timestamps), "vkCreateQueryPool(timestamps)");
        }
        vkCmdResetQueryPool(s.RecordCommands(), frame.timestamps, 0, TimestampCapacity);
        s.WriteTimestamp("FrameStart");
        Profiler::MarkGpuPending(frame.profileGeneration, frame.profileFrameIndex);
    }
    else frame.profileFrameIndex = -1;
}

//记录当前采集帧的 GPU 阶段检查点
void VulkanRenderBackend::RecordGpuCheckpoint(const char* name)
{
    BeginFrame();
    auto& s = *state;
    const auto& frame = s.frames[s.frameIndex];
    if (frame.profileGeneration != Profiler::GetCaptureGeneration()
        || frame.profileFrameIndex != Profiler::GetCaptureFrameIndex()) return;
    const char* label = std::strrchr(name, '/');
    s.WriteTimestamp(label ? label + 1 : name);
}

//结束场景阶段的渲染范围
void VulkanRenderBackend::EndFrame() { EndPass(); }

//提交全部窗口绘制并呈现
void VulkanRenderBackend::Present()
{
    auto& s = *state;
    if (!s.recording) return;
    EndPass();
    List<VkSemaphoreSubmitInfo> waits, signals;
    List<VkSwapchainKHR> swapchains;
    List<uint32> indices;
    List<VkSemaphore> presentSemaphores;
    for (auto& [id, surface] : s.surfaces) if (surface.acquired)
    {
        s.UseImage(surface.images[surface.imageIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);
        if (surface.waitPending)
        {
            VkSemaphoreSubmitInfo wait = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
            wait.semaphore = surface.available[s.frameIndex]; wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            waits.push_back(wait);
        }
        VkSemaphoreSubmitInfo signal = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
        signal.semaphore = surface.finished[surface.imageIndex]; signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signals.push_back(signal); presentSemaphores.push_back(signal.semaphore);
        swapchains.push_back(surface.swapchain); indices.push_back(surface.imageIndex);
        surface.acquired = surface.waitPending = false;
    }
    auto& frame = s.frames[s.frameIndex];
    s.WriteTimestamp("EditorGuiAndFinalize", true);
    Check(vkEndCommandBuffer(frame.commands), "vkEndCommandBuffer");
    VkCommandBufferSubmitInfo commands = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO }; commands.commandBuffer = frame.commands;
    VkSubmitInfo2 submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    submit.waitSemaphoreInfoCount = static_cast<uint32>(waits.size()); submit.pWaitSemaphoreInfos = waits.data();
    submit.signalSemaphoreInfoCount = static_cast<uint32>(signals.size()); submit.pSignalSemaphoreInfos = signals.data();
    submit.commandBufferInfoCount = 1; submit.pCommandBufferInfos = &commands;
    Check(vkResetFences(s.device, 1, &frame.fence), "vkResetFences");
    Check(vkQueueSubmit2(s.queue, 1, &submit, frame.fence), "vkQueueSubmit2");
    frame.timestampsSubmitted = frame.timestampCount > 0;
    frame.serial = ++s.submittedSerial;
    s.recording = false;
    s.commandsPending = false;
    if (!swapchains.empty())
    {
        List<VkResult> results(swapchains.size());
        VkPresentInfoKHR present = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        present.waitSemaphoreCount = static_cast<uint32>(presentSemaphores.size()); present.pWaitSemaphores = presentSemaphores.data();
        present.swapchainCount = static_cast<uint32>(swapchains.size()); present.pSwapchains = swapchains.data(); present.pImageIndices = indices.data(); present.pResults = results.data();
        VkResult result = vkQueuePresentKHR(s.queue, &present);
        if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR) Check(result, "vkQueuePresentKHR");
        for (auto& [id, surface] : s.surfaces)
            for (usize index = 0; index < swapchains.size(); ++index)
                if (surface.swapchain == swapchains[index] && (results[index] == VK_ERROR_OUT_OF_DATE_KHR || results[index] == VK_SUBOPTIMAL_KHR)) surface.rebuild = true;
    }
}

//等待此前命令完成
void VulkanRenderBackend::WaitForIdle()
{
    auto& s = *state;
    if (!s.device) return;
    s.Flush();
    if (s.completedSerial < s.submittedSerial)
    {
        Check(vkQueueWaitIdle(s.queue), "vkQueueWaitIdle");
        s.completedSerial = s.submittedSerial;
    }
}

//创建附加窗口呈现表面
GpuSurfaceID VulkanRenderBackend::CreateSurface(IWindow* window)
{
    auto platform = dynamic_cast<GlfwWindow*>(window);
    if (!platform || window->GetGraphicsApi() != WindowGraphicsApi::Vulkan) return {};
    auto& s = *state;
    State::Surface surface; surface.window = window;
    Check(glfwCreateWindowSurface(s.instance, platform->GetGlfwWindow(), nullptr, &surface.handle), "glfwCreateWindowSurface");
    VkBool32 present;
    Check(vkGetPhysicalDeviceSurfaceSupportKHR(s.physical, s.queueFamily, surface.handle, &present), "vkGetPhysicalDeviceSurfaceSupportKHR");
    if (!present) { vkDestroySurfaceKHR(s.instance, surface.handle, nullptr); Log::Error("Vulkan device cannot present to the additional window."); return {}; }
    VkSemaphoreCreateInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (auto& semaphore : surface.available) Check(vkCreateSemaphore(s.device, &info, nullptr, &semaphore), "vkCreateSemaphore(surface)");
    s.BuildSwapchain(surface);
    uint32 id = s.nextId++; s.surfaces.emplace(id, std::move(surface));
    return { id };
}

//释放附加窗口呈现表面
void VulkanRenderBackend::DeleteSurface(GpuSurfaceID id)
{
    auto& s = *state;
    auto found = s.surfaces.find(id.id);
    if (!id.IsValid() || found == s.surfaces.end()) return;
    if (found->second.acquired) Present();
    WaitForIdle();
    s.ClearPassAttachments();
    auto& surface = found->second;
    for (auto& image : surface.images) s.DestroyImage(image);
    for (auto semaphore : surface.finished) vkDestroySemaphore(s.device, semaphore, nullptr);
    for (auto semaphore : surface.available) vkDestroySemaphore(s.device, semaphore, nullptr);
    if (surface.swapchain) vkDestroySwapchainKHR(s.device, surface.swapchain, nullptr);
    vkDestroySurfaceKHR(s.instance, surface.handle, nullptr);
    DeleteDepthTexture({ surface.depth });
    s.surfaces.erase(found);
    if (s.currentSurface == id.id) s.currentSurface = 0;
}

//选择默认绘制窗口
void VulkanRenderBackend::SetSurface(GpuSurfaceID id)
{
    EndPass();
    if (!state->surfaces.contains(id.id)) throw std::invalid_argument("Vulkan surface handle is invalid.");
    state->currentSurface = id.id;
}

//开始指定附件的渲染阶段
void VulkanRenderBackend::BeginPass(const RenderPassDesc& desc)
{
    BeginFrame();
    EndPass();
    state->pass = desc; state->passActive = true; state->scissorEnabled = false;
    state->StartRendering();
}

//结束当前渲染阶段
void VulkanRenderBackend::EndPass()
{
    state->PauseRendering(); state->passActive = false;
    state->ClearPassAttachments();
}

//设置像素裁剪区域
void VulkanRenderBackend::SetScissor(bool enabled, int32 x, int32 y, int32 width, int32 height)
{
    auto& s = *state;
    s.scissorEnabled = enabled;
    if (!s.pass.renderTarget.IsValid()) y = static_cast<int32>(s.surfaces.at(s.currentSurface).extent.height) - y - height;
    s.scissor = { { x, y }, { static_cast<uint32>(std::max(width, 0)), static_cast<uint32>(std::max(height, 0)) } };
    if (s.rendering)
    {
        VkRect2D rect = s.scissor;
        if (!enabled)
        {
            auto [color, depth] = s.GetAttachments(s.pass.renderTarget); auto image = color ? color : depth;
            rect = { { s.pass.x, s.pass.renderTarget.IsValid() ? s.pass.y : static_cast<int32>(image->height) - s.pass.y - s.pass.height }, { static_cast<uint32>(s.pass.width), static_cast<uint32>(s.pass.height) } };
        }
        vkCmdSetScissor(s.RecordCommands(), 0, 1, &rect);
    }
}

//创建顶点缓冲
GpuVertexBufferID VulkanRenderBackend::CreateVertexBuffer(const GpuBufferDesc& desc)
{
    if (!desc.size || (!desc.data && desc.usage != GpuBufferUsage::Stream)) return {};
    State::Buffer buffer;
    buffer.capacity = desc.size; buffer.stream = desc.usage == GpuBufferUsage::Stream;
    if (desc.data)
    {
        buffer.used = desc.size;
        if (buffer.stream) buffer.contents.assign(static_cast<const uint8*>(desc.data), static_cast<const uint8*>(desc.data) + desc.size);
        else
        {
            auto& s = *state;
            buffer = s.AllocateBuffer(desc.size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
            buffer.used = desc.size;
            auto upload = s.Upload(desc.data, desc.size);
            s.PauseRendering();
            VkBufferCopy copy = { upload.offset, 0, desc.size };
            vkCmdCopyBuffer(s.RecordCommands(), upload.handle, buffer.handle, 1, &copy);
            VkBufferMemoryBarrier2 barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT; barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT; barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = buffer.handle; barrier.size = desc.size;
            VkDependencyInfo dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO }; dependency.bufferMemoryBarrierCount = 1; dependency.pBufferMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(s.RecordCommands(), &dependency);
        }
    }
    uint32 id = state->nextId++; state->buffers.emplace(id, buffer);
    return { id };
}

//释放顶点缓冲
void VulkanRenderBackend::DeleteVertexBuffer(GpuVertexBufferID id)
{
    auto& s = *state; auto found = s.buffers.find(id.id);
    if (found == s.buffers.end()) return;
    for (auto& [inputId, input] : s.inputs)
    {
        if (input.vertices == &found->second) input.vertices = nullptr;
        if (input.indices == &found->second) input.indices = nullptr;
    }
    if (s.instanceBinding == &found->second) s.instanceBinding = nullptr;
    auto buffer = found->second; s.buffers.erase(found);
    if (buffer.owned && buffer.handle) s.Retire([&s, buffer] { s.DestroyBuffer(buffer); });
}

//创建索引缓冲
GpuIndexBufferID VulkanRenderBackend::CreateIndexBuffer(const GpuBufferDesc& desc)
{
    return { CreateVertexBuffer(desc).id };
}

//释放索引缓冲
void VulkanRenderBackend::DeleteIndexBuffer(GpuIndexBufferID id) { DeleteVertexBuffer({ id.id }); }

//创建顶点输入描述
GpuVertexInputID VulkanRenderBackend::CreateVertexInput(const GpuVertexInputDesc& desc)
{
    auto& s = *state;
    auto vertices = s.buffers.find(desc.vertexBuffer.id);
    auto indices = s.buffers.find(desc.indexBuffer.id);
    if (vertices == s.buffers.end() || indices == s.buffers.end() || !desc.stride) return {};
    uint32 id = s.nextId++;
    s.inputs.emplace(id, State::VertexInput{ desc, &vertices->second, &indices->second, GetVertexLayout(desc.layout).instanceStride });
    return { id };
}

//释放顶点输入描述
void VulkanRenderBackend::DeleteVertexInput(GpuVertexInputID id)
{
    auto& s = *state;
    if (s.currentInput == id.id)
    {
        s.currentInput = 0;
        s.inputBinding = nullptr;
    }
    s.inputs.erase(id.id);
}

//上传帧内不可变的顶点数据
bool VulkanRenderBackend::UploadVertexBuffer(GpuVertexBufferID id, const void* data, usize size, usize capacity)
{
    auto& s = *state; auto found = s.buffers.find(id.id);
    if (found == s.buffers.end() || size > capacity || !data || !size) return false;
    auto previous = found->second;
    auto buffer = s.Upload(data, size);
    buffer.stream = true; buffer.used = size; buffer.capacity = capacity;
    buffer.contents.assign(static_cast<const uint8*>(data), static_cast<const uint8*>(data) + size);
    buffer.uploadSerial = s.submittedSerial + 1;
    found->second = std::move(buffer);
    if (previous.owned && previous.handle) s.Retire([&s, previous] { s.DestroyBuffer(previous); });
    return true;
}

//上传帧内不可变的索引数据
bool VulkanRenderBackend::UploadIndexBuffer(GpuIndexBufferID id, const uint32* data, uint32 count, uint32 capacity)
{
    return UploadVertexBuffer({ id.id }, data, static_cast<usize>(count) * sizeof(uint32), static_cast<usize>(capacity) * sizeof(uint32));
}

//选择顶点输入
void VulkanRenderBackend::BindVertexInput(GpuVertexInputID id)
{
    auto& s = *state;
    s.instanceBuffer = 0; s.instanceOffset = 0; s.instanceBinding = nullptr;
    if (s.currentInput == id.id && s.inputBinding) return;
    s.currentInput = id.id;
    s.inputBinding = nullptr;
    if (!id.IsValid()) return;
    auto input = s.inputs.find(id.id);
    if (input == s.inputs.end()) return;
    s.inputBinding = &input->second;
}

//选择实例数据区间
bool VulkanRenderBackend::BindInstanceBuffer(GpuVertexBufferID id, usize byteOffset)
{
    auto& s = *state; auto found = s.buffers.find(id.id);
    if (found == s.buffers.end() || byteOffset >= found->second.used) return false;
    s.instanceBuffer = id.id; s.instanceOffset = byteOffset;
    s.instanceBinding = &found->second;
    return true;
}

//生成纹理的逐级缩小图
void VulkanRenderBackend::State::GenerateMipmaps(Image& image)
{
    if (image.levels == 1) return;
    PauseRendering();
    UseImage(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    auto commands = RecordCommands();
    for (uint32 level = 1; level < image.levels; ++level)
    {
        VkImageMemoryBarrier2 barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        barrier.srcStageMask = barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.handle; barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 1, 0, image.layers };
        VkDependencyInfo dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO }; dependency.imageMemoryBarrierCount = 1; dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commands, &dependency);
        VkImageBlit blit = {};
        blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, image.layers };
        blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, image.layers };
        blit.srcOffsets[1] = { static_cast<int32>(std::max(image.width >> (level - 1), 1u)), static_cast<int32>(std::max(image.height >> (level - 1), 1u)), 1 };
        blit.dstOffsets[1] = { static_cast<int32>(std::max(image.width >> level, 1u)), static_cast<int32>(std::max(image.height >> level, 1u)), 1 };
        vkCmdBlitImage(commands, image.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT; barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        vkCmdPipelineBarrier2(commands, &dependency);
    }
}

//创建二维采样纹理
GpuTextureID VulkanRenderBackend::CreateTexture(const GpuTextureDesc& desc)
{
    if (desc.width <= 0 || desc.height <= 0 || (desc.channels != 1 && desc.channels != 3 && desc.channels != 4) || !desc.pixels) return {};
    auto& s = *state;
    uint32 levels = desc.generateMipmaps ? static_cast<uint32>(std::floor(std::log2(std::max(desc.width, desc.height)))) + 1 : 1;
    VkFormat format = desc.channels == 1 ? VK_FORMAT_R8_UNORM : desc.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    auto image = s.AllocateImage(desc.width, desc.height, format, false, 1, levels, true, !desc.clampToEdge);
    image.uploadChannels = desc.channels;
    uint32 id = s.nextId++; s.images.emplace(id, image);
    if (!UploadTextureRegion({ id }, 0, 0, desc.width, desc.height, desc.channels, desc.pixels, desc.width * desc.channels)) { DeleteTexture({ id }); return {}; }
    return { id };
}

//上传纹理矩形与更新缩小图
bool VulkanRenderBackend::UploadTextureRegion(GpuTextureID id, int32 x, int32 y, int32 width, int32 height, int32 channels, const uint8* pixels, int32 rowStride)
{
    auto& s = *state; auto found = s.images.find(id.id);
    if (found == s.images.end() || !pixels || width <= 0 || height <= 0 || x < 0 || y < 0 || rowStride < width * channels || (channels != 1 && channels != 3 && channels != 4)) return false;
    auto& image = found->second;
    if (static_cast<uint32>(x + width) > image.width || static_cast<uint32>(y + height) > image.height || image.layers != 1 || image.uploadChannels != static_cast<uint32>(channels)) return false;
    uint32 destinationChannels = image.format == VK_FORMAT_R8_UNORM ? 1 : 4;
    if ((channels == 1) != (destinationChannels == 1)) return false;
    auto upload = s.Upload(nullptr, static_cast<VkDeviceSize>(width) * height * destinationChannels, 4);
    for (int32 row = 0; row < height; ++row)
    {
        auto source = pixels + static_cast<usize>(row) * rowStride;
        auto destination = upload.mapped + static_cast<usize>(row) * width * destinationChannels;
        if (channels == destinationChannels) std::memcpy(destination, source, static_cast<usize>(width) * channels);
        else for (int32 column = 0; column < width; ++column)
        {
            std::memcpy(destination + column * 4, source + column * 3, 3); destination[column * 4 + 3] = 255;
        }
    }
    s.UseImage(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy copy = {}; copy.bufferOffset = upload.offset; copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copy.imageOffset = { x, y, 0 }; copy.imageExtent = { static_cast<uint32>(width), static_cast<uint32>(height), 1 };
    vkCmdCopyBufferToImage(s.RecordCommands(), upload.handle, image.handle, image.layout, 1, &copy);
    image.initialized = true;
    s.GenerateMipmaps(image);
    return true;
}

//释放采样纹理
void VulkanRenderBackend::DeleteTexture(GpuTextureID id)
{
    auto& s = *state; auto found = s.images.find(id.id);
    if (found == s.images.end()) return;
    if (s.passColor == &found->second || s.passDepth == &found->second) s.ClearPassAttachments();
    auto image = found->second; s.images.erase(found);
    ++s.imageAccessRevision;
    s.Retire([&s, image] { s.DestroyImage(image); });
}

//创建深度附件与采样视图
GpuDepthTextureID VulkanRenderBackend::CreateDepthTexture(const GpuDepthTextureDesc& desc)
{
    if (desc.width <= 0 || desc.height <= 0) return {};
    uint32 id = state->nextId++;
    state->images.emplace(id, state->AllocateImage(desc.width, desc.height, VK_FORMAT_D32_SFLOAT, true));
    return { id };
}

//释放深度纹理
void VulkanRenderBackend::DeleteDepthTexture(GpuDepthTextureID id) { DeleteTexture({ id.id }); }

//创建六面采样纹理
GpuCubeTextureID VulkanRenderBackend::CreateCubeTexture(const GpuCubeTextureDesc& desc)
{
    if (desc.width <= 0 || desc.height <= 0 || desc.width != desc.height || (desc.channels != 3 && desc.channels != 4)) return {};
    for (const auto face : desc.faces) if (!face) return {};
    auto& s = *state;
    uint32 levels = desc.generateMipmaps ? static_cast<uint32>(std::floor(std::log2(desc.width))) + 1 : 1;
    auto image = s.AllocateImage(desc.width, desc.height, desc.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM, false, 6, levels, true);
    VkDeviceSize faceSize = static_cast<VkDeviceSize>(desc.width) * desc.height * 4;
    auto upload = s.Upload(nullptr, faceSize * 6, 4);
    for (uint32 face = 0; face < 6; ++face)
        for (uint32 pixel = 0; pixel < static_cast<uint32>(desc.width * desc.height); ++pixel)
        {
            uint8* target = upload.mapped + face * faceSize + pixel * 4;
            std::memcpy(target, desc.faces[face] + pixel * desc.channels, desc.channels); if (desc.channels == 3) target[3] = 255;
        }
    s.UseImage(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy copy = {}; copy.bufferOffset = upload.offset; copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 6 };
    copy.imageExtent = { static_cast<uint32>(desc.width), static_cast<uint32>(desc.height), 1 };
    vkCmdCopyBufferToImage(s.RecordCommands(), upload.handle, image.handle, image.layout, 1, &copy);
    image.initialized = true; s.GenerateMipmaps(image);
    uint32 id = s.nextId++; s.images.emplace(id, image);
    return { id };
}

//释放六面采样纹理
void VulkanRenderBackend::DeleteCubeTexture(GpuCubeTextureID id) { DeleteTexture({ id.id }); }

//创建颜色与深度附件组合
GpuRenderTargetID VulkanRenderBackend::CreateRenderTarget(const GpuRenderTargetDesc& desc)
{
    auto& s = *state;
    if (desc.width <= 0 || desc.height <= 0 || (desc.depthOnly && desc.colorOnly) || (desc.colorOnly && desc.depthTexture.IsValid()) || (!desc.colorOnly && !s.images.contains(desc.depthTexture.id))) return {};
    if (desc.depthTexture.IsValid())
    {
        auto& depth = s.images.at(desc.depthTexture.id);
        if (depth.width != static_cast<uint32>(desc.width) || depth.height != static_cast<uint32>(desc.height)) return {};
    }
    State::Target target; target.depth = desc.depthTexture.id; target.width = desc.width; target.height = desc.height;
    if (!desc.depthOnly)
    {
        VkFormat format = desc.format == GpuRenderTargetFormat::RGBA16F ? VK_FORMAT_R16G16B16A16_SFLOAT : desc.format == GpuRenderTargetFormat::R8 ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
        target.color = s.nextId++; s.images.emplace(target.color, s.AllocateImage(desc.width, desc.height, format, false, 1, 1, desc.linearColorFilter));
    }
    uint32 id = s.nextId++; s.targets.emplace(id, target);
    return { id };
}

//更新共享深度附件
void VulkanRenderBackend::AttachRenderTargetDepthTexture(GpuRenderTargetID id, GpuDepthTextureID depth)
{
    auto& s = *state; auto target = s.targets.find(id.id); auto image = s.images.find(depth.id);
    if (target == s.targets.end() || image == s.images.end() || target->second.width != image->second.width || target->second.height != image->second.height) return;
    s.PauseRendering(); target->second.depth = depth.id;
    if (s.passActive && s.pass.renderTarget.id == id.id) s.ClearPassAttachments();
}

//释放附件组合与拥有的颜色纹理
void VulkanRenderBackend::DeleteRenderTarget(GpuRenderTargetID id)
{
    auto& s = *state; auto found = s.targets.find(id.id);
    if (found == s.targets.end()) return;
    if (s.passActive && s.pass.renderTarget.id == id.id) EndPass();
    uint32 color = found->second.color; s.targets.erase(found); if (color) DeleteTexture({ color });
}

//获取目标颜色纹理句柄
GpuTextureID VulkanRenderBackend::GetRenderTargetColorTexture(GpuRenderTargetID id) const
{
    auto found = state->targets.find(id.id); return { found == state->targets.end() ? 0 : found->second.color };
}

//复制颜色与深度快照
bool VulkanRenderBackend::CopyRenderTarget(const GpuRenderTargetCopyDesc& desc)
{
    if (!desc.destinationRenderTarget.IsValid() || desc.width <= 0 || desc.height <= 0 || desc.sourceX < 0 || desc.sourceY < 0) return false;
    auto& s = *state; BeginFrame(); s.PauseRendering();
    auto [source, sourceDepth] = s.GetAttachments(desc.sourceRenderTarget);
    auto [destination, destinationDepth] = s.GetAttachments(desc.destinationRenderTarget);
    if (!source || !destination || desc.sourceX + desc.width > static_cast<int32>(source->width) || desc.sourceY + desc.height > static_cast<int32>(source->height) || desc.width > static_cast<int32>(destination->width) || desc.height > static_cast<int32>(destination->height)) return false;
    if (!desc.colorOnly && (source->format != destination->format || !sourceDepth || !destinationDepth)) return false;
    s.UseImage(*source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    s.UseImage(*destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkImageBlit blit = {}; blit.srcSubresource = blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    blit.srcOffsets[0] = { desc.sourceX, desc.sourceY, 0 }; blit.srcOffsets[1] = { desc.sourceX + desc.width, desc.sourceY + desc.height, 1 };
    if (!desc.sourceRenderTarget.IsValid())
    {
        blit.srcOffsets[0].y = static_cast<int32>(source->height) - desc.sourceY;
        blit.srcOffsets[1].y = blit.srcOffsets[0].y - desc.height;
    }
    blit.dstOffsets[1] = { desc.width, desc.height, 1 };
    vkCmdBlitImage(s.RecordCommands(), source->handle, source->layout, destination->handle, destination->layout, 1, &blit, VK_FILTER_NEAREST);
    destination->initialized = true;
    if (!desc.colorOnly)
    {
        s.UseImage(*sourceDepth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        s.UseImage(*destinationDepth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkImageCopy copy = {}; copy.srcSubresource = copy.dstSubresource = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 };
        copy.srcOffset = { desc.sourceX, desc.sourceY, 0 }; copy.extent = { static_cast<uint32>(desc.width), static_cast<uint32>(desc.height), 1 };
        vkCmdCopyImage(s.RecordCommands(), sourceDepth->handle, sourceDepth->layout, destinationDepth->handle, destinationDepth->layout, 1, &copy);
        destinationDepth->initialized = true;
    }
    return true;
}

//编译跨阶段图形着色器
GpuShaderProgramID VulkanRenderBackend::CreateShaderProgram(const GpuShaderProgramDesc& desc)
{
    CompiledShader compiled; std::string error;
    if (!ShaderCompiler::CompileGraphics(desc, compiled, error)) { Log::Error(("Vulkan shader compile failed: " + error).c_str()); return {}; }
    if (compiled.parameterSize > state->properties.limits.maxUniformBufferRange) { Log::Error("Vulkan shader parameter block exceeds device limits."); return {}; }
    return { state->CreateProgram(std::move(compiled)) };
}

//释放着色器与派生管线
void VulkanRenderBackend::DeleteShaderProgram(GpuShaderProgramID id)
{
    auto& s = *state; auto found = s.programs.find(id.id); if (found == s.programs.end()) return;
    if (s.currentProgram == id.id) { s.currentProgram = 0; s.programBinding = nullptr; }
    auto program = std::move(found->second); s.programs.erase(found);
    s.Retire([&s, program = std::move(program)]() mutable { s.DestroyProgram(std::move(program)); });
    ReleaseShaderParameters(id);
}

//查询链接后的参数
bool VulkanRenderBackend::HasShaderParameter(GpuShaderProgramID id, const char* name)
{
    auto found = state->programs.find(id.id); if (found == state->programs.end() || !name) return false;
    std::string_view query(name);
    for (const auto& parameter : found->second.compiled.parameters)
        if (query == parameter.name || (query.starts_with(parameter.name) && query.size() > parameter.name.size() && query[parameter.name.size()] == '[')) return true;
    for (const auto& sampler : found->second.compiled.samplers) if (query == sampler.name) return true;
    return false;
}

//选择图形或计算着色器
void VulkanRenderBackend::OnBindShaderProgram(GpuShaderProgramID id)
{
    auto& s = *state;
    if (s.currentProgram == id.id && s.programBinding) return;
    s.currentProgram = id.id;
    if (!id.IsValid()) { s.programBinding = nullptr; return; }
    auto program = s.programs.find(id.id);
    s.programBinding = program != s.programs.end() ? &program->second : nullptr;
}

//绑定二维采样资源
void VulkanRenderBackend::BindTexture(uint32 slot, GpuTextureID id)
{
    auto& s = *state;
    uint32& texture = s.textures[slot];
    if (texture == id.id) return;
    texture = id.id;
    ++s.textureBindingsRevision;
}

//绑定深度采样资源
void VulkanRenderBackend::BindDepthTexture(uint32 slot, GpuDepthTextureID id) { BindTexture(slot, { id.id }); }

//绑定立方体采样资源
void VulkanRenderBackend::BindCubeTexture(uint32 slot, GpuCubeTextureID id) { BindTexture(slot, { id.id }); }

//查询实例绘制支持
bool VulkanRenderBackend::SupportsInstancing() const { return state->device && state->properties.limits.maxVertexInputAttributes >= 14; }

//查询片元采样资源上限
uint32 VulkanRenderBackend::GetFragmentTextureUnitCount() const
{
    return std::min(state->properties.limits.maxPerStageDescriptorSamplers, state->properties.limits.maxPerStageDescriptorSampledImages);
}

//更新变化参数并复用帧内资源描述
VkDescriptorSet VulkanRenderBackend::State::PrepareDescriptors(Program& program, Buffer* storage)
{
    ShaderParameters& parameters = backend.GetParameters();
    if (!program.compute)
    {
        CachePassAttachments();
        if (!program.framebufferParameter)
            program.framebufferParameter = &parameters.GetOrCreate("u_OrbedenFramebufferInfo");
        if (program.framebufferInfoRevision != framebufferInfoRevision
            || program.framebufferParameterRevision != program.framebufferParameter->revision)
        {
            parameters.SetVector3("u_OrbedenFramebufferInfo", framebufferInfo);
            program.framebufferInfoRevision = framebufferInfoRevision;
            program.framebufferParameterRevision = program.framebufferParameter->revision;
        }
    }

    //注册反射参数的稳定索引与缓冲偏移
    if (!program.parametersRegistered)
    {
        program.parameterBytes.resize(program.compiled.parameterSize);
        for (const auto& layout : program.compiled.parameters)
        {
            uint32 size = layout.type == ShaderParameterType::Matrix4 ? 64 : layout.type == ShaderParameterType::Color ? 16 : layout.type == ShaderParameterType::Vector3 ? 12 : 4;
            for (uint32 index = 0; index < layout.count; ++index)
            {
                std::string name = layout.name;
                if (layout.count > 1 || layout.stride)
                {
                    if (name.ends_with("[0]")) name.resize(name.size() - 3);
                    name += "[" + std::to_string(index) + "]";
                }
                uint32 offset = layout.offset + index * layout.stride;
                if (offset + size > program.compiled.parameterSize) throw std::runtime_error("Vulkan shader parameter layout overflow: " + name);
                auto registerBinding = [&](const std::string& parameterName)
                {
                    const auto& value = parameters.GetOrCreate(parameterName.c_str());
                    if (program.parameterBindings.size() <= value.index) program.parameterBindings.resize(value.index + 1);
                    program.parameterBindings[value.index] = { offset, size, layout.type };
                };
                registerBinding(name);
                if (index == 0 && name != layout.name) registerBinding(layout.name);
            }
        }
        for (const auto& sampler : program.compiled.samplers)
            program.samplerParameters.push_back(&parameters.GetOrCreate(sampler.name.c_str()));
        program.imageInfos.resize(program.compiled.samplers.size());
        program.parametersRegistered = true;
    }

    //增量更新 CPU 参数块
    bool changed = false, resourcesChanged = false;
    for (const ShaderParameterValue* value : parameters.GetChanges())
    {
        if (value->index >= program.parameterBindings.size() || !program.parameterBindings[value->index].size)
        {
            resourcesChanged = true;
            continue;
        }
        const auto& binding = program.parameterBindings[value->index];
        if (value->type != binding.type) throw std::runtime_error(std::string("Vulkan shader parameter type mismatch: ") + value->name);
        std::memcpy(program.parameterBytes.data() + binding.offset, value->bytes.data(), binding.size);
        changed = true;
    }
    parameters.ClearChanges();
    if (changed) ++program.parameterRevision;

    //上传当前绘制的不可变参数快照
    VkDescriptorBufferInfo parameterInfo = {}, storageInfo = {};
    if (program.compiled.parameterSize)
    {
        if (program.parameterSerial != submittedSerial + 1 || program.uploadedParameterRevision != program.parameterRevision)
        {
            program.parameterData = Upload(program.parameterBytes.data(), program.compiled.parameterSize, properties.limits.minUniformBufferOffsetAlignment);
        }
        program.parameterSerial = submittedSerial + 1;
        program.uploadedParameterRevision = program.parameterRevision;
        parameterInfo = { program.parameterData.handle, 0, program.compiled.parameterSize };
    }

    //生成资源组合键并准备图像依赖
    if (program.descriptorSerial != submittedSerial + 1)
    {
        program.descriptorSets.clear();
        program.descriptorSerial = submittedSerial + 1;
        program.lastDescriptors = VK_NULL_HANDLE;
    }
    if (storage) storageInfo = { storage->handle, storage->offset, storage->capacity };
    if (program.lastDescriptors && !resourcesChanged
        && program.textureBindingsRevision == textureBindingsRevision && program.imageAccessRevision == imageAccessRevision
        && program.lastParameterBuffer == parameterInfo.buffer
        && program.lastStorage.buffer == storageInfo.buffer && program.lastStorage.offset == storageInfo.offset && program.lastStorage.range == storageInfo.range)
        return program.lastDescriptors;

    //缓存当前资源组合与访问版本
    auto cacheDescriptors = [&](VkDescriptorSet set)
    {
        program.lastDescriptors = set;
        program.lastParameterBuffer = parameterInfo.buffer;
        program.lastStorage = storageInfo;
        program.textureBindingsRevision = textureBindingsRevision;
        program.imageAccessRevision = imageAccessRevision;
        return set;
    };
    std::string& key = program.descriptorKey;
    key.clear();
    auto append = [&key](auto value) { key.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
    append(parameterInfo.buffer);
    for (usize index = 0; index < program.compiled.samplers.size(); ++index)
    {
        const auto& sampler = program.compiled.samplers[index];
        const auto* value = program.samplerParameters[index];
        int32 slot = value->type == ShaderParameterType::Int ? value->Read<int32>() : 0;
        auto bound = textures.find(static_cast<uint32>(slot));
        uint32 texture = bound == textures.end() ? 0 : bound->second;
        auto image = images.find(texture);
        if (image == images.end() || (image->second.layers == 6) != sampler.cube) image = images.find(sampler.cube ? blackCube : whiteTexture);
        if (image == images.end()) throw std::runtime_error("Vulkan default sampled image is unavailable.");
        UseImage(image->second, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, program.compute ? VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        auto& info = program.imageInfos[index];
        info = { image->second.sampler, image->second.view, image->second.layout };
        append(info.sampler); append(info.imageView); append(info.imageLayout);
    }
    append(storageInfo.buffer); append(storageInfo.offset); append(storageInfo.range);
    auto found = program.descriptorSets.find(key);
    if (found != program.descriptorSets.end()) return cacheDescriptors(found->second);

    //写入首次使用的资源组合
    VkDescriptorSet set = AllocateDescriptors(program.descriptors);
    List<VkWriteDescriptorSet> writes;
    writes.reserve(program.compiled.samplers.size() + 2);
    if (program.compiled.parameterSize)
    {
        VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = set; write.dstBinding = 0; write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; write.pBufferInfo = &parameterInfo;
        writes.push_back(write);
    }
    for (usize index = 0; index < program.compiled.samplers.size(); ++index)
    {
        VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = set; write.dstBinding = program.compiled.samplers[index].binding; write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo = &program.imageInfos[index];
        writes.push_back(write);
    }
    if (storage)
    {
        VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = set; write.dstBinding = 31; write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; write.pBufferInfo = &storageInfo;
        writes.push_back(write);
    }
    vkUpdateDescriptorSets(device, static_cast<uint32>(writes.size()), writes.data(), 0, nullptr);
    program.descriptorSets.emplace(key, set);
    return cacheDescriptors(set);
}

//获取当前固定功能状态对应的图形管线
VkPipeline VulkanRenderBackend::State::GetGraphicsPipeline(Program& program, const GpuVertexInputDesc& input)
{
    Image* color = passColor;
    Image* depth = passDepth;
    const PipelineState& state = backend.GetPipelineState();
    GraphicsPipelineKey pipelineKey;
    pipelineKey.layout = input.layout; pipelineKey.stride = input.stride;
    pipelineKey.color = color ? color->format : VK_FORMAT_UNDEFINED;
    pipelineKey.depth = depth ? depth->format : VK_FORMAT_UNDEFINED;
    pipelineKey.depthTest = state.depthTest; pipelineKey.depthWrite = state.depthWrite; pipelineKey.depthCompare = state.depthCompare;
    pipelineKey.blend = state.blend; pipelineKey.blendMode = state.blendMode; pipelineKey.cull = state.cull;
    pipelineKey.polygonMode = state.polygonMode; pipelineKey.topology = state.topology;
    pipelineKey.depthBias = state.depthBias; pipelineKey.offscreen = pass.renderTarget.IsValid();
    if (program.lastPipeline && program.lastPipelineKey == pipelineKey) return program.lastPipeline;

    //查找变化后的固定管线状态
    std::string& key = program.pipelineKey;
    key.clear();
    auto append = [&key](auto value) { key.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
    append(input.layout); append(input.stride); append(color ? color->format : VK_FORMAT_UNDEFINED); append(depth ? depth->format : VK_FORMAT_UNDEFINED);
    append(state.depthTest); append(state.depthWrite); append(state.depthCompare); append(state.blend); append(state.blendMode); append(state.cull); append(state.polygonMode); append(state.topology); append(state.depthBias); append(pass.renderTarget.IsValid());
    auto found = program.pipelines.find(key);
    if (found != program.pipelines.end())
    {
        program.lastPipelineKey = pipelineKey;
        program.lastPipeline = found->second;
        return found->second;
    }
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = program.vertex; stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = program.fragment; stages[1].pName = "main";
    const VertexLayout& layout = GetVertexLayout(input.layout);
    List<VkVertexInputAttributeDescription> attributes;
    for (const VertexAttribute& attribute : layout.attributes)
    {
        VkFormat format = attribute.format == VertexAttributeFormat::Float2 ? VK_FORMAT_R32G32_SFLOAT : attribute.format == VertexAttributeFormat::Float3 ? VK_FORMAT_R32G32B32_SFLOAT : attribute.format == VertexAttributeFormat::Float4 ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
        attributes.push_back({ attribute.location, attribute.binding, format, attribute.offset });
    }
    VkVertexInputBindingDescription bindings[] = { { 0, input.stride, VK_VERTEX_INPUT_RATE_VERTEX }, { 1, layout.instanceStride, VK_VERTEX_INPUT_RATE_INSTANCE } };
    VkPipelineVertexInputStateCreateInfo vertex = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertex.vertexBindingDescriptionCount = layout.instanceStride ? 2 : 1; vertex.pVertexBindingDescriptions = bindings;
    vertex.vertexAttributeDescriptionCount = static_cast<uint32>(attributes.size()); vertex.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo assembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    assembly.topology = state.topology == PrimitiveTopology::Lines ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO }; viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    raster.polygonMode = state.polygonMode == PolygonMode::Line ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    raster.cullMode = state.cull == CullMode::Front ? VK_CULL_MODE_FRONT_BIT : state.cull == CullMode::Back ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    raster.frontFace = pass.renderTarget.IsValid() ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.depthBiasEnable = state.depthBias; raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO }; multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthState = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthState.depthTestEnable = depth && state.depthTest; depthState.depthWriteEnable = depth && state.depthWrite;
    depthState.depthCompareOp = state.depthCompare == DepthCompare::LessEqual ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState blend = {};
    blend.blendEnable = state.blend; blend.colorBlendOp = blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.srcColorBlendFactor = state.blendMode == BlendMode::PremultipliedAlpha ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = state.blendMode == BlendMode::Additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.srcAlphaBlendFactor = state.blendMode == BlendMode::Additive ? VK_BLEND_FACTOR_ZERO : VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = state.blendMode == BlendMode::Additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blending = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    blending.attachmentCount = color ? 1 : 0; blending.pAttachments = &blend;
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS };
    VkPipelineDynamicStateCreateInfo dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO }; dynamic.dynamicStateCount = 3; dynamic.pDynamicStates = dynamicStates;
    VkFormat colorFormat = color ? color->format : VK_FORMAT_UNDEFINED;
    VkPipelineRenderingCreateInfo rendering = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    rendering.colorAttachmentCount = color ? 1 : 0; rendering.pColorAttachmentFormats = &colorFormat; rendering.depthAttachmentFormat = depth ? depth->format : VK_FORMAT_UNDEFINED;
    VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    info.pNext = &rendering; info.stageCount = 2; info.pStages = stages; info.pVertexInputState = &vertex; info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport; info.pRasterizationState = &raster; info.pMultisampleState = &multisample; info.pDepthStencilState = &depthState;
    info.pColorBlendState = &blending; info.pDynamicState = &dynamic; info.layout = program.layout;
    VkPipeline result;
    Check(vkCreateGraphicsPipelines(device, pipelineCache, 1, &info, nullptr, &result), "vkCreateGraphicsPipelines");
    program.pipelines.emplace(key, result);
    program.lastPipelineKey = pipelineKey;
    program.lastPipeline = result;
    return result;
}

//提交索引图元
void VulkanRenderBackend::State::Draw(uint32 indexStart, uint32 indexCount, uint32 instanceCount)
{
    if (!indexCount || !instanceCount || !passActive) return;
    if (!programBinding || !inputBinding || !inputBinding->vertices || !inputBinding->indices) return;
    Program& program = *programBinding;
    const auto& input = inputBinding->desc;
    Buffer& vertices = *inputBinding->vertices;
    Buffer& indices = *inputBinding->indices;
    RefreshBuffer(vertices);
    RefreshBuffer(indices);
    if (static_cast<uint64>(indexStart) + indexCount > indices.used / sizeof(uint32)) throw std::out_of_range("Vulkan index range exceeds buffer contents.");
    std::array<VkBuffer, 2> vertexBuffers = { vertices.handle, VK_NULL_HANDLE };
    std::array<VkDeviceSize, 2> offsets = { vertices.offset, 0 };
    uint32 count = 1;
    uint32 stride = inputBinding->instanceStride;
    if (stride)
    {
        if (!instanceBinding || instanceOffset + static_cast<uint64>(instanceCount) * stride > instanceBinding->used) throw std::out_of_range("Vulkan instance range exceeds buffer contents.");
        RefreshBuffer(*instanceBinding);
        vertexBuffers[1] = instanceBinding->handle; offsets[1] = instanceBinding->offset + instanceOffset; count = 2;
    }
    VkDescriptorSet descriptors = PrepareDescriptors(program);
    if (!StartRendering()) return;
    VkPipeline pipeline = GetGraphicsPipeline(program, input);
    VkCommandBuffer commands = RecordCommands();
    if (drawBindings.pipeline != pipeline)
    {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        drawBindings.pipeline = pipeline;
    }
    uint32 parameterOffset = static_cast<uint32>(program.parameterData.offset);
    uint32 parameterCount = program.compiled.parameterSize ? 1 : 0;
    if (drawBindings.layout != program.layout || drawBindings.descriptors != descriptors
        || drawBindings.parameterCount != parameterCount || drawBindings.parameterOffset != parameterOffset)
    {
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, program.layout, 0, 1, &descriptors, parameterCount, &parameterOffset);
        drawBindings.layout = program.layout; drawBindings.descriptors = descriptors;
        drawBindings.parameterCount = parameterCount; drawBindings.parameterOffset = parameterOffset;
    }
    const PipelineState& raster = backend.GetPipelineState();
    if (!drawBindings.depthBiasSet || drawBindings.depthBiasSlope != raster.depthBiasSlope || drawBindings.depthBiasConstant != raster.depthBiasConstant)
    {
        vkCmdSetDepthBias(commands, raster.depthBiasConstant, 0.0f, raster.depthBiasSlope);
        drawBindings.depthBiasSet = true;
        drawBindings.depthBiasSlope = raster.depthBiasSlope; drawBindings.depthBiasConstant = raster.depthBiasConstant;
    }
    if (drawBindings.vertexCount != count || drawBindings.vertexBuffers != vertexBuffers || drawBindings.vertexOffsets != offsets)
    {
        vkCmdBindVertexBuffers(commands, 0, count, vertexBuffers.data(), offsets.data());
        drawBindings.vertexCount = count; drawBindings.vertexBuffers = vertexBuffers; drawBindings.vertexOffsets = offsets;
    }
    if (drawBindings.indexBuffer != indices.handle || drawBindings.indexOffset != indices.offset)
    {
        vkCmdBindIndexBuffer(commands, indices.handle, indices.offset, VK_INDEX_TYPE_UINT32);
        drawBindings.indexBuffer = indices.handle; drawBindings.indexOffset = indices.offset;
    }
    vkCmdDrawIndexed(commands, indexCount, instanceCount, indexStart, 0, 0);
}

//绘制索引图元
void VulkanRenderBackend::DrawIndexed(uint32 indexStart, uint32 indexCount) { state->Draw(indexStart, indexCount, 1); }

//绘制实例索引图元
void VulkanRenderBackend::DrawIndexedInstanced(uint32 indexStart, uint32 indexCount, uint32 instanceCount) { state->Draw(indexStart, indexCount, instanceCount); }

//读取目标的一个深度像素
bool VulkanRenderBackend::ReadDepthPixel(GpuRenderTargetID target, int32 x, int32 y, float32& depth)
{
    auto& s = *state; BeginFrame();
    auto [color, image] = s.GetAttachments(target);
    if (!image || x < 0 || y < 0 || static_cast<uint32>(x) >= image->width || static_cast<uint32>(y) >= image->height || !image->initialized) return false;
    auto readback = s.AllocateBuffer(sizeof(float32), VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
    s.UseImage(*image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy copy = {}; copy.imageSubresource = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 }; copy.imageExtent = { 1, 1, 1 };
    copy.imageOffset = { x, target.IsValid() ? y : static_cast<int32>(image->height) - y - 1, 0 };
    vkCmdCopyImageToBuffer(s.RecordCommands(), image->handle, image->layout, readback.handle, 1, &copy);
    VkBufferMemoryBarrier2 barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT; barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT; barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = readback.handle; barrier.size = sizeof(float32);
    VkDependencyInfo dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO }; dependency.bufferMemoryBarrierCount = 1; dependency.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(s.RecordCommands(), &dependency);
    s.Flush();
    std::memcpy(&depth, readback.mapped, sizeof(depth)); s.DestroyBuffer(readback);
    return true;
}

//创建异步深度统计缓冲
GpuDepthDistributionID VulkanRenderBackend::CreateDepthDistribution()
{
    auto& s = *state;
    if (!s.distributionProgram)
    {
        std::string source = DepthDistributionSource;
        source.insert(source.find('\n') + 1, "#define ORBEDEN_DISTRIBUTION_BINDING 31\n");
        CompiledShader compiled; std::string error;
        if (!ShaderCompiler::CompileCompute(source.c_str(), compiled, error)) { Log::Error(error.c_str()); return {}; }
        s.distributionProgram = s.CreateProgram(std::move(compiled));
        auto& program = s.programs.at(s.distributionProgram);
        VkComputePipelineCreateInfo pipeline = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        pipeline.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; pipeline.stage.module = program.compute; pipeline.stage.pName = "main";
        pipeline.layout = program.layout;
        VkPipeline handle;
        Check(vkCreateComputePipelines(s.device, s.pipelineCache, 1, &pipeline, nullptr, &handle), "vkCreateComputePipelines");
        program.pipelines.emplace("", handle);
    }
    State::Distribution distribution;
    distribution.bins = s.AllocateBuffer(GpuDepthDistribution::BinCount * sizeof(uint32), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
    uint32 id = s.nextId++; s.distributions.emplace(id, distribution);
    return { id };
}

//释放异步深度统计缓冲
void VulkanRenderBackend::DeleteDepthDistribution(GpuDepthDistributionID id)
{
    auto& s = *state; auto found = s.distributions.find(id.id); if (found == s.distributions.end()) return;
    auto buffer = found->second.bins; s.distributions.erase(found); s.Retire([&s, buffer] { s.DestroyBuffer(buffer); });
}

//提交冻结深度的对数直方图
bool VulkanRenderBackend::SubmitDepthDistribution(GpuDepthDistributionID id, GpuDepthTextureID depth,
    const matrix4x4& inverseProjection, float32 nearPlane, float32 farPlane)
{
    auto& s = *state; auto found = s.distributions.find(id.id); auto image = s.images.find(depth.id);
    if (found == s.distributions.end() || found->second.serial || image == s.images.end() || !(nearPlane > 0.0f && farPlane > nearPlane)) return false;
    BeginFrame(); s.PauseRendering();
    auto& distribution = found->second;
    VkCommandBuffer commands = s.RecordCommands();
    vkCmdFillBuffer(commands, distribution.bins.handle, 0, distribution.bins.capacity, 0);
    VkBufferMemoryBarrier2 barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT; barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT; barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.buffer = distribution.bins.handle; barrier.size = distribution.bins.capacity;
    VkDependencyInfo dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO }; dependency.bufferMemoryBarrierCount = 1; dependency.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands, &dependency);
    uint32 previousProgram = s.currentProgram;
    auto previousTexture = s.textures.find(0); uint32 texture = previousTexture == s.textures.end() ? 0 : previousTexture->second;
    BindShaderProgram({ s.distributionProgram }); BindDepthTexture(0, depth);
    GetParameters().SetInt("u_Depth", 0); GetParameters().SetMatrix4("u_InverseProjection", inverseProjection);
    GetParameters().SetFloat("u_Near", nearPlane); GetParameters().SetFloat("u_Far", farPlane);
    auto& program = s.programs.at(s.distributionProgram);
    VkDescriptorSet descriptors = s.PrepareDescriptors(program, &distribution.bins);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, program.pipelines.at(""));
    uint32 parameterOffset = static_cast<uint32>(program.parameterData.offset);
    uint32 parameterCount = program.compiled.parameterSize ? 1 : 0;
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, program.layout, 0, 1, &descriptors, parameterCount, &parameterOffset);
    vkCmdDispatch(commands, (image->second.width + 15) / 16, (image->second.height + 15) / 16, 1);
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT; barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT; barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    vkCmdPipelineBarrier2(commands, &dependency);
    distribution.serial = s.submittedSerial + 1; distribution.nearPlane = nearPlane; distribution.farPlane = farPlane;
    BindShaderProgram({ previousProgram }); BindTexture(0, { texture });
    return true;
}

//零等待读取已完成统计
bool VulkanRenderBackend::TryReadDepthDistribution(GpuDepthDistributionID id, GpuDepthDistribution& output)
{
    auto& s = *state; auto found = s.distributions.find(id.id);
    if (found == s.distributions.end() || !found->second.serial) return false;
    for (auto& frame : s.frames)
        if (frame.serial > s.completedSerial && vkGetFenceStatus(s.device, frame.fence) == VK_SUCCESS) s.completedSerial = std::max(s.completedSerial, frame.serial);
    auto& distribution = found->second;
    if (distribution.serial > s.completedSerial) return false;
    std::memcpy(output.bins, distribution.bins.mapped, sizeof(output.bins));
    output.nearPlane = distribution.nearPlane; output.farPlane = distribution.farPlane; distribution.serial = 0;
    return true;
}
