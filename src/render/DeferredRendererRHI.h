#pragma once

// RHI-based Deferred Renderer
// Works with both OpenGL and Vulkan backends through the RHI abstraction layer

#include "Renderer.h"
#include "RenderPassRHI.h"
#include "ShaderCompiler.h"
#include "WorldRendererRHI.h"
#include "VertexPoolRHI.h"
#include "rhi/RHI.h"
#include <memory>
#include <unordered_map>

namespace Render {

class DeferredRendererRHI : public Renderer {
public:
    DeferredRendererRHI();
    ~DeferredRendererRHI() override;

    // Renderer interface implementation
    bool initialize(GLFWwindow* window, const RenderConfig& config) override;
    void shutdown() override;
    void resize(uint32_t width, uint32_t height) override;

    void beginFrame() override;
    void render(::World& world, const CameraData& camera) override;
    void endFrame() override;

    void setConfig(const RenderConfig& config) override;
    const RenderConfig& getConfig() const override { return m_config; }

    void setLighting(const LightingParams& lighting) override;
    void setFog(const FogParams& fog) override;

    const RenderStats& getStats() const override { return m_stats; }

    void setDebugMode(int mode) override;
    int getDebugMode() const override { return m_config.debugMode; }

    // Access RHI device for external resource creation
    RHI::RHIDevice* getDevice() const { return m_device.get(); }

    // Access individual passes for fine-grained control
    ShadowPassRHI* getShadowPass() { return m_shadowPass.get(); }
    GBufferPassRHI* getGBufferPass() { return m_gBufferPass.get(); }
    SSAOPassRHI* getSSAOPass() { return m_ssaoPass.get(); }
    CompositePassRHI* getCompositePass() { return m_compositePass.get(); }
    FSRPassRHI* getFSRPass() { return m_fsrPass.get(); }

    // Access RHI vertex pool
    VertexPoolRHI* getVertexPool() { return m_vertexPool.get(); }

private:
    bool createDevice(GLFWwindow* window);
    bool createSwapchain();
    bool createPipelines();
    bool createDescriptorPools();
    void destroySwapchain();

    // Window
    GLFWwindow* m_window = nullptr;

    // Configuration
    RenderConfig m_config;
    LightingParams m_lighting;
    FogParams m_fog;

    // RHI resources
    std::unique_ptr<RHI::RHIDevice> m_device;
    std::unique_ptr<RHI::RHISwapchain> m_swapchain;
    std::unique_ptr<RHI::RHIDescriptorPool> m_descriptorPool;

    // Command buffers (one per frame in flight)
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<std::unique_ptr<RHI::RHICommandBuffer>> m_commandBuffers;
    std::vector<std::unique_ptr<RHI::RHIFence>> m_frameFences;
    uint32_t m_currentFrame = 0;

    // Render passes
    std::unique_ptr<ShadowPassRHI> m_shadowPass;
    std::unique_ptr<GBufferPassRHI> m_gBufferPass;
    std::unique_ptr<HiZPassRHI> m_hiZPass;
    std::unique_ptr<SSAOPassRHI> m_ssaoPass;
    std::unique_ptr<GPUCullingPassRHI> m_gpuCullingPass;
    std::unique_ptr<CompositePassRHI> m_compositePass;
    std::unique_ptr<SkyPassRHI> m_skyPass;
    std::unique_ptr<FSRPassRHI> m_fsrPass;

    // Pipelines
    std::unique_ptr<RHI::RHIGraphicsPipeline> m_shadowPipeline;
    std::unique_ptr<RHI::RHIGraphicsPipeline> m_gBufferPipeline;
    std::unique_ptr<RHI::RHIGraphicsPipeline> m_compositePipeline;
    std::unique_ptr<RHI::RHIGraphicsPipeline> m_skyPipeline;
    std::unique_ptr<RHI::RHIComputePipeline> m_hiZPipeline;
    std::unique_ptr<RHI::RHIComputePipeline> m_ssaoPipeline;
    std::unique_ptr<RHI::RHIComputePipeline> m_ssaoBlurPipeline;
    std::unique_ptr<RHI::RHIComputePipeline> m_cullingPipeline;
    std::unique_ptr<RHI::RHIComputePipeline> m_fsrEASUPipeline;
    std::unique_ptr<RHI::RHIComputePipeline> m_fsrRCASPipeline;

    // Shader programs (compiled SPIR-V)
    std::unordered_map<std::string, std::unique_ptr<RHI::RHIShaderProgram>> m_shaderPrograms;

    // Shader compiler
    ShaderCompiler m_shaderCompiler;

    // Render context (passed to each pass)
    RenderContext m_context;

    // Stats
    RenderStats m_stats;

    // Frame timing
    uint64_t m_frameNumber = 0;
    float m_lastFrameTime = 0.0f;

    // Samplers
    std::unique_ptr<RHI::RHISampler> m_linearSampler;
    std::unique_ptr<RHI::RHISampler> m_nearestSampler;
    std::unique_ptr<RHI::RHISampler> m_shadowSampler;

    // World renderer (bridges World rendering to RHI)
    std::unique_ptr<WorldRendererRHI> m_worldRenderer;

    // RHI vertex pool (replacement for OpenGL VertexPool)
    std::unique_ptr<VertexPoolRHI> m_vertexPool;

    // Dimensions
    uint32_t m_displayWidth = 0;
    uint32_t m_displayHeight = 0;
    uint32_t m_renderWidth = 0;
    uint32_t m_renderHeight = 0;
};

} // namespace Render
