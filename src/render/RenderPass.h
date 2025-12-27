#pragma once

#include "Renderer.h"
#include <string>
#include <chrono>

namespace Render {

// Base class for all render passes
// Each pass performs a specific rendering operation (shadows, G-buffer, SSAO, etc.)
class RenderPass {
public:
    explicit RenderPass(const std::string& name) : m_name(name) {}
    virtual ~RenderPass() = default;

    // Initialize GPU resources for this pass
    virtual bool initialize(const RenderConfig& config) = 0;

    // Release GPU resources
    virtual void shutdown() = 0;

    // Handle window/framebuffer resize
    virtual void resize(uint32_t width, uint32_t height) = 0;

    // Execute the render pass
    virtual void execute(RenderContext& context) = 0;

    // Get pass name for debugging/profiling
    const std::string& getName() const { return m_name; }

    // Get last execution time in milliseconds
    float getExecutionTime() const { return m_executionTimeMs; }

    // Enable/disable the pass
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

protected:
    // Helper to measure execution time with GPU queries
    void beginTiming();
    void endTiming();

    std::string m_name;
    bool m_enabled = true;
    float m_executionTimeMs = 0.0f;

    // GPU timer query objects
    uint32_t m_timerQueries[2] = {0, 0};
    bool m_timerQueriesCreated = false;
};

// Shadow map generation pass
class ShadowPass : public RenderPass {
public:
    ShadowPass();
    ~ShadowPass() override;

    bool initialize(const RenderConfig& config) override;
    void shutdown() override;
    void resize(uint32_t width, uint32_t height) override;
    void execute(RenderContext& context) override;

    // Get shadow map texture array
    uint32_t getShadowMapArray() const { return m_shadowMapArray; }

    // Get cascade view-projection matrices
    const glm::mat4* getCascadeMatrices() const { return m_cascadeMatrices; }

    // Get cascade split distances
    const float* getCascadeSplits() const { return m_cascadeSplits; }

private:
    void calculateCascadeSplits(float nearPlane, float farPlane);
    glm::mat4 calculateCascadeMatrix(const CameraData& camera, float nearSplit, float farSplit,
                                      const glm::vec3& lightDir);

    uint32_t m_shadowFBO = 0;
    uint32_t m_shadowMapArray = 0;
    uint32_t m_resolution = 2048;
    uint32_t m_numCascades = 3;

    glm::mat4 m_cascadeMatrices[4];
    float m_cascadeSplits[4] = {0.0f};

    uint32_t m_shaderProgram = 0;
};

// Z-Prepass for early depth testing
class ZPrepass : public RenderPass {
public:
    ZPrepass();
    ~ZPrepass() override;

    bool initialize(const RenderConfig& config) override;
    void shutdown() override;
    void resize(uint32_t width, uint32_t height) override;
    void execute(RenderContext& context) override;

private:
    uint32_t m_shaderProgram = 0;
};

// G-Buffer generation pass for deferred rendering
class GBufferPass : public RenderPass {
public:
    GBufferPass();
    ~GBufferPass() override;

    bool initialize(const RenderConfig& config) override;
    void shutdown() override;
    void resize(uint32_t width, uint32_t height) override;
    void execute(RenderContext& context) override;

    // Get G-Buffer textures
    uint32_t getPositionTexture() const { return m_gPosition; }
    uint32_t getNormalTexture() const { return m_gNormal; }
    uint32_t getAlbedoTexture() const { return m_gAlbedo; }
    uint32_t getDepthTexture() const { return m_gDepth; }
    uint32_t getFBO() const { return m_gBufferFBO; }

private:
    void createGBuffer(uint32_t width, uint32_t height);
    void destroyGBuffer();

    uint32_t m_gBufferFBO = 0;
    uint32_t m_gPosition = 0;  // RGB = position, A = AO
    uint32_t m_gNormal = 0;    // RGB = normal, A = light level
    uint32_t m_gAlbedo = 0;    // RGB = albedo, A = emission
    uint32_t m_gDepth = 0;     // Depth buffer

    uint32_t m_width = 0;
    uint32_t m_height = 0;

    uint32_t m_shaderProgram = 0;
};

// Hierarchical-Z buffer generation for occlusion culling
class HiZPass : public RenderPass {
public:
    HiZPass();
    ~HiZPass() override;

    bool initialize(const RenderConfig& config) override;
    void shutdown() override;
    void resize(uint32_t width, uint32_t height) override;
    void execute(RenderContext& context) override;

    uint32_t getHiZTexture() const { return m_hiZTexture; }
    int getMipLevels() const { return m_mipLevels; }

private:
    void createHiZBuffer(uint32_t width, uint32_t height);
    void destroyHiZBuffer();

    uint32_t m_hiZTexture = 0;
    int m_mipLevels = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    uint32_t m_computeShader = 0;
};

// Screen-Space Ambient Occlusion pass
class SSAOPass : public RenderPass {
public:
    SSAOPass();
    ~SSAOPass() override;

    bool initialize(const RenderConfig& config) override;
    void shutdown() override;
    void resize(uint32_t width, uint32_t height) override;
    void execute(RenderContext& context) override;

    uint32_t getSSAOTexture() const { return m_ssaoBlurred; }

private:
    void createSSAOBuffers(uint32_t width, uint32_t height);
    void destroySSAOBuffers();
    void generateKernelAndNoise();

    uint32_t m_ssaoFBO = 0;
    uint32_t m_ssaoBlurFBO = 0;
    uint32_t m_ssaoTexture = 0;
    uint32_t m_ssaoBlurred = 0;
    uint32_t m_noiseTexture = 0;

    std::vector<glm::vec3> m_ssaoKernel;
    uint32_t m_kernelSize = 16;
    float m_radius = 0.5f;
    float m_bias = 0.025f;

    uint32_t m_width = 0;
    uint32_t m_height = 0;

    uint32_t m_ssaoShader = 0;
    uint32_t m_blurShader = 0;
};

// Final composite/lighting pass
class CompositePass : public RenderPass {
public:
    CompositePass();
    ~CompositePass() override;

    bool initialize(const RenderConfig& config) override;
    void shutdown() override;
    void resize(uint32_t width, uint32_t height) override;
    void execute(RenderContext& context) override;

    uint32_t getOutputTexture() const { return m_sceneColor; }
    uint32_t getFBO() const { return m_sceneFBO; }

private:
    void createSceneBuffer(uint32_t width, uint32_t height);
    void destroySceneBuffer();

    uint32_t m_sceneFBO = 0;
    uint32_t m_sceneColor = 0;
    uint32_t m_sceneDepth = 0;

    uint32_t m_width = 0;
    uint32_t m_height = 0;

    uint32_t m_shaderProgram = 0;

    // Fullscreen quad
    uint32_t m_quadVAO = 0;
    uint32_t m_quadVBO = 0;
};

// Sky rendering pass
class SkyPass : public RenderPass {
public:
    SkyPass();
    ~SkyPass() override;

    bool initialize(const RenderConfig& config) override;
    void shutdown() override;
    void resize(uint32_t width, uint32_t height) override;
    void execute(RenderContext& context) override;

private:
    uint32_t m_shaderProgram = 0;
    uint32_t m_skyVAO = 0;
    uint32_t m_skyVBO = 0;
};

// FSR upscaling pass (AMD FidelityFX Super Resolution)
class FSRPass : public RenderPass {
public:
    FSRPass();
    ~FSRPass() override;

    bool initialize(const RenderConfig& config) override;
    void shutdown() override;
    void resize(uint32_t width, uint32_t height) override;
    void execute(RenderContext& context) override;

    // Set input/output dimensions
    void setDimensions(uint32_t renderWidth, uint32_t renderHeight,
                       uint32_t displayWidth, uint32_t displayHeight);

    uint32_t getOutputTexture() const { return m_outputTexture; }

private:
    void createBuffers();
    void destroyBuffers();

    uint32_t m_easuShader = 0;  // Edge Adaptive Spatial Upsampling
    uint32_t m_rcasShader = 0;  // Robust Contrast Adaptive Sharpening

    uint32_t m_intermediateFBO = 0;
    uint32_t m_intermediateTexture = 0;
    uint32_t m_outputFBO = 0;
    uint32_t m_outputTexture = 0;

    uint32_t m_renderWidth = 0;
    uint32_t m_renderHeight = 0;
    uint32_t m_displayWidth = 0;
    uint32_t m_displayHeight = 0;

    uint32_t m_quadVAO = 0;
    uint32_t m_quadVBO = 0;
};

} // namespace Render
