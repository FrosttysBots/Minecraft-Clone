// ============================================================================
// WIP: VULKAN BACKEND - Work In Progress
// This file is part of the Vulkan rendering backend which is currently disabled.
// Development is focused on OpenGL. To re-enable Vulkan, remove DISABLE_VULKAN
// from CMakeLists.txt and uncomment the Vulkan build targets.
// ============================================================================

#pragma once

#include "../RHI.h"

#include <volk.h>

#include <vk_mem_alloc.h>
#include <GLFW/glfw3.h>

#include <vector>
#include <optional>
#include <functional>

namespace RHI {

// Forward declarations
class VKBuffer;
class VKTexture;
class VKShaderProgram;
class VKGraphicsPipeline;
class VKComputePipeline;
class VKFramebuffer;
class VKRenderPass;
class VKSwapchain;
class VKDescriptorPool;
class VKDescriptorSet;
class VKDescriptorSetLayout;
class VKCommandBuffer;
class VKDevice;

// ============================================================================
// QUEUE FAMILY INDICES
// ============================================================================

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    std::optional<uint32_t> computeFamily;
    std::optional<uint32_t> transferFamily;

    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

// ============================================================================
// VK QUEUE
// ============================================================================

class VKQueue : public RHIQueue {
public:
    VKQueue(VKDevice* device, VkQueue queue, uint32_t familyIndex, QueueType type);

    QueueType getType() const override { return m_type; }
    void* getNativeHandle() const override { return m_queue; }

    void submit(const std::vector<RHICommandBuffer*>& commandBuffers) override;
    void waitIdle() override;

    // Submit with swapchain synchronization (wait and signal semaphores, fence)
    void submitWithSync(const std::vector<RHICommandBuffer*>& commandBuffers,
                       VkSemaphore waitSemaphore, VkSemaphore signalSemaphore,
                       VkFence fence);

    VkQueue getVkQueue() const { return m_queue; }
    uint32_t getFamilyIndex() const { return m_familyIndex; }

private:
    VKDevice* m_device = nullptr;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_familyIndex = 0;
    QueueType m_type;
};

// ============================================================================
// VK FENCE
// ============================================================================

class VKFence : public RHIFence {
public:
    VKFence(VKDevice* device, bool signaled);
    ~VKFence() override;

    void* getNativeHandle() const override { return m_fence; }
    void reset() override;
    void wait(uint64_t timeout) override;
    bool isSignaled() const override;

    VkFence getVkFence() const { return m_fence; }

private:
    VKDevice* m_device = nullptr;
    VkFence m_fence = VK_NULL_HANDLE;
};

// ============================================================================
// VK SEMAPHORE
// ============================================================================

class VKSemaphore : public RHISemaphore {
public:
    explicit VKSemaphore(VKDevice* device);
    ~VKSemaphore() override;

    void* getNativeHandle() const override { return m_semaphore; }
    VkSemaphore getVkSemaphore() const { return m_semaphore; }

private:
    VKDevice* m_device = nullptr;
    VkSemaphore m_semaphore = VK_NULL_HANDLE;
};

// ============================================================================
// VK DEVICE
// ============================================================================

class VKDevice : public RHIDevice {
public:
    explicit VKDevice(GLFWwindow* window);
    ~VKDevice() override;

    // Non-copyable
    VKDevice(const VKDevice&) = delete;
    VKDevice& operator=(const VKDevice&) = delete;

    // RHIDevice interface
    const DeviceInfo& getInfo() const override { return m_info; }
    Backend getBackend() const override { return Backend::Vulkan; }

    RHIQueue* getGraphicsQueue() override { return m_graphicsQueue.get(); }
    RHIQueue* getComputeQueue() override { return m_computeQueue ? m_computeQueue.get() : m_graphicsQueue.get(); }
    RHIQueue* getTransferQueue() override { return m_transferQueue ? m_transferQueue.get() : m_graphicsQueue.get(); }

    void waitIdle() override;

    // Resource creation
    std::unique_ptr<RHIBuffer> createBuffer(const BufferDesc& desc) override;
    std::unique_ptr<RHITexture> createTexture(const TextureDesc& desc) override;
    std::unique_ptr<RHISampler> createSampler(const SamplerDesc& desc) override;
    std::unique_ptr<RHIShaderModule> createShaderModule(const ShaderModuleDesc& desc) override;
    std::unique_ptr<RHIShaderProgram> createShaderProgram(const ShaderProgramDesc& desc) override;
    std::unique_ptr<RHIDescriptorSetLayout> createDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) override;
    std::unique_ptr<RHIPipelineLayout> createPipelineLayout(const PipelineLayoutDesc& desc) override;
    std::unique_ptr<RHIGraphicsPipeline> createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    std::unique_ptr<RHIComputePipeline> createComputePipeline(const ComputePipelineDesc& desc) override;
    std::unique_ptr<RHIRenderPass> createRenderPass(const RenderPassDesc& desc) override;
    std::unique_ptr<RHIFramebuffer> createFramebuffer(const FramebufferDesc& desc) override;
    std::unique_ptr<RHISwapchain> createSwapchain(const SwapchainDesc& desc) override;
    std::unique_ptr<RHIDescriptorPool> createDescriptorPool(const DescriptorPoolDesc& desc) override;
    std::unique_ptr<RHICommandBuffer> createCommandBuffer(CommandBufferLevel level) override;
    std::unique_ptr<RHIFence> createFence(bool signaled) override;
    std::unique_ptr<RHISemaphore> createSemaphore() override;

    void executeImmediate(std::function<void(RHICommandBuffer*)> recordFunc) override;

    // Vulkan-specific getters
    VkInstance getInstance() const { return m_instance; }
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VkDevice getDevice() const { return m_device; }
    VmaAllocator getAllocator() const { return m_allocator; }
    VkCommandPool getCommandPool() const { return m_commandPool; }
    const QueueFamilyIndices& getQueueFamilies() const { return m_queueFamilies; }

    // Format conversion
    static VkFormat toVkFormat(Format format);
    static VkBufferUsageFlags toVkBufferUsage(BufferUsage usage);
    static VkImageType toVkImageType(TextureType type);
    static VkImageViewType toVkImageViewType(TextureType type);
    static VkShaderStageFlagBits toVkShaderStage(ShaderStage stage);
    static VkShaderStageFlags toVkShaderStageFlags(ShaderStage stages);
    static VkPrimitiveTopology toVkPrimitiveTopology(PrimitiveTopology topology);
    static VkPolygonMode toVkPolygonMode(PolygonMode mode);
    static VkCullModeFlags toVkCullMode(CullMode mode);
    static VkFrontFace toVkFrontFace(FrontFace face);
    static VkCompareOp toVkCompareOp(CompareOp op);
    static VkBlendFactor toVkBlendFactor(BlendFactor factor);
    static VkBlendOp toVkBlendOp(BlendOp op);
    static VkFilter toVkFilter(Filter filter);
    static VkSamplerMipmapMode toVkMipmapMode(MipmapMode mode);
    static VkSamplerAddressMode toVkAddressMode(AddressMode mode);
    static VkDescriptorType toVkDescriptorType(DescriptorType type);
    static VkAttachmentLoadOp toVkLoadOp(LoadOp op);
    static VkAttachmentStoreOp toVkStoreOp(StoreOp op);

private:
    bool createInstance();
    bool selectPhysicalDevice();
    bool createLogicalDevice();
    bool createAllocator();
    bool createCommandPool();
    void queryDeviceInfo();

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    int rateDeviceSuitability(VkPhysicalDevice device);

    GLFWwindow* m_window = nullptr;
    DeviceInfo m_info;

    // Vulkan handles
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;

    QueueFamilyIndices m_queueFamilies;
    std::unique_ptr<VKQueue> m_graphicsQueue;
    std::unique_ptr<VKQueue> m_computeQueue;
    std::unique_ptr<VKQueue> m_transferQueue;
    std::unique_ptr<RHICommandBuffer> m_immediateCommandBuffer;

    // Required device extensions
    const std::vector<const char*> m_deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    // Validation layers (debug only)
#ifdef NDEBUG
    const bool m_enableValidation = false;
#else
    const bool m_enableValidation = true;
#endif
    const std::vector<const char*> m_validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };
};

} // namespace RHI
