#include "DeferredRenderer.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <fstream>
#include <sstream>

namespace Render {

// Factory method implementation
std::unique_ptr<Renderer> Renderer::create(bool useDeferred) {
    if (useDeferred) {
        return std::make_unique<DeferredRenderer>();
    } else {
        return std::make_unique<ForwardRenderer>();
    }
}

// ============================================================================
// DeferredRenderer Implementation
// ============================================================================

DeferredRenderer::DeferredRenderer() {
    // Create all render passes
    m_shadowPass = std::make_unique<ShadowPass>();
    m_zPrepass = std::make_unique<ZPrepass>();
    m_gBufferPass = std::make_unique<GBufferPass>();
    m_hiZPass = std::make_unique<HiZPass>();
    m_ssaoPass = std::make_unique<SSAOPass>();
    m_compositePass = std::make_unique<CompositePass>();
    m_skyPass = std::make_unique<SkyPass>();
    m_fsrPass = std::make_unique<FSRPass>();
}

DeferredRenderer::~DeferredRenderer() {
    shutdown();
}

bool DeferredRenderer::initialize(GLFWwindow* window, const RenderConfig& config) {
    m_window = window;
    m_config = config;

    std::cout << "[DeferredRenderer] Initializing..." << std::endl;

    // Initialize shader compiler
    if (!ShaderCompiler::initialize()) {
        std::cerr << "[DeferredRenderer] Failed to initialize shader compiler" << std::endl;
        return false;
    }
    m_shaderCompiler.setCacheDirectory("shader_cache");

    // Load shaders
    if (!loadShaders()) {
        std::cerr << "[DeferredRenderer] Failed to load shaders" << std::endl;
        return false;
    }

    // Initialize render passes
    if (!m_shadowPass->initialize(config)) {
        std::cerr << "[DeferredRenderer] Failed to initialize shadow pass" << std::endl;
        return false;
    }

    if (!m_zPrepass->initialize(config)) {
        std::cerr << "[DeferredRenderer] Failed to initialize z-prepass" << std::endl;
        return false;
    }

    if (!m_gBufferPass->initialize(config)) {
        std::cerr << "[DeferredRenderer] Failed to initialize G-buffer pass" << std::endl;
        return false;
    }

    if (config.enableHiZCulling) {
        if (!m_hiZPass->initialize(config)) {
            std::cerr << "[DeferredRenderer] Failed to initialize Hi-Z pass" << std::endl;
            return false;
        }
    }

    if (config.enableSSAO) {
        if (!m_ssaoPass->initialize(config)) {
            std::cerr << "[DeferredRenderer] Failed to initialize SSAO pass" << std::endl;
            return false;
        }
    }

    if (!m_compositePass->initialize(config)) {
        std::cerr << "[DeferredRenderer] Failed to initialize composite pass" << std::endl;
        return false;
    }

    if (!m_skyPass->initialize(config)) {
        std::cerr << "[DeferredRenderer] Failed to initialize sky pass" << std::endl;
        return false;
    }

    if (config.enableFSR) {
        if (!m_fsrPass->initialize(config)) {
            std::cerr << "[DeferredRenderer] Failed to initialize FSR pass" << std::endl;
            return false;
        }
    }

    // Create GPU timer queries
    glGenQueries(NUM_TIMER_QUERIES, m_timerQueries[0]);
    glGenQueries(NUM_TIMER_QUERIES, m_timerQueries[1]);
    m_timerQueriesCreated = true;

    // Create fullscreen quad
    createQuadBuffers();

    // Store window in context
    m_context.window = window;
    m_context.config = &m_config;

    std::cout << "[DeferredRenderer] Initialization complete" << std::endl;
    std::cout << "  Render resolution: " << config.renderWidth << "x" << config.renderHeight << std::endl;
    std::cout << "  Display resolution: " << config.displayWidth << "x" << config.displayHeight << std::endl;
    std::cout << "  SSAO: " << (config.enableSSAO ? "enabled" : "disabled") << std::endl;
    std::cout << "  Shadows: " << (config.enableShadows ? "enabled" : "disabled") << std::endl;
    std::cout << "  FSR: " << (config.enableFSR ? "enabled" : "disabled") << std::endl;
    std::cout << "  Hi-Z Culling: " << (config.enableHiZCulling ? "enabled" : "disabled") << std::endl;

    return true;
}

void DeferredRenderer::shutdown() {
    std::cout << "[DeferredRenderer] Shutting down..." << std::endl;

    // Shutdown all passes
    if (m_shadowPass) m_shadowPass->shutdown();
    if (m_zPrepass) m_zPrepass->shutdown();
    if (m_gBufferPass) m_gBufferPass->shutdown();
    if (m_hiZPass) m_hiZPass->shutdown();
    if (m_ssaoPass) m_ssaoPass->shutdown();
    if (m_compositePass) m_compositePass->shutdown();
    if (m_skyPass) m_skyPass->shutdown();
    if (m_fsrPass) m_fsrPass->shutdown();

    // Delete shader programs
    for (auto& [name, program] : m_shaderPrograms) {
        if (program) {
            glDeleteProgram(program);
        }
    }
    m_shaderPrograms.clear();

    // Delete timer queries
    if (m_timerQueriesCreated) {
        glDeleteQueries(NUM_TIMER_QUERIES, m_timerQueries[0]);
        glDeleteQueries(NUM_TIMER_QUERIES, m_timerQueries[1]);
        m_timerQueriesCreated = false;
    }

    destroyQuadBuffers();

    // Shutdown shader compiler
    ShaderCompiler::shutdown();

    m_window = nullptr;
}

void DeferredRenderer::resize(uint32_t width, uint32_t height) {
    // Update display resolution
    m_config.displayWidth = width;
    m_config.displayHeight = height;

    // If FSR is disabled, render resolution matches display
    if (!m_config.enableFSR) {
        m_config.renderWidth = width;
        m_config.renderHeight = height;
    }

    // Resize all passes
    m_gBufferPass->resize(m_config.renderWidth, m_config.renderHeight);
    m_hiZPass->resize(m_config.renderWidth, m_config.renderHeight);
    m_ssaoPass->resize(m_config.renderWidth, m_config.renderHeight);
    m_compositePass->resize(m_config.renderWidth, m_config.renderHeight);

    if (m_config.enableFSR) {
        m_fsrPass->setDimensions(m_config.renderWidth, m_config.renderHeight,
                                 m_config.displayWidth, m_config.displayHeight);
    }

    std::cout << "[DeferredRenderer] Resized to " << width << "x" << height << std::endl;
}

void DeferredRenderer::beginFrame() {
    m_frameNumber++;
    m_context.frameNumber = m_frameNumber;
    m_context.time = static_cast<float>(glfwGetTime());
    m_context.deltaTime = m_context.time - m_lastFrameTime;
    m_lastFrameTime = m_context.time;

    // Swap timer query buffers
    m_currentTimerFrame = 1 - m_currentTimerFrame;

    // Reset stats
    m_stats = RenderStats();
}

void DeferredRenderer::render(World& world, const CameraData& camera) {
    // Set up render context
    m_context.camera = &camera;
    m_context.lighting = &m_lighting;
    m_context.fog = &m_fog;
    m_context.world = &world;

    // Update lighting time
    m_lighting.time = m_context.time;

    // Execute render passes in order

    // 1. Shadow Pass
    if (m_config.enableShadows) {
        m_shadowPass->execute(m_context);
    }

    // 2. Z-Prepass (eliminates overdraw in G-buffer)
    glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferPass->getFBO());
    glViewport(0, 0, m_config.renderWidth, m_config.renderHeight);
    m_zPrepass->execute(m_context);

    // 3. G-Buffer Pass
    m_gBufferPass->execute(m_context);

    // 4. Hi-Z Generation (for occlusion culling)
    if (m_config.enableHiZCulling) {
        m_hiZPass->execute(m_context);
    }

    // 5. SSAO Pass
    if (m_config.enableSSAO) {
        m_ssaoPass->execute(m_context);
    }

    // 6. Composite Pass (lighting + shadows + fog)
    m_compositePass->execute(m_context);

    // 7. Copy depth for sky rendering
    GLuint targetFBO = m_config.enableFSR ? m_compositePass->getFBO() : 0;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_gBufferPass->getFBO());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, targetFBO);
    glBlitFramebuffer(0, 0, m_config.renderWidth, m_config.renderHeight,
                      0, 0, m_config.renderWidth, m_config.renderHeight,
                      GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);

    // 8. Sky Pass (renders at depth = 1.0)
    if (m_config.debugMode == 0) {  // Only in normal mode
        m_skyPass->execute(m_context);
    }

    // 9. FSR Upscaling
    if (m_config.enableFSR) {
        m_fsrPass->execute(m_context);
    }

    // Collect stats from passes
    m_stats.shadowTime = m_shadowPass->getExecutionTime();
    m_stats.gbufferTime = m_gBufferPass->getExecutionTime();
    m_stats.hizTime = m_hiZPass->getExecutionTime();
    m_stats.ssaoTime = m_ssaoPass->getExecutionTime();
    m_stats.compositeTime = m_compositePass->getExecutionTime();
    m_stats.skyTime = m_skyPass->getExecutionTime();
    m_stats.totalTime = m_stats.shadowTime + m_stats.gbufferTime + m_stats.hizTime +
                        m_stats.ssaoTime + m_stats.compositeTime + m_stats.skyTime;

    // Get chunk stats from world
    m_stats.chunksRendered = world.lastRenderedChunks;
    m_stats.chunksCulled = world.lastCulledChunks;
    m_stats.chunksTotal = world.lastRenderedChunks + world.lastCulledChunks;
}

void DeferredRenderer::endFrame() {
    // Swap buffers is handled by the main loop
}

void DeferredRenderer::setConfig(const RenderConfig& config) {
    bool needsResize = (config.renderWidth != m_config.renderWidth ||
                        config.renderHeight != m_config.renderHeight);

    m_config = config;

    // Update pass enable states
    m_shadowPass->setEnabled(config.enableShadows);
    m_ssaoPass->setEnabled(config.enableSSAO);
    m_hiZPass->setEnabled(config.enableHiZCulling);
    m_fsrPass->setEnabled(config.enableFSR);

    if (needsResize) {
        resize(config.displayWidth, config.displayHeight);
    }
}

void DeferredRenderer::setLighting(const LightingParams& lighting) {
    m_lighting = lighting;
}

void DeferredRenderer::setFog(const FogParams& fog) {
    m_fog = fog;
}

void DeferredRenderer::setDebugMode(int mode) {
    m_config.debugMode = mode;
}

bool DeferredRenderer::loadShaders() {
    ShaderCompileOptions options;
    options.vulkanSemantics = false;  // OpenGL
    options.optimizePerformance = true;

    auto loadShaderProgram = [&](const std::string& name,
                                  const std::string& vertPath,
                                  const std::string& fragPath) -> bool {
        auto vertShader = m_shaderCompiler.loadShader(vertPath, ShaderStage::Vertex, options);
        if (!vertShader) {
            std::cerr << "[DeferredRenderer] Failed to load vertex shader: " << vertPath << std::endl;
            return false;
        }

        auto fragShader = m_shaderCompiler.loadShader(fragPath, ShaderStage::Fragment, options);
        if (!fragShader) {
            std::cerr << "[DeferredRenderer] Failed to load fragment shader: " << fragPath << std::endl;
            return false;
        }

        // Note: For OpenGL, we'd need to create GL shader objects from SPIR-V or GLSL source
        // For now, store the compiled shaders for when we fully integrate with the RHI
        std::cout << "[DeferredRenderer] Loaded shader program: " << name << std::endl;
        return true;
    };

    // Load all shader programs
    // These paths assume shaders have been extracted to the shaders/ directory
    bool success = true;

    // Check if shader files exist before trying to load
    std::ifstream testFile("shaders/deferred/gbuffer.vert");
    if (testFile.good()) {
        testFile.close();
        success &= loadShaderProgram("gbuffer", "shaders/deferred/gbuffer.vert", "shaders/deferred/gbuffer.frag");
        success &= loadShaderProgram("composite", "shaders/deferred/composite.vert", "shaders/deferred/composite.frag");
        success &= loadShaderProgram("zprepass", "shaders/deferred/zprepass.vert", "shaders/deferred/zprepass.frag");
        success &= loadShaderProgram("shadow", "shaders/forward/shadow.vert", "shaders/forward/shadow.frag");
        success &= loadShaderProgram("ssao", "shaders/postprocess/ssao.vert", "shaders/postprocess/ssao.frag");
        success &= loadShaderProgram("ssao_blur", "shaders/postprocess/ssao.vert", "shaders/postprocess/ssao_blur.frag");
    } else {
        std::cout << "[DeferredRenderer] Shader files not found in shaders/ directory" << std::endl;
        std::cout << "[DeferredRenderer] Using inline shaders from main.cpp for now" << std::endl;
        // Return true to allow continued operation with existing inline shaders
        return true;
    }

    return success;
}

void DeferredRenderer::createQuadBuffers() {
    float quadVertices[] = {
        // positions   // texCoords
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };

    glGenVertexArrays(1, &m_quadVAO);
    glGenBuffers(1, &m_quadVBO);
    glBindVertexArray(m_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);
}

void DeferredRenderer::destroyQuadBuffers() {
    if (m_quadVAO) { glDeleteVertexArrays(1, &m_quadVAO); m_quadVAO = 0; }
    if (m_quadVBO) { glDeleteBuffers(1, &m_quadVBO); m_quadVBO = 0; }
}

// ============================================================================
// ForwardRenderer Implementation (Stub for now)
// ============================================================================

ForwardRenderer::ForwardRenderer() {}

ForwardRenderer::~ForwardRenderer() {
    shutdown();
}

bool ForwardRenderer::initialize(GLFWwindow* window, const RenderConfig& config) {
    m_window = window;
    m_config = config;

    std::cout << "[ForwardRenderer] Initializing..." << std::endl;

    // Create shadow map
    glGenFramebuffers(1, &m_shadowFBO);
    glGenTextures(1, &m_shadowMap);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
                 m_shadowResolution, m_shadowResolution, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_shadowMap, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Create sky quad
    float skyVertices[] = {
        -1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f
    };

    glGenVertexArrays(1, &m_skyVAO);
    glGenBuffers(1, &m_skyVBO);
    glBindVertexArray(m_skyVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_skyVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyVertices), skyVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    std::cout << "[ForwardRenderer] Initialization complete" << std::endl;
    return true;
}

void ForwardRenderer::shutdown() {
    if (m_shadowFBO) { glDeleteFramebuffers(1, &m_shadowFBO); m_shadowFBO = 0; }
    if (m_shadowMap) { glDeleteTextures(1, &m_shadowMap); m_shadowMap = 0; }
    if (m_mainShader) { glDeleteProgram(m_mainShader); m_mainShader = 0; }
    if (m_shadowShader) { glDeleteProgram(m_shadowShader); m_shadowShader = 0; }
    if (m_skyShader) { glDeleteProgram(m_skyShader); m_skyShader = 0; }
    if (m_skyVAO) { glDeleteVertexArrays(1, &m_skyVAO); m_skyVAO = 0; }
    if (m_skyVBO) { glDeleteBuffers(1, &m_skyVBO); m_skyVBO = 0; }
    m_window = nullptr;
}

void ForwardRenderer::resize(uint32_t width, uint32_t height) {
    m_config.displayWidth = width;
    m_config.displayHeight = height;
    m_config.renderWidth = width;
    m_config.renderHeight = height;
}

void ForwardRenderer::beginFrame() {
    m_stats = RenderStats();
}

void ForwardRenderer::render(World& world, const CameraData& camera) {
    int width = m_config.displayWidth;
    int height = m_config.displayHeight;

    // Shadow pass
    if (m_config.enableShadows && m_lighting.lightDir.y > 0.05f) {
        glViewport(0, 0, m_shadowResolution, m_shadowResolution);
        glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFBO);
        glClear(GL_DEPTH_BUFFER_BIT);
        glCullFace(GL_FRONT);

        // Calculate light space matrix
        float shadowDist = 60.0f;
        glm::mat4 lightProj = glm::ortho(-shadowDist, shadowDist, -shadowDist, shadowDist, 1.0f, 250.0f);
        glm::vec3 lightPos = camera.position + m_lighting.lightDir * 120.0f;
        glm::mat4 lightView = glm::lookAt(lightPos, camera.position, glm::vec3(0, 1, 0));
        glm::mat4 lightSpaceMatrix = lightProj * lightView;

        glUseProgram(m_shadowShader);
        glUniformMatrix4fv(glGetUniformLocation(m_shadowShader, "lightSpaceMatrix"),
                           1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));

        // world.render(camera.position, chunkOffsetLoc);

        glCullFace(GL_BACK);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // Main pass
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Render sky
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glUseProgram(m_skyShader);
    glUniformMatrix4fv(glGetUniformLocation(m_skyShader, "invView"),
                       1, GL_FALSE, glm::value_ptr(camera.invView));
    glUniformMatrix4fv(glGetUniformLocation(m_skyShader, "invProjection"),
                       1, GL_FALSE, glm::value_ptr(camera.invProjection));
    glBindVertexArray(m_skyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    // Render world
    glUseProgram(m_mainShader);
    glUniformMatrix4fv(glGetUniformLocation(m_mainShader, "view"),
                       1, GL_FALSE, glm::value_ptr(camera.view));
    glUniformMatrix4fv(glGetUniformLocation(m_mainShader, "projection"),
                       1, GL_FALSE, glm::value_ptr(camera.projection));
    glUniform3fv(glGetUniformLocation(m_mainShader, "lightDir"),
                 1, glm::value_ptr(m_lighting.lightDir));
    glUniform3fv(glGetUniformLocation(m_mainShader, "lightColor"),
                 1, glm::value_ptr(m_lighting.lightColor));
    glUniform3fv(glGetUniformLocation(m_mainShader, "ambientColor"),
                 1, glm::value_ptr(m_lighting.ambientColor));
    glUniform3fv(glGetUniformLocation(m_mainShader, "skyColor"),
                 1, glm::value_ptr(m_lighting.skyColor));

    // world.render(camera.position, chunkOffsetLoc);

    m_stats.chunksRendered = world.lastRenderedChunks;
    m_stats.chunksCulled = world.lastCulledChunks;
}

void ForwardRenderer::endFrame() {
    // Nothing to do
}

void ForwardRenderer::setConfig(const RenderConfig& config) {
    m_config = config;
}

void ForwardRenderer::setLighting(const LightingParams& lighting) {
    m_lighting = lighting;
}

void ForwardRenderer::setFog(const FogParams& fog) {
    m_fog = fog;
}

void ForwardRenderer::setDebugMode(int mode) {
    m_config.debugMode = mode;
}

} // namespace Render
