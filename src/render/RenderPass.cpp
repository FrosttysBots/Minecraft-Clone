#include "RenderPass.h"

#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <random>
#include <algorithm>

// Forward declaration for World rendering
#include "../world/World.h"
#include "../world/Chunk.h"

namespace Render {

// ============================================================================
// RenderPass Base Class
// ============================================================================

void RenderPass::beginTiming() {
    if (!m_timerQueriesCreated) {
        glGenQueries(2, m_timerQueries);
        m_timerQueriesCreated = true;
    }
    glBeginQuery(GL_TIME_ELAPSED, m_timerQueries[0]);
}

void RenderPass::endTiming() {
    glEndQuery(GL_TIME_ELAPSED);

    // Get result from previous frame's query (avoids stall)
    GLuint64 timeNs = 0;
    glGetQueryObjectui64v(m_timerQueries[1], GL_QUERY_RESULT, &timeNs);
    m_executionTimeMs = static_cast<float>(timeNs) / 1000000.0f;

    // Swap queries for next frame
    std::swap(m_timerQueries[0], m_timerQueries[1]);
}

// ============================================================================
// ShadowPass Implementation
// ============================================================================

ShadowPass::ShadowPass() : RenderPass("Shadow") {}

ShadowPass::~ShadowPass() {
    shutdown();
}

bool ShadowPass::initialize(const RenderConfig& config) {
    m_resolution = config.shadowResolution;
    m_numCascades = config.numCascades;

    // Create shadow map texture array (one layer per cascade)
    glGenTextures(1, &m_shadowMapArray);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_shadowMapArray);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F,
                 m_resolution, m_resolution, m_numCascades,
                 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

    float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, borderColor);

    // Create framebuffer
    glGenFramebuffers(1, &m_shadowFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFBO);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_shadowMapArray, 0, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[ShadowPass] Framebuffer incomplete!" << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    std::cout << "[ShadowPass] Created " << m_numCascades << " cascade shadow maps ("
              << m_resolution << "x" << m_resolution << ")" << std::endl;

    return true;
}

void ShadowPass::shutdown() {
    if (m_shadowFBO) {
        glDeleteFramebuffers(1, &m_shadowFBO);
        m_shadowFBO = 0;
    }
    if (m_shadowMapArray) {
        glDeleteTextures(1, &m_shadowMapArray);
        m_shadowMapArray = 0;
    }
    if (m_shaderProgram) {
        glDeleteProgram(m_shaderProgram);
        m_shaderProgram = 0;
    }
}

void ShadowPass::resize(uint32_t /*width*/, uint32_t /*height*/) {
    // Shadow maps don't resize with window
}

void ShadowPass::execute(RenderContext& context) {
    if (!m_enabled || !context.lighting || context.lighting->lightDir.y <= 0.05f) {
        return;
    }

    beginTiming();

    // Calculate cascade splits
    calculateCascadeSplits(context.camera->nearPlane, context.camera->farPlane);

    glViewport(0, 0, m_resolution, m_resolution);
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFBO);
    glCullFace(GL_FRONT);  // Reduce peter panning

    for (uint32_t cascade = 0; cascade < m_numCascades; cascade++) {
        float nearSplit = (cascade == 0) ? context.camera->nearPlane : m_cascadeSplits[cascade - 1];
        float farSplit = m_cascadeSplits[cascade];

        m_cascadeMatrices[cascade] = calculateCascadeMatrix(
            *context.camera, nearSplit, farSplit, context.lighting->lightDir);

        // Render to this cascade layer
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  m_shadowMapArray, 0, cascade);
        glClear(GL_DEPTH_BUFFER_BIT);

        glUseProgram(m_shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "lightSpaceMatrix"),
                           1, GL_FALSE, glm::value_ptr(m_cascadeMatrices[cascade]));

        // Render world geometry (shadow-specific limited distance rendering)
        // This would call world->renderForShadow() in the actual implementation
        if (context.world) {
            // context.world->renderForShadow(context.camera->position, chunkOffsetLoc, cascadeDistances[cascade]);
        }
    }

    glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Store results in context
    context.cascadeShadowMaps = m_shadowMapArray;
    for (uint32_t i = 0; i < m_numCascades; i++) {
        context.cascadeMatrices[i] = m_cascadeMatrices[i];
        context.cascadeSplits[i] = m_cascadeSplits[i];
    }

    endTiming();
    context.stats.shadowTime = m_executionTimeMs;
}

void ShadowPass::calculateCascadeSplits(float nearPlane, float farPlane) {
    // Practical split scheme: blend between logarithmic and uniform
    const float lambda = 0.5f;
    float ratio = farPlane / nearPlane;

    for (uint32_t i = 0; i < m_numCascades; i++) {
        float p = static_cast<float>(i + 1) / static_cast<float>(m_numCascades);
        float logSplit = nearPlane * std::pow(ratio, p);
        float uniSplit = nearPlane + (farPlane - nearPlane) * p;
        m_cascadeSplits[i] = lambda * logSplit + (1.0f - lambda) * uniSplit;
    }
}

glm::mat4 ShadowPass::calculateCascadeMatrix(const CameraData& camera, float nearSplit,
                                              float farSplit, const glm::vec3& lightDir) {
    // Get frustum corners in world space
    glm::mat4 proj = glm::perspective(glm::radians(camera.fov), camera.aspectRatio, nearSplit, farSplit);
    glm::mat4 invViewProj = glm::inverse(proj * camera.view);

    std::vector<glm::vec4> corners;
    for (int x = 0; x < 2; x++) {
        for (int y = 0; y < 2; y++) {
            for (int z = 0; z < 2; z++) {
                glm::vec4 pt = invViewProj * glm::vec4(
                    2.0f * x - 1.0f, 2.0f * y - 1.0f, 2.0f * z - 1.0f, 1.0f);
                corners.push_back(pt / pt.w);
            }
        }
    }

    // Calculate frustum center
    glm::vec3 center(0.0f);
    for (const auto& corner : corners) {
        center += glm::vec3(corner);
    }
    center /= static_cast<float>(corners.size());

    // Light view matrix
    glm::mat4 lightView = glm::lookAt(center - lightDir * 100.0f, center, glm::vec3(0, 1, 0));

    // Find bounding box in light space
    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();

    for (const auto& corner : corners) {
        glm::vec4 lsCorner = lightView * corner;
        minX = std::min(minX, lsCorner.x);
        maxX = std::max(maxX, lsCorner.x);
        minY = std::min(minY, lsCorner.y);
        maxY = std::max(maxY, lsCorner.y);
        minZ = std::min(minZ, lsCorner.z);
        maxZ = std::max(maxZ, lsCorner.z);
    }

    // Expand Z range to include shadow casters
    float zMult = 10.0f;
    if (minZ < 0) minZ *= zMult; else minZ /= zMult;
    if (maxZ < 0) maxZ /= zMult; else maxZ *= zMult;

    glm::mat4 lightProj = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);
    return lightProj * lightView;
}

// ============================================================================
// ZPrepass Implementation
// ============================================================================

ZPrepass::ZPrepass() : RenderPass("ZPrepass") {}

ZPrepass::~ZPrepass() {
    shutdown();
}

bool ZPrepass::initialize(const RenderConfig& /*config*/) {
    // Shader would be loaded here
    return true;
}

void ZPrepass::shutdown() {
    if (m_shaderProgram) {
        glDeleteProgram(m_shaderProgram);
        m_shaderProgram = 0;
    }
}

void ZPrepass::resize(uint32_t /*width*/, uint32_t /*height*/) {
    // Nothing to resize for Z-prepass
}

void ZPrepass::execute(RenderContext& context) {
    if (!m_enabled) return;

    // Disable color writes - depth only
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);

    glClear(GL_DEPTH_BUFFER_BIT);

    glUseProgram(m_shaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "view"),
                       1, GL_FALSE, glm::value_ptr(context.camera->view));
    glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "projection"),
                       1, GL_FALSE, glm::value_ptr(context.camera->projection));

    // Render world depth-only
    if (context.world) {
        // context.world->render(context.camera->position, chunkOffsetLoc);
    }

    // Restore color writes
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

// ============================================================================
// GBufferPass Implementation
// ============================================================================

GBufferPass::GBufferPass() : RenderPass("GBuffer") {}

GBufferPass::~GBufferPass() {
    shutdown();
}

bool GBufferPass::initialize(const RenderConfig& config) {
    m_width = config.renderWidth;
    m_height = config.renderHeight;
    createGBuffer(m_width, m_height);
    return m_gBufferFBO != 0;
}

void GBufferPass::shutdown() {
    destroyGBuffer();
    if (m_shaderProgram) {
        glDeleteProgram(m_shaderProgram);
        m_shaderProgram = 0;
    }
}

void GBufferPass::resize(uint32_t width, uint32_t height) {
    if (width != m_width || height != m_height) {
        destroyGBuffer();
        createGBuffer(width, height);
    }
}

void GBufferPass::createGBuffer(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;

    glGenFramebuffers(1, &m_gBufferFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferFBO);

    // Position + AO (RGBA16F)
    glGenTextures(1, &m_gPosition);
    glBindTexture(GL_TEXTURE_2D, m_gPosition);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_gPosition, 0);

    // Normal + Light Level (RGBA16F)
    glGenTextures(1, &m_gNormal);
    glBindTexture(GL_TEXTURE_2D, m_gNormal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_gNormal, 0);

    // Albedo + Emission (RGBA8)
    glGenTextures(1, &m_gAlbedo);
    glBindTexture(GL_TEXTURE_2D, m_gAlbedo);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, m_gAlbedo, 0);

    // Depth buffer
    glGenTextures(1, &m_gDepth);
    glBindTexture(GL_TEXTURE_2D, m_gDepth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_gDepth, 0);

    // Set draw buffers
    GLenum drawBuffers[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3, drawBuffers);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[GBufferPass] Framebuffer incomplete!" << std::endl;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    std::cout << "[GBufferPass] Created G-buffer (" << width << "x" << height << ")" << std::endl;
}

void GBufferPass::destroyGBuffer() {
    if (m_gBufferFBO) {
        glDeleteFramebuffers(1, &m_gBufferFBO);
        m_gBufferFBO = 0;
    }
    if (m_gPosition) { glDeleteTextures(1, &m_gPosition); m_gPosition = 0; }
    if (m_gNormal) { glDeleteTextures(1, &m_gNormal); m_gNormal = 0; }
    if (m_gAlbedo) { glDeleteTextures(1, &m_gAlbedo); m_gAlbedo = 0; }
    if (m_gDepth) { glDeleteTextures(1, &m_gDepth); m_gDepth = 0; }
}

void GBufferPass::execute(RenderContext& context) {
    if (!m_enabled) return;

    beginTiming();

    glViewport(0, 0, m_width, m_height);
    glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferFBO);

    // Set draw buffers
    GLenum drawBuffers[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3, drawBuffers);

    // Use GL_LEQUAL after Z-prepass (draw at same or closer depth)
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);  // Don't write depth again

    // Clear color only (keep Z-prepass depth)
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_shaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "view"),
                       1, GL_FALSE, glm::value_ptr(context.camera->view));
    glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "projection"),
                       1, GL_FALSE, glm::value_ptr(context.camera->projection));

    // Bind texture atlas
    if (context.textureAtlas) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, context.textureAtlas);
        glUniform1i(glGetUniformLocation(m_shaderProgram, "texAtlas"), 0);
    }

    // Render world geometry
    if (context.world) {
        // context.world->render(context.camera->position, chunkOffsetLoc);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Restore depth state
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);

    // Store results in context
    context.gPosition = m_gPosition;
    context.gNormal = m_gNormal;
    context.gAlbedo = m_gAlbedo;
    context.gDepth = m_gDepth;

    endTiming();
    context.stats.gbufferTime = m_executionTimeMs;
}

// ============================================================================
// HiZPass Implementation
// ============================================================================

HiZPass::HiZPass() : RenderPass("HiZ") {}

HiZPass::~HiZPass() {
    shutdown();
}

bool HiZPass::initialize(const RenderConfig& config) {
    m_width = config.renderWidth;
    m_height = config.renderHeight;
    createHiZBuffer(m_width, m_height);
    return m_hiZTexture != 0;
}

void HiZPass::shutdown() {
    destroyHiZBuffer();
    if (m_computeShader) {
        glDeleteProgram(m_computeShader);
        m_computeShader = 0;
    }
}

void HiZPass::resize(uint32_t width, uint32_t height) {
    if (width != m_width || height != m_height) {
        destroyHiZBuffer();
        createHiZBuffer(width, height);
    }
}

void HiZPass::createHiZBuffer(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;

    // Calculate mip levels
    m_mipLevels = 1 + static_cast<int>(std::floor(std::log2(std::max(width, height))));

    glGenTextures(1, &m_hiZTexture);
    glBindTexture(GL_TEXTURE_2D, m_hiZTexture);
    glTexStorage2D(GL_TEXTURE_2D, m_mipLevels, GL_R32F, width, height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    std::cout << "[HiZPass] Created Hi-Z buffer (" << width << "x" << height
              << ", " << m_mipLevels << " mips)" << std::endl;
}

void HiZPass::destroyHiZBuffer() {
    if (m_hiZTexture) {
        glDeleteTextures(1, &m_hiZTexture);
        m_hiZTexture = 0;
    }
}

void HiZPass::execute(RenderContext& context) {
    if (!m_enabled || !context.gDepth) return;

    beginTiming();

    // Copy G-buffer depth to Hi-Z level 0
    glCopyImageSubData(
        context.gDepth, GL_TEXTURE_2D, 0, 0, 0, 0,
        m_hiZTexture, GL_TEXTURE_2D, 0, 0, 0, 0,
        m_width, m_height, 1
    );

    // Generate mipmap pyramid using compute shader
    glUseProgram(m_computeShader);

    int currentWidth = m_width;
    int currentHeight = m_height;

    for (int level = 1; level < m_mipLevels; level++) {
        int srcLevel = level - 1;
        currentWidth = std::max(1, currentWidth / 2);
        currentHeight = std::max(1, currentHeight / 2);

        // Bind source (previous mip level)
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_hiZTexture);
        glUniform1i(glGetUniformLocation(m_computeShader, "srcDepth"), 0);
        glUniform1i(glGetUniformLocation(m_computeShader, "srcLevel"), srcLevel);

        // Bind destination (current mip level as image)
        glBindImageTexture(0, m_hiZTexture, level, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);

        // Dispatch compute shader
        int groupsX = (currentWidth + 7) / 8;
        int groupsY = (currentHeight + 7) / 8;
        glDispatchCompute(groupsX, groupsY, 1);

        // Memory barrier before next iteration
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    // Store results in context
    context.hiZTexture = m_hiZTexture;
    context.hiZMipLevels = m_mipLevels;

    endTiming();
    context.stats.hizTime = m_executionTimeMs;
}

// ============================================================================
// SSAOPass Implementation
// ============================================================================

SSAOPass::SSAOPass() : RenderPass("SSAO") {}

SSAOPass::~SSAOPass() {
    shutdown();
}

bool SSAOPass::initialize(const RenderConfig& config) {
    m_width = config.renderWidth;
    m_height = config.renderHeight;
    m_kernelSize = config.ssaoSamples;
    m_radius = config.ssaoRadius;
    m_bias = config.ssaoBias;

    createSSAOBuffers(m_width, m_height);
    generateKernelAndNoise();

    return m_ssaoFBO != 0;
}

void SSAOPass::shutdown() {
    destroySSAOBuffers();
    if (m_ssaoShader) { glDeleteProgram(m_ssaoShader); m_ssaoShader = 0; }
    if (m_blurShader) { glDeleteProgram(m_blurShader); m_blurShader = 0; }
}

void SSAOPass::resize(uint32_t width, uint32_t height) {
    if (width != m_width || height != m_height) {
        destroySSAOBuffers();
        createSSAOBuffers(width, height);
    }
}

void SSAOPass::createSSAOBuffers(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;

    // SSAO FBO
    glGenFramebuffers(1, &m_ssaoFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFBO);

    glGenTextures(1, &m_ssaoTexture);
    glBindTexture(GL_TEXTURE_2D, m_ssaoTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ssaoTexture, 0);

    // SSAO Blur FBO
    glGenFramebuffers(1, &m_ssaoBlurFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoBlurFBO);

    glGenTextures(1, &m_ssaoBlurred);
    glBindTexture(GL_TEXTURE_2D, m_ssaoBlurred);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ssaoBlurred, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    std::cout << "[SSAOPass] Created SSAO buffers (" << width << "x" << height << ")" << std::endl;
}

void SSAOPass::destroySSAOBuffers() {
    if (m_ssaoFBO) { glDeleteFramebuffers(1, &m_ssaoFBO); m_ssaoFBO = 0; }
    if (m_ssaoBlurFBO) { glDeleteFramebuffers(1, &m_ssaoBlurFBO); m_ssaoBlurFBO = 0; }
    if (m_ssaoTexture) { glDeleteTextures(1, &m_ssaoTexture); m_ssaoTexture = 0; }
    if (m_ssaoBlurred) { glDeleteTextures(1, &m_ssaoBlurred); m_ssaoBlurred = 0; }
    if (m_noiseTexture) { glDeleteTextures(1, &m_noiseTexture); m_noiseTexture = 0; }
}

void SSAOPass::generateKernelAndNoise() {
    std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
    std::default_random_engine generator;

    // Generate kernel samples
    m_ssaoKernel.clear();
    for (uint32_t i = 0; i < m_kernelSize; i++) {
        glm::vec3 sample(
            randomFloats(generator) * 2.0f - 1.0f,
            randomFloats(generator) * 2.0f - 1.0f,
            randomFloats(generator)
        );
        sample = glm::normalize(sample);
        sample *= randomFloats(generator);

        // Scale samples to cluster near origin
        float scale = static_cast<float>(i) / static_cast<float>(m_kernelSize);
        scale = 0.1f + scale * scale * 0.9f;
        sample *= scale;

        m_ssaoKernel.push_back(sample);
    }

    // Generate noise texture (4x4)
    std::vector<glm::vec3> ssaoNoise;
    for (int i = 0; i < 16; i++) {
        glm::vec3 noise(
            randomFloats(generator) * 2.0f - 1.0f,
            randomFloats(generator) * 2.0f - 1.0f,
            0.0f
        );
        ssaoNoise.push_back(noise);
    }

    glGenTextures(1, &m_noiseTexture);
    glBindTexture(GL_TEXTURE_2D, m_noiseTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, ssaoNoise.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

void SSAOPass::execute(RenderContext& context) {
    if (!m_enabled) return;

    beginTiming();

    // SSAO pass
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFBO);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_ssaoShader);

    // Bind G-buffer textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, context.gPosition);
    glUniform1i(glGetUniformLocation(m_ssaoShader, "gPosition"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, context.gNormal);
    glUniform1i(glGetUniformLocation(m_ssaoShader, "gNormal"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, context.gDepth);
    glUniform1i(glGetUniformLocation(m_ssaoShader, "gDepth"), 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_noiseTexture);
    glUniform1i(glGetUniformLocation(m_ssaoShader, "texNoise"), 3);

    // Set uniforms
    glUniformMatrix4fv(glGetUniformLocation(m_ssaoShader, "projection"),
                       1, GL_FALSE, glm::value_ptr(context.camera->projection));
    glUniformMatrix4fv(glGetUniformLocation(m_ssaoShader, "view"),
                       1, GL_FALSE, glm::value_ptr(context.camera->view));
    glUniform2f(glGetUniformLocation(m_ssaoShader, "noiseScale"),
                m_width / 4.0f, m_height / 4.0f);
    glUniform1f(glGetUniformLocation(m_ssaoShader, "radius"), m_radius);
    glUniform1f(glGetUniformLocation(m_ssaoShader, "bias"), m_bias);

    // Upload kernel samples
    for (uint32_t i = 0; i < m_kernelSize; i++) {
        std::string uniformName = "samples[" + std::to_string(i) + "]";
        glUniform3fv(glGetUniformLocation(m_ssaoShader, uniformName.c_str()),
                     1, glm::value_ptr(m_ssaoKernel[i]));
    }

    // Render fullscreen quad (would need quad VAO)
    // glBindVertexArray(quadVAO);
    // glDrawArrays(GL_TRIANGLES, 0, 6);

    // SSAO blur pass
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoBlurFBO);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_blurShader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_ssaoTexture);
    glUniform1i(glGetUniformLocation(m_blurShader, "ssaoInput"), 0);

    // glBindVertexArray(quadVAO);
    // glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Store result in context
    context.ssaoTexture = m_ssaoBlurred;

    endTiming();
    context.stats.ssaoTime = m_executionTimeMs;
}

// ============================================================================
// CompositePass Implementation
// ============================================================================

CompositePass::CompositePass() : RenderPass("Composite") {}

CompositePass::~CompositePass() {
    shutdown();
}

bool CompositePass::initialize(const RenderConfig& config) {
    m_width = config.renderWidth;
    m_height = config.renderHeight;
    createSceneBuffer(m_width, m_height);

    // Create fullscreen quad
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

    return m_sceneFBO != 0;
}

void CompositePass::shutdown() {
    destroySceneBuffer();
    if (m_shaderProgram) { glDeleteProgram(m_shaderProgram); m_shaderProgram = 0; }
    if (m_quadVAO) { glDeleteVertexArrays(1, &m_quadVAO); m_quadVAO = 0; }
    if (m_quadVBO) { glDeleteBuffers(1, &m_quadVBO); m_quadVBO = 0; }
}

void CompositePass::resize(uint32_t width, uint32_t height) {
    if (width != m_width || height != m_height) {
        destroySceneBuffer();
        createSceneBuffer(width, height);
    }
}

void CompositePass::createSceneBuffer(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;

    glGenFramebuffers(1, &m_sceneFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFBO);

    // Scene color texture
    glGenTextures(1, &m_sceneColor);
    glBindTexture(GL_TEXTURE_2D, m_sceneColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sceneColor, 0);

    // Scene depth texture (for sky rendering)
    glGenTextures(1, &m_sceneDepth);
    glBindTexture(GL_TEXTURE_2D, m_sceneDepth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_sceneDepth, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[CompositePass] Framebuffer incomplete!" << std::endl;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    std::cout << "[CompositePass] Created scene buffer (" << width << "x" << height << ")" << std::endl;
}

void CompositePass::destroySceneBuffer() {
    if (m_sceneFBO) { glDeleteFramebuffers(1, &m_sceneFBO); m_sceneFBO = 0; }
    if (m_sceneColor) { glDeleteTextures(1, &m_sceneColor); m_sceneColor = 0; }
    if (m_sceneDepth) { glDeleteTextures(1, &m_sceneDepth); m_sceneDepth = 0; }
}

void CompositePass::execute(RenderContext& context) {
    if (!m_enabled) return;

    beginTiming();

    // Bind scene FBO (for FSR) or screen
    if (context.config->enableFSR && m_sceneFBO != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFBO);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    glViewport(0, 0, m_width, m_height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(m_shaderProgram);

    // Bind G-buffer textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, context.gPosition);
    glUniform1i(glGetUniformLocation(m_shaderProgram, "gPosition"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, context.gNormal);
    glUniform1i(glGetUniformLocation(m_shaderProgram, "gNormal"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, context.gAlbedo);
    glUniform1i(glGetUniformLocation(m_shaderProgram, "gAlbedo"), 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, context.gDepth);
    glUniform1i(glGetUniformLocation(m_shaderProgram, "gDepth"), 3);

    // Bind SSAO texture
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, context.ssaoTexture);
    glUniform1i(glGetUniformLocation(m_shaderProgram, "ssaoTexture"), 4);
    glUniform1i(glGetUniformLocation(m_shaderProgram, "enableSSAO"), context.config->enableSSAO ? 1 : 0);

    // Bind cascade shadow maps
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D_ARRAY, context.cascadeShadowMaps);
    glUniform1i(glGetUniformLocation(m_shaderProgram, "cascadeShadowMaps"), 5);

    // Set cascade matrices and splits
    glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "cascadeMatrices"),
                       3, GL_FALSE, glm::value_ptr(context.cascadeMatrices[0]));
    glUniform1fv(glGetUniformLocation(m_shaderProgram, "cascadeSplits"), 3, context.cascadeSplits);

    // Lighting uniforms
    if (context.lighting) {
        glUniform3fv(glGetUniformLocation(m_shaderProgram, "lightDir"),
                     1, glm::value_ptr(context.lighting->lightDir));
        glUniform3fv(glGetUniformLocation(m_shaderProgram, "lightColor"),
                     1, glm::value_ptr(context.lighting->lightColor));
        glUniform3fv(glGetUniformLocation(m_shaderProgram, "ambientColor"),
                     1, glm::value_ptr(context.lighting->ambientColor));
        glUniform3fv(glGetUniformLocation(m_shaderProgram, "skyColor"),
                     1, glm::value_ptr(context.lighting->skyColor));
        glUniform1f(glGetUniformLocation(m_shaderProgram, "shadowStrength"),
                    context.lighting->shadowStrength);
        glUniform1f(glGetUniformLocation(m_shaderProgram, "time"), context.lighting->time);
    }

    // Camera and fog
    glUniform3fv(glGetUniformLocation(m_shaderProgram, "cameraPos"),
                 1, glm::value_ptr(context.camera->position));

    if (context.fog) {
        glUniform1f(glGetUniformLocation(m_shaderProgram, "fogDensity"), context.fog->density);
        glUniform1f(glGetUniformLocation(m_shaderProgram, "isUnderwater"),
                    context.fog->isUnderwater ? 1.0f : 0.0f);
        glUniform1f(glGetUniformLocation(m_shaderProgram, "renderDistanceBlocks"),
                    context.fog->renderDistance);
    }

    // Inverse view-projection for position reconstruction
    glm::mat4 invViewProj = glm::inverse(context.camera->viewProjection);
    glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "invViewProj"),
                       1, GL_FALSE, glm::value_ptr(invViewProj));

    glUniform1i(glGetUniformLocation(m_shaderProgram, "debugMode"), context.config->debugMode);

    // Render fullscreen quad
    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    // Re-enable depth test for subsequent passes
    glEnable(GL_DEPTH_TEST);

    // Store results in context
    context.sceneColor = m_sceneColor;
    context.sceneDepth = m_sceneDepth;

    endTiming();
    context.stats.compositeTime = m_executionTimeMs;
}

// ============================================================================
// SkyPass Implementation
// ============================================================================

SkyPass::SkyPass() : RenderPass("Sky") {}

SkyPass::~SkyPass() {
    shutdown();
}

bool SkyPass::initialize(const RenderConfig& /*config*/) {
    // Create sky fullscreen quad
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

    return true;
}

void SkyPass::shutdown() {
    if (m_shaderProgram) { glDeleteProgram(m_shaderProgram); m_shaderProgram = 0; }
    if (m_skyVAO) { glDeleteVertexArrays(1, &m_skyVAO); m_skyVAO = 0; }
    if (m_skyVBO) { glDeleteBuffers(1, &m_skyVBO); m_skyVBO = 0; }
}

void SkyPass::resize(uint32_t /*width*/, uint32_t /*height*/) {
    // Nothing to resize for sky pass
}

void SkyPass::execute(RenderContext& context) {
    if (!m_enabled) return;

    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);

    glUseProgram(m_shaderProgram);

    glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "invView"),
                       1, GL_FALSE, glm::value_ptr(context.camera->invView));
    glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "invProjection"),
                       1, GL_FALSE, glm::value_ptr(context.camera->invProjection));
    glUniform3fv(glGetUniformLocation(m_shaderProgram, "cameraPos"),
                 1, glm::value_ptr(context.camera->position));

    if (context.lighting) {
        glUniform3fv(glGetUniformLocation(m_shaderProgram, "sunDir"),
                     1, glm::value_ptr(context.lighting->lightDir));
        glUniform3fv(glGetUniformLocation(m_shaderProgram, "skyTop"),
                     1, glm::value_ptr(context.lighting->skyColor));
        glm::vec3 skyBottom = glm::mix(context.lighting->skyColor,
                                        glm::vec3(0.9f, 0.85f, 0.8f), 0.3f);
        glUniform3fv(glGetUniformLocation(m_shaderProgram, "skyBottom"),
                     1, glm::value_ptr(skyBottom));
        glUniform1f(glGetUniformLocation(m_shaderProgram, "time"), context.time);
    }

    glBindVertexArray(m_skyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    context.stats.skyTime = m_executionTimeMs;
}

// ============================================================================
// FSRPass Implementation
// ============================================================================

FSRPass::FSRPass() : RenderPass("FSR") {}

FSRPass::~FSRPass() {
    shutdown();
}

bool FSRPass::initialize(const RenderConfig& config) {
    m_renderWidth = config.renderWidth;
    m_renderHeight = config.renderHeight;
    m_displayWidth = config.displayWidth;
    m_displayHeight = config.displayHeight;

    createBuffers();

    // Create fullscreen quad
    float quadVertices[] = {
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

    return true;
}

void FSRPass::shutdown() {
    destroyBuffers();
    if (m_easuShader) { glDeleteProgram(m_easuShader); m_easuShader = 0; }
    if (m_rcasShader) { glDeleteProgram(m_rcasShader); m_rcasShader = 0; }
    if (m_quadVAO) { glDeleteVertexArrays(1, &m_quadVAO); m_quadVAO = 0; }
    if (m_quadVBO) { glDeleteBuffers(1, &m_quadVBO); m_quadVBO = 0; }
}

void FSRPass::resize(uint32_t width, uint32_t height) {
    if (width != m_displayWidth || height != m_displayHeight) {
        m_displayWidth = width;
        m_displayHeight = height;
        destroyBuffers();
        createBuffers();
    }
}

void FSRPass::setDimensions(uint32_t renderWidth, uint32_t renderHeight,
                            uint32_t displayWidth, uint32_t displayHeight) {
    m_renderWidth = renderWidth;
    m_renderHeight = renderHeight;
    m_displayWidth = displayWidth;
    m_displayHeight = displayHeight;
    destroyBuffers();
    createBuffers();
}

void FSRPass::createBuffers() {
    // Intermediate buffer at display resolution (for EASU output)
    glGenFramebuffers(1, &m_intermediateFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_intermediateFBO);

    glGenTextures(1, &m_intermediateTexture);
    glBindTexture(GL_TEXTURE_2D, m_intermediateTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, m_displayWidth, m_displayHeight,
                 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           m_intermediateTexture, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    std::cout << "[FSRPass] Created buffers (render: " << m_renderWidth << "x" << m_renderHeight
              << ", display: " << m_displayWidth << "x" << m_displayHeight << ")" << std::endl;
}

void FSRPass::destroyBuffers() {
    if (m_intermediateFBO) { glDeleteFramebuffers(1, &m_intermediateFBO); m_intermediateFBO = 0; }
    if (m_intermediateTexture) { glDeleteTextures(1, &m_intermediateTexture); m_intermediateTexture = 0; }
    if (m_outputFBO) { glDeleteFramebuffers(1, &m_outputFBO); m_outputFBO = 0; }
    if (m_outputTexture) { glDeleteTextures(1, &m_outputTexture); m_outputTexture = 0; }
}

void FSRPass::execute(RenderContext& context) {
    if (!m_enabled || !context.config->enableFSR) return;

    // Switch to screen framebuffer at display resolution
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_displayWidth, m_displayHeight);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    // FSR EASU (Edge Adaptive Spatial Upscaling)
    glUseProgram(m_easuShader);

    // Bind scene texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, context.sceneColor);
    glUniform1i(glGetUniformLocation(m_easuShader, "inputTexture"), 0);

    // Set resolution uniforms
    glUniform2f(glGetUniformLocation(m_easuShader, "inputSize"),
                static_cast<float>(m_renderWidth), static_cast<float>(m_renderHeight));
    glUniform2f(glGetUniformLocation(m_easuShader, "outputSize"),
                static_cast<float>(m_displayWidth), static_cast<float>(m_displayHeight));

    // FSR constants
    float scaleX = static_cast<float>(m_renderWidth) / static_cast<float>(m_displayWidth);
    float scaleY = static_cast<float>(m_renderHeight) / static_cast<float>(m_displayHeight);
    glUniform4f(glGetUniformLocation(m_easuShader, "con0"),
                scaleX, scaleY, 0.5f * scaleX - 0.5f, 0.5f * scaleY - 0.5f);
    glUniform4f(glGetUniformLocation(m_easuShader, "con1"),
                1.0f / m_renderWidth, 1.0f / m_renderHeight,
                1.0f / m_renderWidth, -1.0f / m_renderHeight);
    glUniform4f(glGetUniformLocation(m_easuShader, "con2"),
                -1.0f / m_renderWidth, 2.0f / m_renderHeight,
                1.0f / m_renderWidth, 2.0f / m_renderHeight);
    glUniform4f(glGetUniformLocation(m_easuShader, "con3"),
                0.0f, 4.0f / m_renderHeight, 0.0f, 0.0f);

    // Render fullscreen quad
    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glActiveTexture(GL_TEXTURE0);
}

} // namespace Render
