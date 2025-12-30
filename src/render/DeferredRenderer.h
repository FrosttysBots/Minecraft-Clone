#pragma once

#include <glad/gl.h>
#include "Renderer.h"
#include "RenderPass.h"
#include "ShaderCompiler.h"
#include <memory>
#include <unordered_map>

namespace Render {

// Deferred renderer implementation
// Orchestrates multiple render passes: Shadow -> ZPrepass -> GBuffer -> HiZ -> SSAO -> Composite -> Sky -> FSR
class DeferredRenderer : public Renderer {
public:
    DeferredRenderer();
    ~DeferredRenderer() override;

    // Renderer interface implementation
    bool initialize(GLFWwindow* window, const RenderConfig& config) override;
    void shutdown() override;
    void resize(uint32_t width, uint32_t height) override;

    void beginFrame() override;
    void render(World& world, const CameraData& camera) override;
    void endFrame() override;

    void setConfig(const RenderConfig& config) override;
    const RenderConfig& getConfig() const override { return m_config; }

    void setLighting(const LightingParams& lighting) override;
    void setFog(const FogParams& fog) override;
    void setTextureAtlas(uint32_t textureID) override { m_textureAtlas = textureID; }

    const RenderStats& getStats() const override { return m_stats; }

    void setDebugMode(int mode) override;
    int getDebugMode() const override { return m_config.debugMode; }

    // Access individual passes for fine-grained control
    ShadowPass* getShadowPass() { return m_shadowPass.get(); }
    GBufferPass* getGBufferPass() { return m_gBufferPass.get(); }
    SSAOPass* getSSAOPass() { return m_ssaoPass.get(); }
    CompositePass* getCompositePass() { return m_compositePass.get(); }
    FSRPass* getFSRPass() { return m_fsrPass.get(); }

private:
    bool loadShaders();
    void createQuadBuffers();
    void destroyQuadBuffers();

    // Window
    GLFWwindow* m_window = nullptr;

    // Configuration
    RenderConfig m_config;
    LightingParams m_lighting;
    FogParams m_fog;
    uint32_t m_textureAtlas = 0;

    // Render passes
    std::unique_ptr<ShadowPass> m_shadowPass;
    std::unique_ptr<ZPrepass> m_zPrepass;
    std::unique_ptr<GBufferPass> m_gBufferPass;
    std::unique_ptr<HiZPass> m_hiZPass;
    std::unique_ptr<SSAOPass> m_ssaoPass;
    std::unique_ptr<CompositePass> m_compositePass;
    std::unique_ptr<SkyPass> m_skyPass;
    std::unique_ptr<FSRPass> m_fsrPass;

    // Shader compiler
    ShaderCompiler m_shaderCompiler;

    // Shader programs
    std::unordered_map<std::string, uint32_t> m_shaderPrograms;

    // Render context (passed to each pass)
    RenderContext m_context;

    // Stats
    RenderStats m_stats;

    // Frame timing
    uint64_t m_frameNumber = 0;
    float m_lastFrameTime = 0.0f;

    // GPU timer queries
    static constexpr int NUM_TIMER_QUERIES = 8;
    uint32_t m_timerQueries[2][NUM_TIMER_QUERIES] = {{0}};
    int m_currentTimerFrame = 0;
    bool m_timerQueriesCreated = false;

    // Fullscreen quad for blitting
    uint32_t m_quadVAO = 0;
    uint32_t m_quadVBO = 0;
};

// Forward renderer (simpler, for comparison/fallback)
class ForwardRenderer : public Renderer {
public:
    ForwardRenderer();
    ~ForwardRenderer() override;

    bool initialize(GLFWwindow* window, const RenderConfig& config) override;
    void shutdown() override;
    void resize(uint32_t width, uint32_t height) override;

    void beginFrame() override;
    void render(World& world, const CameraData& camera) override;
    void endFrame() override;

    void setConfig(const RenderConfig& config) override;
    const RenderConfig& getConfig() const override { return m_config; }

    void setLighting(const LightingParams& lighting) override;
    void setFog(const FogParams& fog) override;
    void setTextureAtlas(uint32_t textureID) override { (void)textureID; }

    const RenderStats& getStats() const override { return m_stats; }

    void setDebugMode(int mode) override;
    int getDebugMode() const override { return m_config.debugMode; }

private:
    GLFWwindow* m_window = nullptr;
    RenderConfig m_config;
    LightingParams m_lighting;
    FogParams m_fog;
    RenderStats m_stats;

    // Simple shadow map (single, not cascaded)
    uint32_t m_shadowFBO = 0;
    uint32_t m_shadowMap = 0;
    uint32_t m_shadowResolution = 2048;

    // Shader programs
    uint32_t m_mainShader = 0;
    uint32_t m_shadowShader = 0;
    uint32_t m_skyShader = 0;

    // Sky quad
    uint32_t m_skyVAO = 0;
    uint32_t m_skyVBO = 0;
};

} // namespace Render
