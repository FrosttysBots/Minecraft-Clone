#pragma once

#include "../RHI.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <unordered_map>

namespace RHI {

// Forward declarations
class GLBuffer;
class GLTexture;
class GLShaderProgram;
class GLGraphicsPipeline;
class GLComputePipeline;
class GLFramebuffer;
class GLRenderPass;
class GLSwapchain;
class GLDescriptorPool;
class GLDescriptorSet;
class GLDescriptorSetLayout;
class GLCommandBuffer;
class GLDevice;

// ============================================================================
// GL QUEUE (defined first because GLDevice returns GLQueue*)
// ============================================================================

class GLQueue : public RHIQueue {
public:
    explicit GLQueue(GLDevice* device) : m_device(device) {}

    QueueType getType() const override { return QueueType::Graphics; }
    void* getNativeHandle() const override { return nullptr; }

    void submit(const std::vector<RHICommandBuffer*>& commandBuffers) override;
    void waitIdle() override;

private:
    GLDevice* m_device = nullptr;
};

// ============================================================================
// GL DEVICE
// ============================================================================

class GLDevice : public RHIDevice {
public:
    explicit GLDevice(GLFWwindow* window);
    ~GLDevice() override;

    // Non-copyable
    GLDevice(const GLDevice&) = delete;
    GLDevice& operator=(const GLDevice&) = delete;

    // RHIDevice interface
    const DeviceInfo& getInfo() const override { return m_info; }
    Backend getBackend() const override { return Backend::OpenGL; }

    RHIQueue* getGraphicsQueue() override { return m_graphicsQueue.get(); }
    RHIQueue* getComputeQueue() override { return m_graphicsQueue.get(); }  // Same queue in GL
    RHIQueue* getTransferQueue() override { return m_graphicsQueue.get(); } // Same queue in GL

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

    // GL-specific helpers
    GLFWwindow* getWindow() const { return m_window; }

    // GL format conversion
    static GLenum toGLFormat(Format format);
    static GLenum toGLInternalFormat(Format format);
    static GLenum toGLType(Format format);
    static GLenum toGLBufferUsage(BufferUsage usage, MemoryUsage memory);
    static GLenum toGLShaderStage(ShaderStage stage);
    static GLenum toGLPrimitiveTopology(PrimitiveTopology topology);
    static GLenum toGLBlendFactor(BlendFactor factor);
    static GLenum toGLBlendOp(BlendOp op);
    static GLenum toGLCompareOp(CompareOp op);
    static GLenum toGLCullMode(CullMode mode);
    static GLenum toGLPolygonMode(PolygonMode mode);
    static GLenum toGLFilter(Filter filter);
    static GLenum toGLAddressMode(AddressMode mode);
    static GLenum toGLTextureTarget(TextureType type, uint32_t samples);

private:
    void queryDeviceInfo();

    GLFWwindow* m_window = nullptr;
    DeviceInfo m_info;
    std::unique_ptr<GLQueue> m_graphicsQueue;
    std::unique_ptr<RHICommandBuffer> m_immediateCommandBuffer;
};

// ============================================================================
// GL FENCE
// ============================================================================

class GLFence : public RHIFence {
public:
    explicit GLFence(bool signaled);
    ~GLFence() override;

    void* getNativeHandle() const override { return reinterpret_cast<void*>(m_sync); }
    void reset() override;
    void wait(uint64_t timeout) override;
    bool isSignaled() const override { return m_signaled; }

    // Called when a command buffer using this fence is submitted
    void setSync(GLsync sync);

private:
    GLsync m_sync = nullptr;
    bool m_signaled = false;
};

// ============================================================================
// GL SEMAPHORE (no-op in OpenGL, synchronization is implicit)
// ============================================================================

class GLSemaphore : public RHISemaphore {
public:
    void* getNativeHandle() const override { return nullptr; }
};

} // namespace RHI
