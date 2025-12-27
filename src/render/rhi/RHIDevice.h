#pragma once

#include "RHITypes.h"
#include "RHIBuffer.h"
#include "RHITexture.h"
#include "RHIShader.h"
#include "RHIPipeline.h"
#include "RHIFramebuffer.h"
#include "RHIDescriptorSet.h"
#include "RHICommandBuffer.h"

#include <memory>
#include <functional>

namespace RHI {

// ============================================================================
// DEVICE CAPABILITIES
// ============================================================================

struct DeviceLimits {
    // Buffer limits
    size_t maxBufferSize = 0;
    size_t minUniformBufferOffsetAlignment = 256;
    size_t minStorageBufferOffsetAlignment = 256;

    // Texture limits
    uint32_t maxTexture2DSize = 16384;
    uint32_t maxTexture3DSize = 2048;
    uint32_t maxTextureCubeSize = 16384;
    uint32_t maxTextureArrayLayers = 2048;

    // Framebuffer limits
    uint32_t maxFramebufferWidth = 16384;
    uint32_t maxFramebufferHeight = 16384;
    uint32_t maxFramebufferLayers = 2048;
    uint32_t maxColorAttachments = 8;

    // Compute limits
    uint32_t maxComputeWorkGroupCount[3] = {65535, 65535, 65535};
    uint32_t maxComputeWorkGroupSize[3] = {1024, 1024, 64};
    uint32_t maxComputeWorkGroupInvocations = 1024;

    // Other limits
    float maxAnisotropy = 16.0f;
    uint32_t maxDescriptorSets = 4;
    uint32_t maxPushConstantSize = 128;

    // Feature support
    bool supportsComputeShaders = true;
    bool supportsGeometryShaders = true;
    bool supportsTessellation = true;
    bool supportsMeshShaders = false;
    bool supportsRayTracing = false;
    bool supportsMultiDrawIndirect = true;
    bool supportsIndirectFirstInstance = true;
    bool supportsPersistentMapping = true;
};

struct DeviceInfo {
    std::string deviceName;
    std::string vendorName;
    std::string driverVersion;
    std::string apiVersion;
    Backend backend = Backend::OpenGL;
    DeviceLimits limits;
};

// ============================================================================
// QUEUE
// ============================================================================

enum class QueueType {
    Graphics,
    Compute,
    Transfer
};

class RHIQueue {
public:
    virtual ~RHIQueue() = default;

    virtual QueueType getType() const = 0;
    virtual void* getNativeHandle() const = 0;

    // Submit command buffers for execution
    virtual void submit(const std::vector<RHICommandBuffer*>& commandBuffers) = 0;

    // Wait for queue to become idle
    virtual void waitIdle() = 0;

protected:
    RHIQueue() = default;
};

// ============================================================================
// FENCE / SEMAPHORE
// ============================================================================

class RHIFence {
public:
    virtual ~RHIFence() = default;

    virtual void* getNativeHandle() const = 0;
    virtual void reset() = 0;
    virtual void wait(uint64_t timeout = UINT64_MAX) = 0;
    virtual bool isSignaled() const = 0;

protected:
    RHIFence() = default;
};

class RHISemaphore {
public:
    virtual ~RHISemaphore() = default;
    virtual void* getNativeHandle() const = 0;

protected:
    RHISemaphore() = default;
};

// ============================================================================
// RHI DEVICE
// ============================================================================
// Central factory for creating all RHI resources
// Manages device lifecycle, queues, and resource creation

class RHIDevice {
public:
    virtual ~RHIDevice() = default;

    // Get device information
    virtual const DeviceInfo& getInfo() const = 0;
    virtual Backend getBackend() const = 0;

    // ========================================================================
    // QUEUE OPERATIONS
    // ========================================================================

    virtual RHIQueue* getGraphicsQueue() = 0;
    virtual RHIQueue* getComputeQueue() = 0;
    virtual RHIQueue* getTransferQueue() = 0;

    // Wait for device to become idle (all queues)
    virtual void waitIdle() = 0;

    // ========================================================================
    // RESOURCE CREATION
    // ========================================================================

    // Buffers
    virtual std::unique_ptr<RHIBuffer> createBuffer(const BufferDesc& desc) = 0;

    // Textures
    virtual std::unique_ptr<RHITexture> createTexture(const TextureDesc& desc) = 0;
    virtual std::unique_ptr<RHISampler> createSampler(const SamplerDesc& desc) = 0;

    // Shaders
    virtual std::unique_ptr<RHIShaderModule> createShaderModule(const ShaderModuleDesc& desc) = 0;
    virtual std::unique_ptr<RHIShaderProgram> createShaderProgram(const ShaderProgramDesc& desc) = 0;

    // Pipelines
    virtual std::unique_ptr<RHIDescriptorSetLayout> createDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) = 0;
    virtual std::unique_ptr<RHIPipelineLayout> createPipelineLayout(const PipelineLayoutDesc& desc) = 0;
    virtual std::unique_ptr<RHIGraphicsPipeline> createGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
    virtual std::unique_ptr<RHIComputePipeline> createComputePipeline(const ComputePipelineDesc& desc) = 0;

    // Render passes and framebuffers
    virtual std::unique_ptr<RHIRenderPass> createRenderPass(const RenderPassDesc& desc) = 0;
    virtual std::unique_ptr<RHIFramebuffer> createFramebuffer(const FramebufferDesc& desc) = 0;

    // Swapchain
    virtual std::unique_ptr<RHISwapchain> createSwapchain(const SwapchainDesc& desc) = 0;

    // Descriptor sets
    virtual std::unique_ptr<RHIDescriptorPool> createDescriptorPool(const DescriptorPoolDesc& desc) = 0;

    // Command buffers
    virtual std::unique_ptr<RHICommandBuffer> createCommandBuffer(CommandBufferLevel level = CommandBufferLevel::Primary) = 0;

    // Synchronization
    virtual std::unique_ptr<RHIFence> createFence(bool signaled = false) = 0;
    virtual std::unique_ptr<RHISemaphore> createSemaphore() = 0;

    // ========================================================================
    // IMMEDIATE MODE HELPERS
    // ========================================================================
    // For simple operations without explicit command buffer management

    // Execute a one-shot command buffer
    virtual void executeImmediate(std::function<void(RHICommandBuffer*)> recordFunc) = 0;

    // ========================================================================
    // STATIC FACTORY
    // ========================================================================

    // Create a device for the specified backend
    // window: GLFW window handle
    static std::unique_ptr<RHIDevice> create(Backend backend, void* window);

    // Query supported backends
    static bool isBackendSupported(Backend backend);

protected:
    RHIDevice() = default;
};

} // namespace RHI
