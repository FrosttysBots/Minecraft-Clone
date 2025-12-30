#pragma once

// RHI-based Deferred Renderer
// Works with both OpenGL and Vulkan backends through the RHI abstraction layer
// NOTE: Vulkan backend is WIP - currently disabled via DISABLE_VULKAN define

#include "Renderer.h"
#include "RenderPassRHI.h"
#include "ShaderCompiler.h"
#include "WorldRendererRHI.h"
#include "VertexPoolRHI.h"
#include "rhi/RHI.h"
#include <memory>
#include <unordered_map>

#ifndef DISABLE_VULKAN
#include <volk.h>  // For Vulkan types in terrain test resources
#endif

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

    // UI overlay rendering - call between render() and endFrame()
    void beginUIOverlay();
    void endUIOverlay();
    bool isUIOverlayActive() const { return m_uiOverlayActive; }

    // Simple UI drawing functions (call within beginUIOverlay/endUIOverlay)
    void drawUIRect(float x, float y, float w, float h, const glm::vec4& color);
    void drawUIText(const std::string& text, float x, float y, const glm::vec4& color, float scale = 1.0f);

    // Menu mode - when true, clears to a dark background instead of rendering terrain
    void setMenuMode(bool enabled) { m_menuMode = enabled; }
    bool isMenuMode() const { return m_menuMode; }
    void setMenuClearColor(const glm::vec4& color) { m_menuClearColor = color; }

    void setConfig(const RenderConfig& config) override;
    const RenderConfig& getConfig() const override { return m_config; }

    void setLighting(const LightingParams& lighting) override;
    void setFog(const FogParams& fog) override;
    void setTextureAtlas(uint32_t textureID) override;

    const RenderStats& getStats() const override { return m_stats; }

    void setDebugMode(int mode) override;
    int getDebugMode() const override { return m_config.debugMode; }

    // Access RHI device for external resource creation
    RHI::RHIDevice* getDevice() const { return m_device.get(); }

    // Access swapchain for UI rendering
    RHI::RHISwapchain* getSwapchain() const { return m_swapchain.get(); }

    // Access current command buffer for UI overlay rendering (call between render() and endFrame())
    RHI::RHICommandBuffer* getCurrentCommandBuffer() const {
        return m_commandBuffers.empty() ? nullptr : m_commandBuffers[m_currentFrame].get();
    }

    // Get current frame index
    uint32_t getCurrentFrameIndex() const { return m_currentFrame; }

    // Access individual passes for fine-grained control
    ShadowPassRHI* getShadowPass() { return m_shadowPass.get(); }
    GBufferPassRHI* getGBufferPass() { return m_gBufferPass.get(); }
    SSAOPassRHI* getSSAOPass() { return m_ssaoPass.get(); }
    CompositePassRHI* getCompositePass() { return m_compositePass.get(); }
    PrecipitationPassRHI* getPrecipitationPass() { return m_precipitationPass.get(); }
    BloomPassRHI* getBloomPass() { return m_bloomPass.get(); }
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

    // UI overlay state
    bool m_uiOverlayActive = false;
    std::unique_ptr<RHI::RHIRenderPass> m_uiRenderPass;
    std::unique_ptr<RHI::RHIFramebuffer> m_uiFramebuffer;

    // Menu mode state
    bool m_menuMode = false;
    glm::vec4 m_menuClearColor = glm::vec4(0.08f, 0.08f, 0.12f, 1.0f);  // Dark blue-gray

    // UI rendering resources (rectangles)
    std::unique_ptr<RHI::RHIShaderProgram> m_uiShader;
    std::unique_ptr<RHI::RHIGraphicsPipeline> m_uiPipeline;
    std::unique_ptr<RHI::RHIBuffer> m_uiQuadVBO;
    std::unique_ptr<RHI::RHIBuffer> m_uiUniformBuffer;
#ifndef DISABLE_VULKAN
    VkPipelineLayout m_uiPipelineLayout = VK_NULL_HANDLE;  // For push constants (Vulkan only)
#endif
    glm::mat4 m_uiProjection;
    bool m_uiResourcesInitialized = false;

    // UI text rendering resources (stb_easy_font)
    std::unique_ptr<RHI::RHIShaderProgram> m_uiTextShader;
    std::unique_ptr<RHI::RHIGraphicsPipeline> m_uiTextPipeline;
    std::unique_ptr<RHI::RHIBuffer> m_uiTextVBO;
#ifndef DISABLE_VULKAN
    VkPipelineLayout m_uiTextPipelineLayout = VK_NULL_HANDLE;  // Vulkan only
#endif
    bool m_uiTextResourcesInitialized = false;
    static constexpr size_t TEXT_VBO_SIZE = 1024 * 1024;  // 1MB for text vertices
    size_t m_textVBOOffset = 0;  // Current write offset in text VBO (reset each frame)

    // Render passes
    std::unique_ptr<ShadowPassRHI> m_shadowPass;
    std::unique_ptr<GBufferPassRHI> m_gBufferPass;
    std::unique_ptr<HiZPassRHI> m_hiZPass;
    std::unique_ptr<SSAOPassRHI> m_ssaoPass;
    std::unique_ptr<GPUCullingPassRHI> m_gpuCullingPass;
    std::unique_ptr<CompositePassRHI> m_compositePass;
    std::unique_ptr<SkyPassRHI> m_skyPass;
    std::unique_ptr<WaterPassRHI> m_waterPass;
    std::unique_ptr<PrecipitationPassRHI> m_precipitationPass;
    std::unique_ptr<BloomPassRHI> m_bloomPass;
    std::unique_ptr<FSRPassRHI> m_fsrPass;

    // Pipelines
    std::unique_ptr<RHI::RHIGraphicsPipeline> m_shadowPipeline;
    std::unique_ptr<RHI::RHIGraphicsPipeline> m_gBufferPipeline;
    std::unique_ptr<RHI::RHIGraphicsPipeline> m_compositePipeline;
    std::unique_ptr<RHI::RHIGraphicsPipeline> m_skyPipeline;
    std::unique_ptr<RHI::RHIGraphicsPipeline> m_waterPipeline;
    std::unique_ptr<RHI::RHIGraphicsPipeline> m_precipitationPipeline;
    std::unique_ptr<RHI::RHIGraphicsPipeline> m_bloomExtractPipeline;
    std::unique_ptr<RHI::RHIGraphicsPipeline> m_bloomDownsamplePipeline;
    std::unique_ptr<RHI::RHIGraphicsPipeline> m_bloomUpsamplePipeline;
    std::unique_ptr<RHI::RHIGraphicsPipeline> m_bloomCombinePipeline;
    std::unique_ptr<RHI::RHIComputePipeline> m_hiZPipeline;
    std::unique_ptr<RHI::RHIComputePipeline> m_ssaoPipeline;
    std::unique_ptr<RHI::RHIComputePipeline> m_ssaoBlurPipeline;
    std::unique_ptr<RHI::RHIComputePipeline> m_cullingPipeline;
    std::unique_ptr<RHI::RHIComputePipeline> m_fsrEASUPipeline;
    std::unique_ptr<RHI::RHIComputePipeline> m_fsrRCASPipeline;

    // Vulkan test pipeline (renders a simple triangle)
    std::unique_ptr<RHI::RHIShaderProgram> m_testShaderProgram;
    std::unique_ptr<RHI::RHIGraphicsPipeline> m_testPipeline;

    // Vulkan terrain test resources
    std::unique_ptr<RHI::RHIBuffer> m_testCubeVBO;        // Cube vertex buffer
    std::unique_ptr<RHI::RHIBuffer> m_testCameraUBO;      // Camera matrices UBO
    std::unique_ptr<RHI::RHIShaderProgram> m_terrainTestShader;
    std::unique_ptr<RHI::RHIGraphicsPipeline> m_terrainTestPipeline;
    std::unique_ptr<RHI::RHITexture> m_terrainAtlas;      // Block texture atlas
    std::unique_ptr<RHI::RHISampler> m_terrainSampler;    // Texture sampler
#ifndef DISABLE_VULKAN
    VkDescriptorSetLayout m_terrainDescriptorLayout = VK_NULL_HANDLE;  // Vulkan only
    VkDescriptorPool m_terrainDescriptorPool = VK_NULL_HANDLE;         // Vulkan only
    VkDescriptorSet m_terrainDescriptorSet = VK_NULL_HANDLE;           // Vulkan only
#endif
    uint32_t m_testCubeVertexCount = 0;

    // VBO cache for chunk meshes (avoids creating new buffers every frame)
    struct ChunkVBOKey {
        int chunkX, chunkZ, subY;
        bool operator==(const ChunkVBOKey& other) const {
            return chunkX == other.chunkX && chunkZ == other.chunkZ && subY == other.subY;
        }
    };
    struct ChunkVBOKeyHash {
        size_t operator()(const ChunkVBOKey& k) const {
            return std::hash<int>()(k.chunkX) ^ (std::hash<int>()(k.chunkZ) << 8) ^ (std::hash<int>()(k.subY) << 16);
        }
    };
    struct CachedVBO {
        std::unique_ptr<RHI::RHIBuffer> buffer;
        uint32_t vertexCount = 0;
        size_t dataHash = 0;  // To detect when mesh data changes
    };
    std::unordered_map<ChunkVBOKey, CachedVBO, ChunkVBOKeyHash> m_chunkVBOCache;

    // Deletion queue for VBOs that might still be in use by GPU
    // Each entry has the frame number when it was queued for deletion
    struct PendingDeletion {
        std::unique_ptr<RHI::RHIBuffer> buffer;
        uint64_t frameQueued;
    };
    std::vector<PendingDeletion> m_pendingVBODeletions;

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

    // OpenGL blit framebuffer for final output
    uint32_t m_blitFBO = 0;
};

} // namespace Render
