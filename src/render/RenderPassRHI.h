#pragma once

// RHI-based render passes for backend-agnostic rendering
// This version works with both OpenGL and Vulkan through the RHI abstraction layer

#include "Renderer.h"
#include "rhi/RHI.h"
#include <string>
#include <memory>
#include <vector>

namespace Render {

// Forward declarations
class World;

// Base class for RHI-based render passes
class RenderPassRHI {
public:
    explicit RenderPassRHI(const std::string& name, RHI::RHIDevice* device)
        : m_name(name), m_device(device) {}
    virtual ~RenderPassRHI() = default;

    // Initialize GPU resources for this pass
    virtual bool initialize(const RenderConfig& config) = 0;

    // Release GPU resources
    virtual void shutdown() = 0;

    // Handle window/framebuffer resize
    virtual void resize(uint32_t width, uint32_t height) = 0;

    // Execute the render pass
    virtual void execute(RHI::RHICommandBuffer* cmd, RenderContext& context) = 0;

    // Get pass name for debugging/profiling
    const std::string& getName() const { return m_name; }

    // Get last execution time in milliseconds
    float getExecutionTime() const { return m_executionTimeMs; }

    // Enable/disable the pass
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

protected:
    std::string m_name;
    RHI::RHIDevice* m_device = nullptr;
    bool m_enabled = true;
    float m_executionTimeMs = 0.0f;
};

// ============================================================================
// Shadow Pass - Cascade shadow map generation
// ============================================================================

class ShadowPassRHI : public RenderPassRHI {
public:
    ShadowPassRHI(RHI::RHIDevice* device);
    ~ShadowPassRHI() override;

    bool initialize(const RenderConfig& config) override;
    void shutdown() override;
    void resize(uint32_t width, uint32_t height) override;
    void execute(RHI::RHICommandBuffer* cmd, RenderContext& context) override;

    // Get shadow map texture array for binding
    RHI::RHITexture* getShadowMapArray() const { return m_shadowMapArray.get(); }

    // Get cascade view-projection matrices
    const glm::mat4* getCascadeMatrices() const { return m_cascadeMatrices; }

    // Get cascade split distances
    const float* getCascadeSplits() const { return m_cascadeSplits; }

    // Set the shadow shader pipeline
    void setPipeline(RHI::RHIGraphicsPipeline* pipeline) { m_pipeline = pipeline; }

    // Get render pass for pipeline creation
    RHI::RHIRenderPass* getRenderPass() const { return m_renderPass.get(); }

private:
    void calculateCascadeSplits(float nearPlane, float farPlane);
    glm::mat4 calculateCascadeMatrix(const CameraData& camera, float nearSplit, float farSplit,
                                      const glm::vec3& lightDir);

    // RHI resources
    std::unique_ptr<RHI::RHITexture> m_shadowMapArray;
    std::unique_ptr<RHI::RHIRenderPass> m_renderPass;
    std::vector<std::unique_ptr<RHI::RHIFramebuffer>> m_cascadeFramebuffers;
    RHI::RHIGraphicsPipeline* m_pipeline = nullptr;

    // Uniform buffer for cascade data
    std::unique_ptr<RHI::RHIBuffer> m_cascadeUBO;
    std::unique_ptr<RHI::RHIDescriptorSet> m_descriptorSet;

    uint32_t m_resolution = 2048;
    uint32_t m_numCascades = 3;

    glm::mat4 m_cascadeMatrices[4];
    float m_cascadeSplits[4] = {0.0f};
};

// ============================================================================
// G-Buffer Pass - Deferred rendering geometry pass
// ============================================================================

class GBufferPassRHI : public RenderPassRHI {
public:
    GBufferPassRHI(RHI::RHIDevice* device);
    ~GBufferPassRHI() override;

    bool initialize(const RenderConfig& config) override;
    void shutdown() override;
    void resize(uint32_t width, uint32_t height) override;
    void execute(RHI::RHICommandBuffer* cmd, RenderContext& context) override;

    // Get G-Buffer textures
    RHI::RHITexture* getPositionTexture() const { return m_gPosition.get(); }
    RHI::RHITexture* getNormalTexture() const { return m_gNormal.get(); }
    RHI::RHITexture* getAlbedoTexture() const { return m_gAlbedo.get(); }
    RHI::RHITexture* getDepthTexture() const { return m_gDepth.get(); }
    RHI::RHIFramebuffer* getFramebuffer() const { return m_framebuffer.get(); }
    RHI::RHIRenderPass* getRenderPass() const { return m_renderPass.get(); }

    void setPipeline(RHI::RHIGraphicsPipeline* pipeline) { m_pipeline = pipeline; }

private:
    void createGBuffer(uint32_t width, uint32_t height);
    void destroyGBuffer();

    // G-Buffer textures
    std::unique_ptr<RHI::RHITexture> m_gPosition;  // RGB = position, A = AO
    std::unique_ptr<RHI::RHITexture> m_gNormal;    // RGB = normal, A = light level
    std::unique_ptr<RHI::RHITexture> m_gAlbedo;    // RGB = albedo, A = emission
    std::unique_ptr<RHI::RHITexture> m_gDepth;     // Depth buffer

    std::unique_ptr<RHI::RHIRenderPass> m_renderPass;
    std::unique_ptr<RHI::RHIFramebuffer> m_framebuffer;
    RHI::RHIGraphicsPipeline* m_pipeline = nullptr;

    // Camera uniform buffer
    std::unique_ptr<RHI::RHIBuffer> m_cameraUBO;
    std::unique_ptr<RHI::RHIDescriptorSet> m_descriptorSet;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

// ============================================================================
// Hi-Z Pass - Hierarchical Z-buffer for occlusion culling
// ============================================================================

class HiZPassRHI : public RenderPassRHI {
public:
    HiZPassRHI(RHI::RHIDevice* device);
    ~HiZPassRHI() override;

    bool initialize(const RenderConfig& config) override;
    void shutdown() override;
    void resize(uint32_t width, uint32_t height) override;
    void execute(RHI::RHICommandBuffer* cmd, RenderContext& context) override;

    RHI::RHITexture* getHiZTexture() const { return m_hiZTexture.get(); }
    int getMipLevels() const { return m_mipLevels; }

    void setComputePipeline(RHI::RHIComputePipeline* pipeline) { m_computePipeline = pipeline; }
    void setDepthTexture(RHI::RHITexture* depth) { m_depthTexture = depth; }

private:
    void createHiZBuffer(uint32_t width, uint32_t height);
    void destroyHiZBuffer();

    std::unique_ptr<RHI::RHITexture> m_hiZTexture;
    std::vector<std::unique_ptr<RHI::RHIDescriptorSet>> m_mipDescriptorSets;
    RHI::RHIComputePipeline* m_computePipeline = nullptr;
    RHI::RHITexture* m_depthTexture = nullptr;

    int m_mipLevels = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

// ============================================================================
// SSAO Pass - Screen-Space Ambient Occlusion
// ============================================================================

class SSAOPassRHI : public RenderPassRHI {
public:
    SSAOPassRHI(RHI::RHIDevice* device);
    ~SSAOPassRHI() override;

    bool initialize(const RenderConfig& config) override;
    void shutdown() override;
    void resize(uint32_t width, uint32_t height) override;
    void execute(RHI::RHICommandBuffer* cmd, RenderContext& context) override;

    RHI::RHITexture* getSSAOTexture() const { return m_ssaoBlurred.get(); }

    void setComputePipeline(RHI::RHIComputePipeline* ssaoPipeline) { m_ssaoPipeline = ssaoPipeline; }
    void setBlurPipeline(RHI::RHIComputePipeline* blurPipeline) { m_blurPipeline = blurPipeline; }
    void setGBufferTextures(RHI::RHITexture* position, RHI::RHITexture* normal, RHI::RHITexture* depth);

private:
    void createSSAOBuffers(uint32_t width, uint32_t height);
    void destroySSAOBuffers();
    void generateKernelAndNoise();

    // SSAO textures
    std::unique_ptr<RHI::RHITexture> m_ssaoTexture;
    std::unique_ptr<RHI::RHITexture> m_ssaoBlurred;
    std::unique_ptr<RHI::RHITexture> m_noiseTexture;

    // SSAO kernel stored in buffer for compute shader
    std::unique_ptr<RHI::RHIBuffer> m_kernelBuffer;

    // Descriptor sets for compute
    std::unique_ptr<RHI::RHIDescriptorSet> m_ssaoDescriptorSet;
    std::unique_ptr<RHI::RHIDescriptorSet> m_blurDescriptorSet;

    RHI::RHIComputePipeline* m_ssaoPipeline = nullptr;
    RHI::RHIComputePipeline* m_blurPipeline = nullptr;

    // G-Buffer references
    RHI::RHITexture* m_positionTexture = nullptr;
    RHI::RHITexture* m_normalTexture = nullptr;
    RHI::RHITexture* m_depthTexture = nullptr;

    std::vector<glm::vec4> m_ssaoKernel;  // vec4 for alignment
    uint32_t m_kernelSize = 16;
    float m_radius = 0.5f;
    float m_bias = 0.025f;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

// ============================================================================
// GPU Culling Pass - Compute-based frustum and occlusion culling
// ============================================================================

class GPUCullingPassRHI : public RenderPassRHI {
public:
    GPUCullingPassRHI(RHI::RHIDevice* device);
    ~GPUCullingPassRHI() override;

    bool initialize(const RenderConfig& config) override;
    void shutdown() override;
    void resize(uint32_t width, uint32_t height) override;
    void execute(RHI::RHICommandBuffer* cmd, RenderContext& context) override;

    // Get the indirect draw buffer
    RHI::RHIBuffer* getIndirectBuffer() const { return m_indirectBuffer.get(); }

    // Get visible count (after readback)
    uint32_t getVisibleCount() const { return m_visibleCount; }

    void setComputePipeline(RHI::RHIComputePipeline* pipeline) { m_computePipeline = pipeline; }
    void setHiZTexture(RHI::RHITexture* hiZ) { m_hiZTexture = hiZ; }

    // Set chunk AABB data for culling
    void setChunkData(RHI::RHIBuffer* chunkAABBs, uint32_t chunkCount);

private:
    // Input: chunk bounding boxes
    RHI::RHIBuffer* m_chunkAABBBuffer = nullptr;
    uint32_t m_chunkCount = 0;

    // Output: indirect draw commands
    std::unique_ptr<RHI::RHIBuffer> m_indirectBuffer;

    // Atomic counter for visible chunks
    std::unique_ptr<RHI::RHIBuffer> m_counterBuffer;

    // Culling uniforms (frustum planes, Hi-Z size, etc.)
    std::unique_ptr<RHI::RHIBuffer> m_cullingUBO;
    std::unique_ptr<RHI::RHIDescriptorSet> m_descriptorSet;

    RHI::RHIComputePipeline* m_computePipeline = nullptr;
    RHI::RHITexture* m_hiZTexture = nullptr;

    uint32_t m_visibleCount = 0;
    uint32_t m_maxChunks = 4096;
};

// ============================================================================
// Composite Pass - Final lighting and composition
// ============================================================================

class CompositePassRHI : public RenderPassRHI {
public:
    CompositePassRHI(RHI::RHIDevice* device);
    ~CompositePassRHI() override;

    bool initialize(const RenderConfig& config) override;
    void shutdown() override;
    void resize(uint32_t width, uint32_t height) override;
    void execute(RHI::RHICommandBuffer* cmd, RenderContext& context) override;

    RHI::RHITexture* getOutputTexture() const { return m_sceneColor.get(); }
    RHI::RHIFramebuffer* getFramebuffer() const { return m_framebuffer.get(); }
    RHI::RHIRenderPass* getRenderPass() const { return m_renderPass.get(); }

    void setPipeline(RHI::RHIGraphicsPipeline* pipeline) { m_pipeline = pipeline; }
    void setGBufferTextures(RHI::RHITexture* position, RHI::RHITexture* normal,
                             RHI::RHITexture* albedo, RHI::RHITexture* depth);
    void setSSAOTexture(RHI::RHITexture* ssao) { m_ssaoTexture = ssao; }
    void setShadowMap(RHI::RHITexture* shadow) { m_shadowMap = shadow; }

private:
    void createSceneBuffer(uint32_t width, uint32_t height);
    void destroySceneBuffer();

    std::unique_ptr<RHI::RHITexture> m_sceneColor;
    std::unique_ptr<RHI::RHITexture> m_sceneDepth;
    std::unique_ptr<RHI::RHIRenderPass> m_renderPass;
    std::unique_ptr<RHI::RHIFramebuffer> m_framebuffer;
    RHI::RHIGraphicsPipeline* m_pipeline = nullptr;

    // Lighting uniform buffer
    std::unique_ptr<RHI::RHIBuffer> m_lightingUBO;
    std::unique_ptr<RHI::RHIDescriptorSet> m_descriptorSet;

    // Fullscreen quad
    std::unique_ptr<RHI::RHIBuffer> m_quadVertexBuffer;

    // G-Buffer texture references
    RHI::RHITexture* m_gPosition = nullptr;
    RHI::RHITexture* m_gNormal = nullptr;
    RHI::RHITexture* m_gAlbedo = nullptr;
    RHI::RHITexture* m_gDepth = nullptr;
    RHI::RHITexture* m_ssaoTexture = nullptr;
    RHI::RHITexture* m_shadowMap = nullptr;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

// ============================================================================
// Sky Pass - Atmospheric sky rendering
// ============================================================================

class SkyPassRHI : public RenderPassRHI {
public:
    SkyPassRHI(RHI::RHIDevice* device);
    ~SkyPassRHI() override;

    bool initialize(const RenderConfig& config) override;
    void shutdown() override;
    void resize(uint32_t width, uint32_t height) override;
    void execute(RHI::RHICommandBuffer* cmd, RenderContext& context) override;

    void setPipeline(RHI::RHIGraphicsPipeline* pipeline) { m_pipeline = pipeline; }
    void setTargetFramebuffer(RHI::RHIFramebuffer* fb) { m_targetFramebuffer = fb; }

private:
    std::unique_ptr<RHI::RHIBuffer> m_skyVertexBuffer;
    std::unique_ptr<RHI::RHIBuffer> m_skyUBO;
    std::unique_ptr<RHI::RHIDescriptorSet> m_descriptorSet;
    RHI::RHIGraphicsPipeline* m_pipeline = nullptr;
    RHI::RHIFramebuffer* m_targetFramebuffer = nullptr;
};

// ============================================================================
// FSR Pass - AMD FidelityFX Super Resolution upscaling
// ============================================================================

class FSRPassRHI : public RenderPassRHI {
public:
    FSRPassRHI(RHI::RHIDevice* device);
    ~FSRPassRHI() override;

    bool initialize(const RenderConfig& config) override;
    void shutdown() override;
    void resize(uint32_t width, uint32_t height) override;
    void execute(RHI::RHICommandBuffer* cmd, RenderContext& context) override;

    void setDimensions(uint32_t renderWidth, uint32_t renderHeight,
                       uint32_t displayWidth, uint32_t displayHeight);

    RHI::RHITexture* getOutputTexture() const { return m_outputTexture.get(); }

    void setEASUPipeline(RHI::RHIComputePipeline* pipeline) { m_easuPipeline = pipeline; }
    void setRCASPipeline(RHI::RHIComputePipeline* pipeline) { m_rcasPipeline = pipeline; }
    void setInputTexture(RHI::RHITexture* input) { m_inputTexture = input; }

private:
    void createBuffers();
    void destroyBuffers();

    RHI::RHIComputePipeline* m_easuPipeline = nullptr;  // Edge Adaptive Spatial Upsampling
    RHI::RHIComputePipeline* m_rcasPipeline = nullptr;  // Robust Contrast Adaptive Sharpening

    std::unique_ptr<RHI::RHITexture> m_intermediateTexture;
    std::unique_ptr<RHI::RHITexture> m_outputTexture;

    std::unique_ptr<RHI::RHIBuffer> m_fsrConstantsBuffer;
    std::unique_ptr<RHI::RHIDescriptorSet> m_easuDescriptorSet;
    std::unique_ptr<RHI::RHIDescriptorSet> m_rcasDescriptorSet;

    RHI::RHITexture* m_inputTexture = nullptr;

    uint32_t m_renderWidth = 0;
    uint32_t m_renderHeight = 0;
    uint32_t m_displayWidth = 0;
    uint32_t m_displayHeight = 0;
};

} // namespace Render
