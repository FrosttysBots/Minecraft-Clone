#include "RenderPassRHI.h"
#include "WorldRendererRHI.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <random>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Render {

// ============================================================================
// ShadowPassRHI Implementation
// ============================================================================

ShadowPassRHI::ShadowPassRHI(RHI::RHIDevice* device)
    : RenderPassRHI("ShadowRHI", device) {}

ShadowPassRHI::~ShadowPassRHI() {
    shutdown();
}

bool ShadowPassRHI::initialize(const RenderConfig& config) {
    m_resolution = config.shadowResolution;
    m_numCascades = config.numCascades;

    // Create shadow map texture array (one layer per cascade)
    RHI::TextureDesc shadowDesc{};
    shadowDesc.type = RHI::TextureType::Texture2DArray;
    shadowDesc.format = RHI::Format::D32_FLOAT;
    shadowDesc.width = m_resolution;
    shadowDesc.height = m_resolution;
    shadowDesc.depth = 1;
    shadowDesc.arrayLayers = m_numCascades;
    shadowDesc.mipLevels = 1;
    shadowDesc.samples = 1;
    shadowDesc.usage = RHI::TextureUsage::DepthStencil | RHI::TextureUsage::Sampled;
    shadowDesc.debugName = "CascadeShadowMap";

    m_shadowMapArray = m_device->createTexture(shadowDesc);
    if (!m_shadowMapArray) {
        std::cerr << "[ShadowPassRHI] Failed to create shadow map array" << std::endl;
        return false;
    }

    // Create render pass for shadow rendering
    RHI::RenderPassDesc rpDesc{};
    rpDesc.depthStencilAttachment.format = RHI::Format::D32_FLOAT;
    rpDesc.depthStencilAttachment.loadOp = RHI::LoadOp::Clear;
    rpDesc.depthStencilAttachment.storeOp = RHI::StoreOp::Store;
    rpDesc.hasDepthStencil = true;

    m_renderPass = m_device->createRenderPass(rpDesc);
    if (!m_renderPass) {
        std::cerr << "[ShadowPassRHI] Failed to create render pass" << std::endl;
        return false;
    }

    // Create framebuffer for each cascade layer
    m_cascadeFramebuffers.resize(m_numCascades);
    for (uint32_t i = 0; i < m_numCascades; i++) {
        RHI::FramebufferDesc fbDesc{};
        fbDesc.renderPass = m_renderPass.get();
        fbDesc.width = m_resolution;
        fbDesc.height = m_resolution;
        fbDesc.depthStencilAttachment.texture = m_shadowMapArray.get();
        fbDesc.depthStencilAttachment.arrayLayer = i;

        m_cascadeFramebuffers[i] = m_device->createFramebuffer(fbDesc);
        if (!m_cascadeFramebuffers[i]) {
            std::cerr << "[ShadowPassRHI] Failed to create cascade framebuffer " << i << std::endl;
            return false;
        }
    }

    // Create uniform buffer for cascade matrices
    RHI::BufferDesc uboDesc{};
    uboDesc.size = sizeof(glm::mat4) * 4 + sizeof(float) * 4;
    uboDesc.usage = RHI::BufferUsage::Uniform;
    uboDesc.memory = RHI::MemoryUsage::CpuToGpu;
    uboDesc.debugName = "CascadeUBO";

    m_cascadeUBO = m_device->createBuffer(uboDesc);

    std::cout << "[ShadowPassRHI] Created " << m_numCascades << " cascade shadow maps ("
              << m_resolution << "x" << m_resolution << ")" << std::endl;

    return true;
}

void ShadowPassRHI::shutdown() {
    m_cascadeFramebuffers.clear();
    m_renderPass.reset();
    m_shadowMapArray.reset();
    m_cascadeUBO.reset();
    m_descriptorSet.reset();
}

void ShadowPassRHI::resize(uint32_t /*width*/, uint32_t /*height*/) {
    // Shadow maps don't resize with window
}

void ShadowPassRHI::execute(RHI::RHICommandBuffer* cmd, RenderContext& context) {
    if (!m_enabled || !context.lighting || context.lighting->lightDir.y <= 0.05f) {
        return;
    }

    // Calculate cascade splits
    calculateCascadeSplits(context.camera->nearPlane, context.camera->farPlane);

    // Set viewport for shadow map resolution
    RHI::Viewport shadowViewport{};
    shadowViewport.width = static_cast<float>(m_resolution);
    shadowViewport.height = static_cast<float>(m_resolution);
    shadowViewport.minDepth = 0.0f;
    shadowViewport.maxDepth = 1.0f;

    RHI::Scissor shadowScissor{};
    shadowScissor.width = m_resolution;
    shadowScissor.height = m_resolution;

    // Render each cascade
    for (uint32_t cascade = 0; cascade < m_numCascades && cascade < m_cascadeFramebuffers.size(); cascade++) {
        if (!m_cascadeFramebuffers[cascade]) continue;

        float nearSplit = (cascade == 0) ? context.camera->nearPlane : m_cascadeSplits[cascade - 1];
        float farSplit = m_cascadeSplits[cascade];

        m_cascadeMatrices[cascade] = calculateCascadeMatrix(
            *context.camera, nearSplit, farSplit, context.lighting->lightDir);

        // Begin shadow render pass for this cascade
        std::vector<RHI::ClearValue> clearValues;
        clearValues.push_back(RHI::ClearValue::DepthStencil(1.0f, 0));

        cmd->beginRenderPass(m_renderPass.get(), m_cascadeFramebuffers[cascade].get(), clearValues);
        cmd->setViewport(shadowViewport);
        cmd->setScissor(shadowScissor);

        // Bind shadow pipeline
        if (m_pipeline) {
            cmd->bindGraphicsPipeline(m_pipeline);
        }

        // Update cascade uniform buffer with light-space matrix
        if (m_cascadeUBO) {
            void* mapped = m_cascadeUBO->map();
            if (mapped) {
                // Write light space matrix for this cascade
                memcpy(mapped, glm::value_ptr(m_cascadeMatrices[cascade]), sizeof(glm::mat4));
                m_cascadeUBO->unmap();
            }
        }

        // Bind descriptor set
        if (m_descriptorSet) {
            cmd->bindDescriptorSet(0, m_descriptorSet.get());
        }

        // World shadow rendering would happen here
        // For now, the World class still uses OpenGL for rendering
        // TODO: Refactor World to support RHI shadow rendering

        cmd->endRenderPass();
    }

    // Store results in context
    if (m_shadowMapArray) {
        context.cascadeShadowMaps = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(m_shadowMapArray->getNativeHandle()));
    }
    for (uint32_t i = 0; i < m_numCascades && i < 4; i++) {
        context.cascadeMatrices[i] = m_cascadeMatrices[i];
        context.cascadeSplits[i] = m_cascadeSplits[i];
    }

    context.stats.shadowTime = m_executionTimeMs;
}

void ShadowPassRHI::calculateCascadeSplits(float nearPlane, float farPlane) {
    const float lambda = 0.95f;
    const float range = farPlane - nearPlane;
    const float ratio = farPlane / nearPlane;

    for (uint32_t i = 0; i < std::min(m_numCascades, 4u); i++) {
        float p = static_cast<float>(i + 1) / static_cast<float>(m_numCascades);
        float log_split = nearPlane * std::pow(ratio, p);
        float uniform_split = nearPlane + range * p;
        m_cascadeSplits[i] = lambda * log_split + (1.0f - lambda) * uniform_split;
    }
}

glm::mat4 ShadowPassRHI::calculateCascadeMatrix(const CameraData& camera, float nearSplit, float farSplit,
                                                 const glm::vec3& lightDir) {
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

    // Find frustum center
    glm::vec3 center(0.0f);
    for (const auto& corner : corners) {
        center += glm::vec3(corner);
    }
    center /= static_cast<float>(corners.size());

    // Light view matrix
    glm::mat4 lightView = glm::lookAt(center + lightDir * 50.0f, center, glm::vec3(0, 1, 0));

    // Find bounding box in light space
    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();

    for (const auto& corner : corners) {
        glm::vec4 lightSpaceCorner = lightView * corner;
        minX = std::min(minX, lightSpaceCorner.x);
        maxX = std::max(maxX, lightSpaceCorner.x);
        minY = std::min(minY, lightSpaceCorner.y);
        maxY = std::max(maxY, lightSpaceCorner.y);
        minZ = std::min(minZ, lightSpaceCorner.z);
        maxZ = std::max(maxZ, lightSpaceCorner.z);
    }

    // Add some padding and extend shadow distance
    float zMult = 10.0f;
    if (minZ < 0) minZ *= zMult;
    else minZ /= zMult;
    if (maxZ < 0) maxZ /= zMult;
    else maxZ *= zMult;

    glm::mat4 lightProj = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);
    return lightProj * lightView;
}

// ============================================================================
// GBufferPassRHI Implementation
// ============================================================================

GBufferPassRHI::GBufferPassRHI(RHI::RHIDevice* device)
    : RenderPassRHI("GBufferRHI", device) {}

GBufferPassRHI::~GBufferPassRHI() {
    shutdown();
}

bool GBufferPassRHI::initialize(const RenderConfig& config) {
    createGBuffer(config.renderWidth, config.renderHeight);
    return m_gPosition != nullptr;
}

void GBufferPassRHI::shutdown() {
    destroyGBuffer();
}

void GBufferPassRHI::resize(uint32_t width, uint32_t height) {
    if (width != m_width || height != m_height) {
        destroyGBuffer();
        createGBuffer(width, height);
    }
}

void GBufferPassRHI::execute(RHI::RHICommandBuffer* cmd, RenderContext& context) {
    if (!m_enabled || !m_framebuffer || !m_renderPass) return;

    // Begin render pass with clear values
    std::vector<RHI::ClearValue> clearValues;
    clearValues.push_back(RHI::ClearValue::Color(0.0f, 0.0f, 0.0f, 0.0f));  // Position
    clearValues.push_back(RHI::ClearValue::Color(0.0f, 0.0f, 0.0f, 0.0f));  // Normal
    clearValues.push_back(RHI::ClearValue::Color(0.0f, 0.0f, 0.0f, 0.0f));  // Albedo
    clearValues.push_back(RHI::ClearValue::DepthStencil(1.0f, 0));          // Depth

    cmd->beginRenderPass(m_renderPass.get(), m_framebuffer.get(), clearValues);

    // Set viewport
    RHI::Viewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_width);
    viewport.height = static_cast<float>(m_height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    cmd->setViewport(viewport);

    // Set scissor
    RHI::Scissor scissor{};
    scissor.x = 0;
    scissor.y = 0;
    scissor.width = m_width;
    scissor.height = m_height;
    cmd->setScissor(scissor);

    // Bind pipeline if available
    if (m_pipeline) {
        cmd->bindGraphicsPipeline(m_pipeline);
    }

    // Update camera uniform buffer
    if (m_cameraUBO && context.camera) {
        struct CameraUniforms {
            glm::mat4 view;
            glm::mat4 projection;
            glm::vec4 position;
            glm::vec4 params;  // nearPlane, farPlane, fov, aspectRatio
        } uniforms;

        uniforms.view = context.camera->view;
        uniforms.projection = context.camera->projection;
        uniforms.position = glm::vec4(context.camera->position, 1.0f);
        uniforms.params = glm::vec4(
            context.camera->nearPlane,
            context.camera->farPlane,
            context.camera->fov,
            context.camera->aspectRatio
        );

        void* mapped = m_cameraUBO->map();
        if (mapped) {
            memcpy(mapped, &uniforms, sizeof(uniforms));
            m_cameraUBO->unmap();
        }
    }

    // Bind descriptor set with camera UBO and texture atlas
    if (m_descriptorSet) {
        cmd->bindDescriptorSet(0, m_descriptorSet.get());
    }

    // World rendering happens here
    // For now, the World class still uses raw OpenGL calls internally
    // This will be refactored later to use RHI command buffer
    // The OpenGL backend executes commands immediately, so this works
    if (context.world) {
        // World::renderSubChunks will be called by the main renderer
        // which still uses the OpenGL-based path
        // TODO: Refactor World to accept RHI command buffer
    }

    cmd->endRenderPass();

    // Store G-Buffer texture handles in context for subsequent passes
    storeTextureHandles(context);
}

void GBufferPassRHI::storeTextureHandles(RenderContext& context) {
    // Get native OpenGL handles from RHI textures for interop
    if (m_gPosition) {
        context.gPosition = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(m_gPosition->getNativeHandle()));
    }
    if (m_gNormal) {
        context.gNormal = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(m_gNormal->getNativeHandle()));
    }
    if (m_gAlbedo) {
        context.gAlbedo = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(m_gAlbedo->getNativeHandle()));
    }
    if (m_gDepth) {
        context.gDepth = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(m_gDepth->getNativeHandle()));
    }
}

void GBufferPassRHI::beginPass(RHI::RHICommandBuffer* cmd, RenderContext& context) {
    if (!m_enabled || !m_framebuffer || !m_renderPass) return;

    // Begin render pass with clear values
    std::vector<RHI::ClearValue> clearValues;
    clearValues.push_back(RHI::ClearValue::Color(0.0f, 0.0f, 0.0f, 0.0f));  // Position
    clearValues.push_back(RHI::ClearValue::Color(0.0f, 0.0f, 0.0f, 0.0f));  // Normal
    clearValues.push_back(RHI::ClearValue::Color(0.0f, 0.0f, 0.0f, 0.0f));  // Albedo
    clearValues.push_back(RHI::ClearValue::DepthStencil(1.0f, 0));          // Depth

    cmd->beginRenderPass(m_renderPass.get(), m_framebuffer.get(), clearValues);

    // Set viewport
    RHI::Viewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_width);
    viewport.height = static_cast<float>(m_height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    cmd->setViewport(viewport);

    // Set scissor
    RHI::Scissor scissor{};
    scissor.x = 0;
    scissor.y = 0;
    scissor.width = m_width;
    scissor.height = m_height;
    cmd->setScissor(scissor);

    // Update camera uniform buffer
    if (m_cameraUBO && context.camera) {
        struct CameraUniforms {
            glm::mat4 view;
            glm::mat4 projection;
            glm::vec4 position;
            glm::vec4 params;
        } uniforms;

        uniforms.view = context.camera->view;
        uniforms.projection = context.camera->projection;
        uniforms.position = glm::vec4(context.camera->position, 1.0f);
        uniforms.params = glm::vec4(
            context.camera->nearPlane,
            context.camera->farPlane,
            context.camera->fov,
            context.camera->aspectRatio
        );

        void* mapped = m_cameraUBO->map();
        if (mapped) {
            memcpy(mapped, &uniforms, sizeof(uniforms));
            m_cameraUBO->unmap();
        }
    }
}

void GBufferPassRHI::endPass(RHI::RHICommandBuffer* cmd) {
    cmd->endRenderPass();
}

void GBufferPassRHI::createGBuffer(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;

    RHI::TextureDesc texDesc{};
    texDesc.type = RHI::TextureType::Texture2D;
    texDesc.width = width;
    texDesc.height = height;
    texDesc.depth = 1;
    texDesc.arrayLayers = 1;
    texDesc.mipLevels = 1;
    texDesc.samples = 1;
    texDesc.usage = RHI::TextureUsage::RenderTarget | RHI::TextureUsage::Sampled;

    texDesc.format = RHI::Format::RGBA16_FLOAT;
    texDesc.debugName = "GBuffer_Position";
    m_gPosition = m_device->createTexture(texDesc);

    texDesc.debugName = "GBuffer_Normal";
    m_gNormal = m_device->createTexture(texDesc);

    texDesc.format = RHI::Format::RGBA8_UNORM;
    texDesc.debugName = "GBuffer_Albedo";
    m_gAlbedo = m_device->createTexture(texDesc);

    texDesc.format = RHI::Format::D32_FLOAT;
    texDesc.usage = RHI::TextureUsage::DepthStencil | RHI::TextureUsage::Sampled;
    texDesc.debugName = "GBuffer_Depth";
    m_gDepth = m_device->createTexture(texDesc);

    if (!m_gPosition || !m_gNormal || !m_gAlbedo || !m_gDepth) {
        std::cerr << "[GBufferPassRHI] Failed to create G-Buffer textures" << std::endl;
        return;
    }

    RHI::RenderPassDesc rpDesc{};
    RHI::AttachmentDesc colorAttach{};
    colorAttach.format = RHI::Format::RGBA16_FLOAT;
    colorAttach.loadOp = RHI::LoadOp::Clear;
    colorAttach.storeOp = RHI::StoreOp::Store;
    rpDesc.colorAttachments.push_back(colorAttach);
    rpDesc.colorAttachments.push_back(colorAttach);

    colorAttach.format = RHI::Format::RGBA8_UNORM;
    rpDesc.colorAttachments.push_back(colorAttach);

    rpDesc.depthStencilAttachment.format = RHI::Format::D32_FLOAT;
    rpDesc.depthStencilAttachment.loadOp = RHI::LoadOp::Clear;
    rpDesc.depthStencilAttachment.storeOp = RHI::StoreOp::Store;
    rpDesc.hasDepthStencil = true;

    m_renderPass = m_device->createRenderPass(rpDesc);

    RHI::FramebufferDesc fbDesc{};
    fbDesc.renderPass = m_renderPass.get();
    fbDesc.width = width;
    fbDesc.height = height;
    fbDesc.colorAttachments = {{m_gPosition.get()}, {m_gNormal.get()}, {m_gAlbedo.get()}};
    fbDesc.depthStencilAttachment.texture = m_gDepth.get();

    m_framebuffer = m_device->createFramebuffer(fbDesc);

    if (!m_cameraUBO) {
        RHI::BufferDesc uboDesc{};
        uboDesc.size = sizeof(glm::mat4) * 2 + sizeof(glm::vec4) * 2;
        uboDesc.usage = RHI::BufferUsage::Uniform;
        uboDesc.memory = RHI::MemoryUsage::CpuToGpu;
        uboDesc.debugName = "GBuffer_CameraUBO";
        m_cameraUBO = m_device->createBuffer(uboDesc);
    }

    std::cout << "[GBufferPassRHI] Created G-Buffer (" << width << "x" << height << ")" << std::endl;
}

void GBufferPassRHI::destroyGBuffer() {
    m_framebuffer.reset();
    m_renderPass.reset();
    m_gPosition.reset();
    m_gNormal.reset();
    m_gAlbedo.reset();
    m_gDepth.reset();
    m_width = 0;
    m_height = 0;
}

// ============================================================================
// HiZPassRHI Implementation
// ============================================================================

HiZPassRHI::HiZPassRHI(RHI::RHIDevice* device)
    : RenderPassRHI("HiZRHI", device) {}

HiZPassRHI::~HiZPassRHI() {
    shutdown();
}

bool HiZPassRHI::initialize(const RenderConfig& config) {
    createHiZBuffer(config.renderWidth, config.renderHeight);
    return m_hiZTexture != nullptr;
}

void HiZPassRHI::shutdown() {
    destroyHiZBuffer();
}

void HiZPassRHI::resize(uint32_t width, uint32_t height) {
    if (width != m_width || height != m_height) {
        destroyHiZBuffer();
        createHiZBuffer(width, height);
    }
}

void HiZPassRHI::execute(RHI::RHICommandBuffer* cmd, RenderContext& context) {
    if (!m_enabled || !m_hiZTexture) return;

    // Hi-Z buffer is generated by downsampling the depth buffer through mip levels
    // Each mip level contains the maximum depth of the 2x2 region from previous level

    if (m_computePipeline && m_mipDescriptorSets.size() > 0) {
        cmd->bindComputePipeline(m_computePipeline);

        uint32_t currentWidth = m_width;
        uint32_t currentHeight = m_height;

        // Generate each mip level
        for (size_t mip = 1; mip < static_cast<size_t>(m_mipLevels) && mip <= m_mipDescriptorSets.size(); mip++) {
            // Bind descriptor set for this mip level transition
            cmd->bindDescriptorSet(0, m_mipDescriptorSets[mip - 1].get());

            // Calculate output dimensions for this mip level
            uint32_t outWidth = std::max(1u, currentWidth / 2);
            uint32_t outHeight = std::max(1u, currentHeight / 2);

            // Push constants with dimensions
            struct HiZPushConstants {
                uint32_t srcWidth;
                uint32_t srcHeight;
                uint32_t dstWidth;
                uint32_t dstHeight;
            } pushConstants = {currentWidth, currentHeight, outWidth, outHeight};

            cmd->pushConstants(RHI::ShaderStage::Compute, 0, sizeof(pushConstants), &pushConstants);

            // Dispatch compute shader
            uint32_t groupsX = (outWidth + 7) / 8;
            uint32_t groupsY = (outHeight + 7) / 8;
            cmd->dispatch(groupsX, groupsY, 1);

            currentWidth = outWidth;
            currentHeight = outHeight;
        }
    }

    // Store Hi-Z texture handle in context
    if (m_hiZTexture) {
        context.hiZTexture = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(m_hiZTexture->getNativeHandle()));
    }

    context.stats.hizTime = m_executionTimeMs;
}

void HiZPassRHI::createHiZBuffer(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;
    m_mipLevels = static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(std::max(width, height))))) + 1;

    RHI::TextureDesc texDesc{};
    texDesc.type = RHI::TextureType::Texture2D;
    texDesc.format = RHI::Format::R32_FLOAT;
    texDesc.width = width;
    texDesc.height = height;
    texDesc.depth = 1;
    texDesc.arrayLayers = 1;
    texDesc.mipLevels = m_mipLevels;
    texDesc.samples = 1;
    texDesc.usage = RHI::TextureUsage::Storage | RHI::TextureUsage::Sampled;
    texDesc.debugName = "HiZ_Buffer";

    m_hiZTexture = m_device->createTexture(texDesc);
    if (!m_hiZTexture) {
        std::cerr << "[HiZPassRHI] Failed to create Hi-Z texture" << std::endl;
        return;
    }

    std::cout << "[HiZPassRHI] Created Hi-Z buffer (" << width << "x" << height
              << ", " << m_mipLevels << " mips)" << std::endl;
}

void HiZPassRHI::destroyHiZBuffer() {
    m_hiZTexture.reset();
    m_width = 0;
    m_height = 0;
    m_mipLevels = 0;
}

// ============================================================================
// SSAOPassRHI Implementation
// ============================================================================

SSAOPassRHI::SSAOPassRHI(RHI::RHIDevice* device)
    : RenderPassRHI("SSAORHI", device) {}

SSAOPassRHI::~SSAOPassRHI() {
    shutdown();
}

bool SSAOPassRHI::initialize(const RenderConfig& config) {
    m_kernelSize = config.ssaoSamples;
    m_radius = config.ssaoRadius;
    m_bias = config.ssaoBias;
    createSSAOBuffers(config.renderWidth, config.renderHeight);
    return m_ssaoTexture != nullptr;
}

void SSAOPassRHI::shutdown() {
    destroySSAOBuffers();
}

void SSAOPassRHI::resize(uint32_t width, uint32_t height) {
    if (width != m_width || height != m_height) {
        destroySSAOBuffers();
        createSSAOBuffers(width, height);
    }
}

void SSAOPassRHI::execute(RHI::RHICommandBuffer* cmd, RenderContext& context) {
    if (!m_enabled || !m_ssaoTexture || !m_ssaoBlurred) return;

    // Update SSAO parameters buffer if needed
    if (m_kernelBuffer && context.camera) {
        struct SSAOParams {
            glm::mat4 projection;
            glm::mat4 view;
            glm::vec4 params;  // radius, bias, noiseScale.x, noiseScale.y
        } params;

        params.projection = context.camera->projection;
        params.view = context.camera->view;
        params.params = glm::vec4(
            m_radius,
            m_bias,
            static_cast<float>(m_width) / 4.0f,
            static_cast<float>(m_height) / 4.0f
        );

        void* mapped = m_kernelBuffer->map();
        if (mapped) {
            // Write params first, then kernel samples
            memcpy(mapped, &params, sizeof(params));
            memcpy(static_cast<char*>(mapped) + sizeof(params),
                   m_ssaoKernel.data(),
                   m_ssaoKernel.size() * sizeof(glm::vec4));
            m_kernelBuffer->unmap();
        }
    }

    // SSAO pass - compute ambient occlusion
    if (m_ssaoPipeline && m_ssaoDescriptorSet) {
        cmd->bindComputePipeline(m_ssaoPipeline);
        cmd->bindDescriptorSet(0, m_ssaoDescriptorSet.get());

        // Dispatch compute shader - one thread per pixel, 8x8 workgroups
        uint32_t groupsX = (m_width + 7) / 8;
        uint32_t groupsY = (m_height + 7) / 8;
        cmd->dispatch(groupsX, groupsY, 1);
    }

    // Memory barrier between SSAO and blur (handled by command buffer)

    // Blur pass - smooth the SSAO result
    if (m_blurPipeline && m_blurDescriptorSet) {
        cmd->bindComputePipeline(m_blurPipeline);
        cmd->bindDescriptorSet(0, m_blurDescriptorSet.get());

        uint32_t groupsX = (m_width + 7) / 8;
        uint32_t groupsY = (m_height + 7) / 8;
        cmd->dispatch(groupsX, groupsY, 1);
    }

    // Store result in context
    if (m_ssaoBlurred) {
        context.ssaoTexture = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(m_ssaoBlurred->getNativeHandle()));
    }

    context.stats.ssaoTime = m_executionTimeMs;
}

void SSAOPassRHI::createSSAOBuffers(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;

    RHI::TextureDesc texDesc{};
    texDesc.type = RHI::TextureType::Texture2D;
    texDesc.format = RHI::Format::R8_UNORM;
    texDesc.width = width;
    texDesc.height = height;
    texDesc.depth = 1;
    texDesc.arrayLayers = 1;
    texDesc.mipLevels = 1;
    texDesc.samples = 1;
    texDesc.usage = RHI::TextureUsage::Storage | RHI::TextureUsage::Sampled;

    texDesc.debugName = "SSAO_Output";
    m_ssaoTexture = m_device->createTexture(texDesc);

    texDesc.debugName = "SSAO_Blurred";
    m_ssaoBlurred = m_device->createTexture(texDesc);

    if (!m_ssaoTexture || !m_ssaoBlurred) {
        std::cerr << "[SSAOPassRHI] Failed to create SSAO textures" << std::endl;
        return;
    }

    // Create 4x4 noise texture
    RHI::TextureDesc noiseDesc{};
    noiseDesc.type = RHI::TextureType::Texture2D;
    noiseDesc.format = RHI::Format::RGBA16_FLOAT;
    noiseDesc.width = 4;
    noiseDesc.height = 4;
    noiseDesc.depth = 1;
    noiseDesc.arrayLayers = 1;
    noiseDesc.mipLevels = 1;
    noiseDesc.samples = 1;
    noiseDesc.usage = RHI::TextureUsage::Sampled;
    noiseDesc.debugName = "SSAO_Noise";
    m_noiseTexture = m_device->createTexture(noiseDesc);

    // Create kernel buffer (params + kernel samples)
    size_t paramsSize = sizeof(glm::mat4) * 2 + sizeof(glm::vec4);  // projection, view, params
    size_t kernelDataSize = m_kernelSize * sizeof(glm::vec4);
    RHI::BufferDesc kernelDesc{};
    kernelDesc.size = paramsSize + kernelDataSize;
    kernelDesc.usage = RHI::BufferUsage::Uniform;
    kernelDesc.memory = RHI::MemoryUsage::CpuToGpu;
    kernelDesc.debugName = "SSAO_KernelBuffer";
    m_kernelBuffer = m_device->createBuffer(kernelDesc);

    generateKernelAndNoise();

    std::cout << "[SSAOPassRHI] Created SSAO buffer (" << width << "x" << height
              << ", " << m_kernelSize << " samples)" << std::endl;
}

void SSAOPassRHI::destroySSAOBuffers() {
    m_ssaoTexture.reset();
    m_ssaoBlurred.reset();
    m_noiseTexture.reset();
    m_kernelBuffer.reset();
    m_ssaoDescriptorSet.reset();
    m_blurDescriptorSet.reset();
    m_width = 0;
    m_height = 0;
}

void SSAOPassRHI::generateKernelAndNoise() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);

    m_ssaoKernel.clear();
    for (uint32_t i = 0; i < m_kernelSize; i++) {
        glm::vec3 sample(
            dis(gen) * 2.0f - 1.0f,
            dis(gen) * 2.0f - 1.0f,
            dis(gen)
        );
        sample = glm::normalize(sample);
        sample *= dis(gen);

        float scale = static_cast<float>(i) / static_cast<float>(m_kernelSize);
        scale = 0.1f + scale * scale * 0.9f;
        sample *= scale;

        m_ssaoKernel.push_back(glm::vec4(sample, 0.0f));
    }
}

void SSAOPassRHI::setGBufferTextures(RHI::RHITexture* position, RHI::RHITexture* normal, RHI::RHITexture* depth) {
    m_positionTexture = position;
    m_normalTexture = normal;
    m_depthTexture = depth;
}

// ============================================================================
// GPUCullingPassRHI Implementation
// ============================================================================

GPUCullingPassRHI::GPUCullingPassRHI(RHI::RHIDevice* device)
    : RenderPassRHI("GPUCullingRHI", device) {}

GPUCullingPassRHI::~GPUCullingPassRHI() {
    shutdown();
}

bool GPUCullingPassRHI::initialize(const RenderConfig& /*config*/) {
    RHI::BufferDesc bufDesc{};
    bufDesc.size = sizeof(uint32_t) * 5 * m_maxChunks;
    bufDesc.usage = RHI::BufferUsage::Storage | RHI::BufferUsage::Indirect;
    bufDesc.memory = RHI::MemoryUsage::GpuOnly;
    bufDesc.debugName = "IndirectBuffer";

    m_indirectBuffer = m_device->createBuffer(bufDesc);

    bufDesc.size = sizeof(uint32_t);
    bufDesc.debugName = "CounterBuffer";
    m_counterBuffer = m_device->createBuffer(bufDesc);

    if (!m_indirectBuffer || !m_counterBuffer) {
        std::cerr << "[GPUCullingPassRHI] Failed to create buffers" << std::endl;
        return false;
    }

    std::cout << "[GPUCullingPassRHI] Created GPU culling buffers" << std::endl;
    return true;
}

void GPUCullingPassRHI::shutdown() {
    m_indirectBuffer.reset();
    m_counterBuffer.reset();
    m_cullingUBO.reset();
    m_descriptorSet.reset();
}

void GPUCullingPassRHI::resize(uint32_t /*width*/, uint32_t /*height*/) {
}

void GPUCullingPassRHI::execute(RHI::RHICommandBuffer* cmd, RenderContext& context) {
    if (!m_enabled || m_chunkCount == 0 || !m_chunkAABBBuffer) return;

    // Reset atomic counter to 0
    if (m_counterBuffer) {
        uint32_t zero = 0;
        void* mapped = m_counterBuffer->map();
        if (mapped) {
            memcpy(mapped, &zero, sizeof(zero));
            m_counterBuffer->unmap();
        }
    }

    // Update culling uniform buffer with frustum planes
    if (m_cullingUBO && context.camera) {
        struct CullingUniforms {
            glm::mat4 viewProj;
            glm::vec4 frustumPlanes[6];  // Frustum planes in world space
            glm::vec4 cameraPos;
            glm::uvec4 params;           // chunkCount, hiZWidth, hiZHeight, hiZMipLevels
        } uniforms;

        uniforms.viewProj = context.camera->viewProjection;
        uniforms.cameraPos = glm::vec4(context.camera->position, 1.0f);
        uniforms.params = glm::uvec4(m_chunkCount, 0, 0, 0);

        // Extract frustum planes from view-projection matrix
        glm::mat4 vp = context.camera->viewProjection;
        // Left plane
        uniforms.frustumPlanes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0],
                                               vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]);
        // Right plane
        uniforms.frustumPlanes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0],
                                               vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]);
        // Bottom plane
        uniforms.frustumPlanes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1],
                                               vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]);
        // Top plane
        uniforms.frustumPlanes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1],
                                               vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]);
        // Near plane
        uniforms.frustumPlanes[4] = glm::vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2],
                                               vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]);
        // Far plane
        uniforms.frustumPlanes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2],
                                               vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]);

        // Normalize frustum planes
        for (int i = 0; i < 6; i++) {
            float len = glm::length(glm::vec3(uniforms.frustumPlanes[i]));
            if (len > 0.0001f) {
                uniforms.frustumPlanes[i] /= len;
            }
        }

        void* mapped = m_cullingUBO->map();
        if (mapped) {
            memcpy(mapped, &uniforms, sizeof(uniforms));
            m_cullingUBO->unmap();
        }
    }

    // Dispatch GPU culling compute shader
    if (m_computePipeline && m_descriptorSet) {
        cmd->bindComputePipeline(m_computePipeline);
        cmd->bindDescriptorSet(0, m_descriptorSet.get());

        // One thread per chunk, 64 chunks per workgroup
        uint32_t groupsX = (m_chunkCount + 63) / 64;
        cmd->dispatch(groupsX, 1, 1);
    }

    // Visible count will be read back after GPU execution
    context.stats.chunksCulled = m_chunkCount - m_visibleCount;
}

void GPUCullingPassRHI::setChunkData(RHI::RHIBuffer* chunkAABBs, uint32_t chunkCount) {
    m_chunkAABBBuffer = chunkAABBs;
    m_chunkCount = chunkCount;
}

// ============================================================================
// CompositePassRHI Implementation
// ============================================================================

CompositePassRHI::CompositePassRHI(RHI::RHIDevice* device)
    : RenderPassRHI("CompositeRHI", device) {}

CompositePassRHI::~CompositePassRHI() {
    shutdown();
}

bool CompositePassRHI::initialize(const RenderConfig& config) {
    createSceneBuffer(config.renderWidth, config.renderHeight);
    return m_sceneColor != nullptr;
}

void CompositePassRHI::shutdown() {
    destroySceneBuffer();
}

void CompositePassRHI::resize(uint32_t width, uint32_t height) {
    if (width != m_width || height != m_height) {
        destroySceneBuffer();
        createSceneBuffer(width, height);
    }
}

void CompositePassRHI::execute(RHI::RHICommandBuffer* cmd, RenderContext& context) {
    if (!m_enabled || !m_framebuffer || !m_renderPass) return;

    // Begin render pass
    std::vector<RHI::ClearValue> clearValues;
    clearValues.push_back(RHI::ClearValue::Color(0.0f, 0.0f, 0.0f, 1.0f));
    clearValues.push_back(RHI::ClearValue::DepthStencil(1.0f, 0));

    cmd->beginRenderPass(m_renderPass.get(), m_framebuffer.get(), clearValues);

    // Set viewport
    RHI::Viewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_width);
    viewport.height = static_cast<float>(m_height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    cmd->setViewport(viewport);

    // Set scissor
    RHI::Scissor scissor{};
    scissor.x = 0;
    scissor.y = 0;
    scissor.width = m_width;
    scissor.height = m_height;
    cmd->setScissor(scissor);

    // Bind pipeline
    if (m_pipeline) {
        cmd->bindGraphicsPipeline(m_pipeline);
    }

    // Update lighting uniform buffer
    if (m_lightingUBO && context.lighting && context.camera && context.fog) {
        struct LightingUniforms {
            glm::mat4 invViewProj;
            glm::vec4 lightDir;
            glm::vec4 lightColor;
            glm::vec4 ambientColor;
            glm::vec4 skyColor;
            glm::vec4 cameraPos;
            glm::vec4 fogParams;      // density, isUnderwater, renderDistance, time
            glm::vec4 cascadeSplits;
            glm::ivec4 flags;         // enableSSAO, debugMode, 0, 0
        } uniforms;

        glm::mat4 invViewProj = glm::inverse(context.camera->viewProjection);
        uniforms.invViewProj = invViewProj;
        uniforms.lightDir = glm::vec4(context.lighting->lightDir, 0.0f);
        uniforms.lightColor = glm::vec4(context.lighting->lightColor, context.lighting->shadowStrength);
        uniforms.ambientColor = glm::vec4(context.lighting->ambientColor, 0.0f);
        uniforms.skyColor = glm::vec4(context.lighting->skyColor, 0.0f);
        uniforms.cameraPos = glm::vec4(context.camera->position, 1.0f);
        uniforms.fogParams = glm::vec4(
            context.fog->density,
            context.fog->isUnderwater ? 1.0f : 0.0f,
            context.fog->renderDistance,
            context.lighting->time
        );
        uniforms.cascadeSplits = glm::vec4(
            context.cascadeSplits[0],
            context.cascadeSplits[1],
            context.cascadeSplits[2],
            0.0f
        );
        uniforms.flags = glm::ivec4(
            context.config->enableSSAO ? 1 : 0,
            context.config->debugMode,
            0, 0
        );

        void* mapped = m_lightingUBO->map();
        if (mapped) {
            memcpy(mapped, &uniforms, sizeof(uniforms));
            m_lightingUBO->unmap();
        }
    }

    // Bind descriptor set with G-buffer textures, SSAO, shadows
    if (m_descriptorSet) {
        cmd->bindDescriptorSet(0, m_descriptorSet.get());
    }

    // Draw fullscreen quad (6 vertices for 2 triangles)
    if (m_quadVertexBuffer) {
        cmd->bindVertexBuffer(0, m_quadVertexBuffer.get());
        cmd->draw(6, 1, 0, 0);
    }

    cmd->endRenderPass();

    // Store output texture handles in context
    if (m_sceneColor) {
        context.sceneColor = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(m_sceneColor->getNativeHandle()));
    }
    if (m_sceneDepth) {
        context.sceneDepth = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(m_sceneDepth->getNativeHandle()));
    }

    context.stats.compositeTime = m_executionTimeMs;
}

void CompositePassRHI::createSceneBuffer(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;

    RHI::TextureDesc texDesc{};
    texDesc.type = RHI::TextureType::Texture2D;
    texDesc.format = RHI::Format::RGBA16_FLOAT;
    texDesc.width = width;
    texDesc.height = height;
    texDesc.depth = 1;
    texDesc.arrayLayers = 1;
    texDesc.mipLevels = 1;
    texDesc.samples = 1;
    texDesc.usage = RHI::TextureUsage::RenderTarget | RHI::TextureUsage::Sampled;
    texDesc.debugName = "SceneColor";

    m_sceneColor = m_device->createTexture(texDesc);

    texDesc.format = RHI::Format::D32_FLOAT;
    texDesc.usage = RHI::TextureUsage::DepthStencil | RHI::TextureUsage::Sampled;
    texDesc.debugName = "SceneDepth";

    m_sceneDepth = m_device->createTexture(texDesc);

    if (!m_sceneColor || !m_sceneDepth) {
        std::cerr << "[CompositePassRHI] Failed to create scene textures" << std::endl;
        return;
    }

    RHI::RenderPassDesc rpDesc{};
    RHI::AttachmentDesc colorAttach{};
    colorAttach.format = RHI::Format::RGBA16_FLOAT;
    colorAttach.loadOp = RHI::LoadOp::Clear;
    colorAttach.storeOp = RHI::StoreOp::Store;
    rpDesc.colorAttachments.push_back(colorAttach);

    rpDesc.depthStencilAttachment.format = RHI::Format::D32_FLOAT;
    rpDesc.depthStencilAttachment.loadOp = RHI::LoadOp::Clear;
    rpDesc.depthStencilAttachment.storeOp = RHI::StoreOp::DontCare;
    rpDesc.hasDepthStencil = true;

    m_renderPass = m_device->createRenderPass(rpDesc);

    RHI::FramebufferDesc fbDesc{};
    fbDesc.renderPass = m_renderPass.get();
    fbDesc.width = width;
    fbDesc.height = height;
    fbDesc.colorAttachments = {{m_sceneColor.get()}};
    fbDesc.depthStencilAttachment.texture = m_sceneDepth.get();

    m_framebuffer = m_device->createFramebuffer(fbDesc);

    // Create lighting uniform buffer
    if (!m_lightingUBO) {
        RHI::BufferDesc uboDesc{};
        uboDesc.size = 256;  // Large enough for LightingUniforms
        uboDesc.usage = RHI::BufferUsage::Uniform;
        uboDesc.memory = RHI::MemoryUsage::CpuToGpu;
        uboDesc.debugName = "Composite_LightingUBO";
        m_lightingUBO = m_device->createBuffer(uboDesc);
    }

    // Create fullscreen quad vertex buffer
    if (!m_quadVertexBuffer) {
        float quadVertices[] = {
            // Position (x, y)
            -1.0f,  1.0f,
            -1.0f, -1.0f,
             1.0f, -1.0f,
            -1.0f,  1.0f,
             1.0f, -1.0f,
             1.0f,  1.0f
        };

        RHI::BufferDesc vbDesc{};
        vbDesc.size = sizeof(quadVertices);
        vbDesc.usage = RHI::BufferUsage::Vertex;
        vbDesc.memory = RHI::MemoryUsage::CpuToGpu;
        vbDesc.debugName = "Composite_QuadVB";
        m_quadVertexBuffer = m_device->createBuffer(vbDesc);

        if (m_quadVertexBuffer) {
            void* mapped = m_quadVertexBuffer->map();
            if (mapped) {
                memcpy(mapped, quadVertices, sizeof(quadVertices));
                m_quadVertexBuffer->unmap();
            }
        }
    }

    std::cout << "[CompositePassRHI] Created scene buffer (" << width << "x" << height << ")" << std::endl;
}

void CompositePassRHI::destroySceneBuffer() {
    m_framebuffer.reset();
    m_renderPass.reset();
    m_sceneColor.reset();
    m_sceneDepth.reset();
    m_lightingUBO.reset();
    m_quadVertexBuffer.reset();
    m_descriptorSet.reset();
    m_width = 0;
    m_height = 0;
}

void CompositePassRHI::setGBufferTextures(RHI::RHITexture* position, RHI::RHITexture* normal,
                                          RHI::RHITexture* albedo, RHI::RHITexture* depth) {
    m_gPosition = position;
    m_gNormal = normal;
    m_gAlbedo = albedo;
    m_gDepth = depth;
}

// ============================================================================
// SkyPassRHI Implementation
// ============================================================================

SkyPassRHI::SkyPassRHI(RHI::RHIDevice* device)
    : RenderPassRHI("SkyRHI", device) {}

SkyPassRHI::~SkyPassRHI() {
    shutdown();
}

bool SkyPassRHI::initialize(const RenderConfig& /*config*/) {
    // Create sky UBO
    RHI::BufferDesc uboDesc{};
    uboDesc.size = sizeof(glm::mat4) * 2 + sizeof(glm::vec4) * 5;  // 2 matrices + 5 vec4s
    uboDesc.usage = RHI::BufferUsage::Uniform;
    uboDesc.memory = RHI::MemoryUsage::CpuToGpu;
    uboDesc.debugName = "SkyUBO";
    m_skyUBO = m_device->createBuffer(uboDesc);

    if (!m_skyUBO) {
        std::cerr << "[SkyPassRHI] Failed to create sky UBO" << std::endl;
        return false;
    }

    // Create fullscreen quad vertex buffer
    float skyVertices[] = {
        -1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f
    };

    RHI::BufferDesc vbDesc{};
    vbDesc.size = sizeof(skyVertices);
    vbDesc.usage = RHI::BufferUsage::Vertex;
    vbDesc.memory = RHI::MemoryUsage::CpuToGpu;
    vbDesc.debugName = "Sky_QuadVB";
    m_skyVertexBuffer = m_device->createBuffer(vbDesc);

    if (m_skyVertexBuffer) {
        void* mapped = m_skyVertexBuffer->map();
        if (mapped) {
            memcpy(mapped, skyVertices, sizeof(skyVertices));
            m_skyVertexBuffer->unmap();
        }
    }

    std::cout << "[SkyPassRHI] Initialized" << std::endl;
    return true;
}

void SkyPassRHI::shutdown() {
    m_skyUBO.reset();
    m_skyVertexBuffer.reset();
    m_descriptorSet.reset();
}

void SkyPassRHI::resize(uint32_t /*width*/, uint32_t /*height*/) {
}

void SkyPassRHI::execute(RHI::RHICommandBuffer* cmd, RenderContext& context) {
    if (!m_enabled) return;

    // Update sky uniform buffer
    if (m_skyUBO && context.camera && context.lighting) {
        struct SkyUniforms {
            glm::mat4 invView;
            glm::mat4 invProjection;
            glm::vec4 cameraPos;
            glm::vec4 sunDir;
            glm::vec4 skyTop;
            glm::vec4 skyBottom;
            glm::vec4 params;  // time, 0, 0, 0
        } uniforms;

        uniforms.invView = context.camera->invView;
        uniforms.invProjection = context.camera->invProjection;
        uniforms.cameraPos = glm::vec4(context.camera->position, 1.0f);
        uniforms.sunDir = glm::vec4(context.lighting->lightDir, 0.0f);
        uniforms.skyTop = glm::vec4(context.lighting->skyColor, 1.0f);
        glm::vec3 skyBottom = glm::mix(context.lighting->skyColor,
                                        glm::vec3(0.9f, 0.85f, 0.8f), 0.3f);
        uniforms.skyBottom = glm::vec4(skyBottom, 1.0f);
        uniforms.params = glm::vec4(context.time, 0.0f, 0.0f, 0.0f);

        void* mapped = m_skyUBO->map();
        if (mapped) {
            memcpy(mapped, &uniforms, sizeof(uniforms));
            m_skyUBO->unmap();
        }
    }

    // Bind sky pipeline (should have depth write disabled, LEQUAL compare)
    if (m_pipeline) {
        cmd->bindGraphicsPipeline(m_pipeline);
    }

    // Bind descriptor set
    if (m_descriptorSet) {
        cmd->bindDescriptorSet(0, m_descriptorSet.get());
    }

    // Draw fullscreen quad
    if (m_skyVertexBuffer) {
        cmd->bindVertexBuffer(0, m_skyVertexBuffer.get());
        cmd->draw(6, 1, 0, 0);
    }

    context.stats.skyTime = m_executionTimeMs;
}

// ============================================================================
// WaterPassRHI Implementation
// ============================================================================

WaterPassRHI::WaterPassRHI(RHI::RHIDevice* device)
    : RenderPassRHI("WaterRHI", device) {}

WaterPassRHI::~WaterPassRHI() {
    shutdown();
}

bool WaterPassRHI::initialize(const RenderConfig& config) {
    m_width = config.renderWidth;
    m_height = config.renderHeight;

    // Create water uniform buffer
    RHI::BufferDesc uboDesc{};
    uboDesc.size = 256;  // Enough for water uniforms
    uboDesc.usage = RHI::BufferUsage::Uniform;
    uboDesc.memory = RHI::MemoryUsage::CpuToGpu;
    uboDesc.debugName = "Water_UBO";
    m_waterUBO = m_device->createBuffer(uboDesc);

    std::cout << "[WaterPassRHI] Initialized" << std::endl;
    return true;
}

void WaterPassRHI::shutdown() {
    m_waterUBO.reset();
    m_descriptorSet.reset();
}

void WaterPassRHI::resize(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;
}

void WaterPassRHI::execute(RHI::RHICommandBuffer* cmd, RenderContext& context) {
    if (!m_enabled || !m_pipeline || !m_worldRenderer) return;

    // Update water uniforms
    if (m_waterUBO && context.camera && context.lighting) {
        struct WaterUniforms {
            glm::mat4 view;
            glm::mat4 projection;
            glm::vec4 lightDir;
            glm::vec4 lightColor;
            glm::vec4 ambientColor;
            glm::vec4 skyColor;
            glm::vec4 cameraPos;
            glm::vec4 waterParams;  // time, fogDensity, isUnderwater, lodDistance
            glm::vec4 waterTexBounds;  // u0, v0, u1, v1
            glm::vec4 animParams;  // waterAnimationEnabled, 0, 0, 0
        } uniforms;

        uniforms.view = context.camera->view;
        uniforms.projection = context.camera->projection;
        uniforms.lightDir = glm::vec4(context.lighting->lightDir, 0.0f);
        uniforms.lightColor = glm::vec4(context.lighting->lightColor, 1.0f);
        uniforms.ambientColor = glm::vec4(context.lighting->ambientColor, 1.0f);
        uniforms.skyColor = glm::vec4(context.lighting->skyColor, 1.0f);
        uniforms.cameraPos = glm::vec4(context.camera->position, 1.0f);
        uniforms.waterParams = glm::vec4(
            context.time,
            context.fog ? context.fog->density : 0.02f,
            0.0f,  // isUnderwater - would need player state
            100.0f  // lodDistance - default value
        );
        uniforms.waterTexBounds = glm::vec4(11.0f/16.0f, 0.0f, 12.0f/16.0f, 1.0f/16.0f);  // Water texture slot 11
        uniforms.animParams = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);  // Animation enabled

        void* mapped = m_waterUBO->map();
        if (mapped) {
            memcpy(mapped, &uniforms, sizeof(uniforms));
            m_waterUBO->unmap();
        }
    }

    // Bind water pipeline
    cmd->bindGraphicsPipeline(m_pipeline);

    // Bind descriptor set if available
    if (m_descriptorSet) {
        cmd->bindDescriptorSet(0, m_descriptorSet.get());
    }

    // In hybrid mode, render water using OpenGL calls
    if (context.world && m_worldRenderer) {
        WorldRenderParams waterParams{};
        waterParams.cameraPosition = context.camera->position;
        waterParams.viewProjection = context.camera->viewProjection;
        waterParams.renderWater = true;

        m_worldRenderer->renderWater(cmd, *context.world, waterParams);
    }

    context.stats.waterTime = m_executionTimeMs;
}

// ============================================================================
// PrecipitationPassRHI Implementation
// ============================================================================

PrecipitationPassRHI::PrecipitationPassRHI(RHI::RHIDevice* device)
    : RenderPassRHI("PrecipitationRHI", device) {}

PrecipitationPassRHI::~PrecipitationPassRHI() {
    shutdown();
}

bool PrecipitationPassRHI::initialize(const RenderConfig& config) {
    m_width = config.renderWidth;
    m_height = config.renderHeight;

    // Reserve space for particles
    m_particles.reserve(m_maxParticles);

    createParticleBuffers();

    std::cout << "[PrecipitationPassRHI] Initialized with max " << m_maxParticles << " particles" << std::endl;
    return true;
}

void PrecipitationPassRHI::shutdown() {
    destroyParticleBuffers();
    m_particles.clear();
}

void PrecipitationPassRHI::resize(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;
}

void PrecipitationPassRHI::createParticleBuffers() {
    // Particle vertex buffer: position (vec3) + size (float) + alpha (float) = 20 bytes per particle
    RHI::BufferDesc vboDesc{};
    vboDesc.size = m_maxParticles * 20;
    vboDesc.usage = RHI::BufferUsage::Vertex;
    vboDesc.memory = RHI::MemoryUsage::CpuToGpu;  // Frequently updated
    vboDesc.debugName = "Precipitation_VBO";
    m_particleBuffer = m_device->createBuffer(vboDesc);

    // Precipitation uniform buffer
    RHI::BufferDesc uboDesc{};
    uboDesc.size = 128;  // view, projection, time, weatherType, intensity, lightColor
    uboDesc.usage = RHI::BufferUsage::Uniform;
    uboDesc.memory = RHI::MemoryUsage::CpuToGpu;
    uboDesc.debugName = "Precipitation_UBO";
    m_precipUBO = m_device->createBuffer(uboDesc);
}

void PrecipitationPassRHI::destroyParticleBuffers() {
    m_particleBuffer.reset();
    m_precipUBO.reset();
    m_descriptorSet.reset();
}

void PrecipitationPassRHI::spawnParticle(const glm::vec3& cameraPos) {
    if (m_particles.size() >= m_maxParticles) return;

    Particle p;

    // Random position within spawn radius around camera
    float radius = 40.0f;
    float angle = static_cast<float>(rand()) / RAND_MAX * 6.28318f;
    float distance = static_cast<float>(rand()) / RAND_MAX * radius;
    p.position.x = cameraPos.x + cos(angle) * distance;
    p.position.z = cameraPos.z + sin(angle) * distance;
    p.position.y = cameraPos.y + 30.0f + static_cast<float>(rand()) / RAND_MAX * 20.0f;

    // Set velocity based on weather type
    if (m_weatherType == 2) {
        // Snow - slow, gentle fall
        p.velocity = glm::vec3(0.0f, -2.0f - static_cast<float>(rand()) / RAND_MAX * 1.0f, 0.0f);
        p.size = 4.0f + static_cast<float>(rand()) / RAND_MAX * 3.0f;
        p.lifetime = 15.0f + static_cast<float>(rand()) / RAND_MAX * 5.0f;
    } else {
        // Rain - fast fall
        p.velocity = glm::vec3(0.0f, -15.0f - static_cast<float>(rand()) / RAND_MAX * 5.0f, 0.0f);
        p.size = 2.0f + static_cast<float>(rand()) / RAND_MAX * 2.0f;
        p.lifetime = 4.0f + static_cast<float>(rand()) / RAND_MAX * 2.0f;
    }

    p.alpha = 0.7f + static_cast<float>(rand()) / RAND_MAX * 0.3f;

    m_particles.push_back(p);
}

void PrecipitationPassRHI::updateParticles(float deltaTime, const glm::vec3& cameraPos) {
    // Spawn new particles based on intensity
    if (m_weatherType > 0 && m_intensity > 0.0f) {
        int spawnCount = static_cast<int>(m_intensity * 50.0f * deltaTime);
        for (int i = 0; i < spawnCount && m_particles.size() < m_maxParticles; i++) {
            spawnParticle(cameraPos);
        }
    }

    // Update existing particles
    for (auto it = m_particles.begin(); it != m_particles.end();) {
        it->position += it->velocity * deltaTime;
        it->lifetime -= deltaTime;

        // Check for despawn conditions
        if (it->lifetime <= 0.0f ||
            it->position.y < cameraPos.y - 50.0f ||
            glm::distance(glm::vec2(it->position.x, it->position.z),
                         glm::vec2(cameraPos.x, cameraPos.z)) > 60.0f) {
            it = m_particles.erase(it);
        } else {
            ++it;
        }
    }

    m_activeParticles = static_cast<uint32_t>(m_particles.size());
}

void PrecipitationPassRHI::execute(RHI::RHICommandBuffer* cmd, RenderContext& context) {
    if (!m_enabled || m_weatherType == 0 || m_intensity <= 0.0f) return;
    if (!m_pipeline || !m_particleBuffer) return;

    // Update particles
    float deltaTime = context.deltaTime > 0.0f ? context.deltaTime : 0.016f;
    updateParticles(deltaTime, context.camera->position);

    if (m_particles.empty()) return;

    // Upload particle vertex data
    struct ParticleVertex {
        glm::vec3 position;
        float size;
        float alpha;
    };

    std::vector<ParticleVertex> vertices(m_particles.size());
    for (size_t i = 0; i < m_particles.size(); i++) {
        vertices[i].position = m_particles[i].position;
        vertices[i].size = m_particles[i].size;
        vertices[i].alpha = m_particles[i].alpha;
    }

    void* mapped = m_particleBuffer->map();
    if (mapped) {
        memcpy(mapped, vertices.data(), vertices.size() * sizeof(ParticleVertex));
        m_particleBuffer->unmap();
    }

    // Update uniforms
    if (m_precipUBO && context.camera) {
        struct PrecipUniforms {
            glm::mat4 view;
            glm::mat4 projection;
            float time;
            int weatherType;
            float intensity;
            float padding1;
            glm::vec3 lightColor;
            float padding2;
        } uniforms;

        uniforms.view = context.camera->view;
        uniforms.projection = context.camera->projection;
        uniforms.time = context.time;
        uniforms.weatherType = m_weatherType;
        uniforms.intensity = m_intensity;
        uniforms.lightColor = m_lightColor;

        void* uboMapped = m_precipUBO->map();
        if (uboMapped) {
            memcpy(uboMapped, &uniforms, sizeof(uniforms));
            m_precipUBO->unmap();
        }
    }

    // Bind pipeline and draw
    cmd->bindGraphicsPipeline(m_pipeline);

    if (m_descriptorSet) {
        cmd->bindDescriptorSet(0, m_descriptorSet.get());
    }

    // Draw particles as points
    cmd->bindVertexBuffer(0, m_particleBuffer.get());
    cmd->draw(static_cast<uint32_t>(m_particles.size()), 1, 0, 0);
}

// ============================================================================
// FSRPassRHI Implementation
// ============================================================================

FSRPassRHI::FSRPassRHI(RHI::RHIDevice* device)
    : RenderPassRHI("FSRRHI", device) {}

FSRPassRHI::~FSRPassRHI() {
    shutdown();
}

bool FSRPassRHI::initialize(const RenderConfig& config) {
    m_renderWidth = config.renderWidth;
    m_renderHeight = config.renderHeight;
    m_displayWidth = config.displayWidth;
    m_displayHeight = config.displayHeight;

    createBuffers();
    return m_outputTexture != nullptr;
}

void FSRPassRHI::shutdown() {
    destroyBuffers();
}

void FSRPassRHI::resize(uint32_t width, uint32_t height) {
    if (width != m_displayWidth || height != m_displayHeight) {
        m_displayWidth = width;
        m_displayHeight = height;
        destroyBuffers();
        createBuffers();
    }
}

void FSRPassRHI::execute(RHI::RHICommandBuffer* cmd, RenderContext& context) {
    (void)context;  // FSR doesn't need render context data
    if (!m_enabled || !m_outputTexture) return;

    // Update FSR constants buffer
    if (m_fsrConstantsBuffer) {
        struct FSRConstants {
            glm::uvec4 const0;  // renderWidth, renderHeight, 1.0/renderWidth, 1.0/renderHeight (packed as uint)
            glm::uvec4 const1;  // displayWidth, displayHeight, 1.0/displayWidth, 1.0/displayHeight (packed as uint)
            glm::uvec4 const2;  // EASU specific constants
            glm::uvec4 const3;  // RCAS specific constants
        } constants;

        // Pack float as uint for shader
        auto packFloat = [](float f) -> uint32_t {
            uint32_t result;
            memcpy(&result, &f, sizeof(float));
            return result;
        };

        constants.const0 = glm::uvec4(
            m_renderWidth, m_renderHeight,
            packFloat(1.0f / static_cast<float>(m_renderWidth)),
            packFloat(1.0f / static_cast<float>(m_renderHeight))
        );
        constants.const1 = glm::uvec4(
            m_displayWidth, m_displayHeight,
            packFloat(1.0f / static_cast<float>(m_displayWidth)),
            packFloat(1.0f / static_cast<float>(m_displayHeight))
        );
        constants.const2 = glm::uvec4(0);  // EASU constants (calculated in shader)
        constants.const3 = glm::uvec4(packFloat(0.25f), 0, 0, 0);  // RCAS sharpness

        void* mapped = m_fsrConstantsBuffer->map();
        if (mapped) {
            memcpy(mapped, &constants, sizeof(constants));
            m_fsrConstantsBuffer->unmap();
        }
    }

    // EASU pass - Edge Adaptive Spatial Upsampling
    if (m_easuPipeline && m_easuDescriptorSet) {
        cmd->bindComputePipeline(m_easuPipeline);
        cmd->bindDescriptorSet(0, m_easuDescriptorSet.get());

        // Dispatch EASU - one thread per output pixel, 16x16 workgroups
        uint32_t groupsX = (m_displayWidth + 15) / 16;
        uint32_t groupsY = (m_displayHeight + 15) / 16;
        cmd->dispatch(groupsX, groupsY, 1);
    }

    // Memory barrier between EASU and RCAS

    // RCAS pass - Robust Contrast Adaptive Sharpening
    if (m_rcasPipeline && m_rcasDescriptorSet) {
        cmd->bindComputePipeline(m_rcasPipeline);
        cmd->bindDescriptorSet(0, m_rcasDescriptorSet.get());

        uint32_t groupsX = (m_displayWidth + 15) / 16;
        uint32_t groupsY = (m_displayHeight + 15) / 16;
        cmd->dispatch(groupsX, groupsY, 1);
    }

    // FSR output texture is available via getOutputTexture()
    // The calling code will use the output directly
}

void FSRPassRHI::setDimensions(uint32_t renderWidth, uint32_t renderHeight,
                               uint32_t displayWidth, uint32_t displayHeight) {
    m_renderWidth = renderWidth;
    m_renderHeight = renderHeight;
    m_displayWidth = displayWidth;
    m_displayHeight = displayHeight;
}

void FSRPassRHI::createBuffers() {
    RHI::TextureDesc texDesc{};
    texDesc.type = RHI::TextureType::Texture2D;
    texDesc.format = RHI::Format::RGBA16_FLOAT;
    texDesc.width = m_displayWidth;
    texDesc.height = m_displayHeight;
    texDesc.depth = 1;
    texDesc.arrayLayers = 1;
    texDesc.mipLevels = 1;
    texDesc.samples = 1;
    texDesc.usage = RHI::TextureUsage::Storage | RHI::TextureUsage::Sampled;
    texDesc.debugName = "FSR_Output";

    m_outputTexture = m_device->createTexture(texDesc);

    texDesc.debugName = "FSR_Intermediate";
    m_intermediateTexture = m_device->createTexture(texDesc);

    RHI::BufferDesc uboDesc{};
    uboDesc.size = sizeof(glm::vec4) * 4;
    uboDesc.usage = RHI::BufferUsage::Uniform;
    uboDesc.memory = RHI::MemoryUsage::CpuToGpu;
    uboDesc.debugName = "FSR_Constants";
    m_fsrConstantsBuffer = m_device->createBuffer(uboDesc);

    if (!m_outputTexture) {
        std::cerr << "[FSRPassRHI] Failed to create output texture" << std::endl;
        return;
    }

    std::cout << "[FSRPassRHI] Initialized FSR upscaling ("
              << m_renderWidth << "x" << m_renderHeight << " -> "
              << m_displayWidth << "x" << m_displayHeight << ")" << std::endl;
}

void FSRPassRHI::destroyBuffers() {
    m_outputTexture.reset();
    m_intermediateTexture.reset();
    m_fsrConstantsBuffer.reset();
    m_easuDescriptorSet.reset();
    m_rcasDescriptorSet.reset();
}

// ============================================================================
// BloomPassRHI Implementation
// ============================================================================

BloomPassRHI::BloomPassRHI(RHI::RHIDevice* device)
    : RenderPassRHI("BloomRHI", device) {}

BloomPassRHI::~BloomPassRHI() {
    shutdown();
}

bool BloomPassRHI::initialize(const RenderConfig& config) {
    m_width = config.renderWidth;
    m_height = config.renderHeight;

    // Create render pass for bloom output
    RHI::RenderPassDesc rpDesc{};
    RHI::AttachmentDesc colorAttachment{};
    colorAttachment.format = RHI::Format::RGBA16_FLOAT;
    colorAttachment.loadOp = RHI::LoadOp::Clear;
    colorAttachment.storeOp = RHI::StoreOp::Store;
    rpDesc.colorAttachments.push_back(colorAttachment);
    rpDesc.hasDepthStencil = false;
    m_renderPass = m_device->createRenderPass(rpDesc);

    // Create uniform buffer for bloom parameters
    RHI::BufferDesc uboDesc{};
    uboDesc.size = 64;  // threshold, softThreshold, intensity, texelSize, etc.
    uboDesc.usage = RHI::BufferUsage::Uniform;
    uboDesc.memory = RHI::MemoryUsage::CpuToGpu;
    uboDesc.debugName = "Bloom_UBO";
    m_bloomUBO = m_device->createBuffer(uboDesc);

    // Create mip chain for blur
    createMipChain(m_width, m_height);

    std::cout << "[BloomPassRHI] Initialized with " << m_mipLevels
              << " mip levels (" << m_width << "x" << m_height << ")" << std::endl;

    return true;
}

void BloomPassRHI::shutdown() {
    destroyMipChain();
    m_bloomUBO.reset();
    m_renderPass.reset();
    m_extractDescriptorSet.reset();
    m_combineDescriptorSet.reset();
}

void BloomPassRHI::resize(uint32_t width, uint32_t height) {
    if (width == m_width && height == m_height) return;

    m_width = width;
    m_height = height;

    destroyMipChain();
    createMipChain(width, height);
}

void BloomPassRHI::createMipChain(uint32_t width, uint32_t height) {
    m_mipChain.clear();
    m_mipFramebuffers.clear();
    m_mipDescriptorSets.clear();

    // Calculate number of mip levels based on resolution
    int maxMips = static_cast<int>(std::floor(std::log2(std::min(width, height))));
    int actualMips = std::min(m_mipLevels, std::min(maxMips, MAX_MIP_LEVELS));

    uint32_t mipWidth = width / 2;
    uint32_t mipHeight = height / 2;

    for (int i = 0; i < actualMips; i++) {
        // Create texture for this mip level
        RHI::TextureDesc texDesc{};
        texDesc.type = RHI::TextureType::Texture2D;
        texDesc.format = RHI::Format::RGBA16_FLOAT;
        texDesc.width = std::max(mipWidth, 1u);
        texDesc.height = std::max(mipHeight, 1u);
        texDesc.depth = 1;
        texDesc.arrayLayers = 1;
        texDesc.mipLevels = 1;
        texDesc.samples = 1;
        texDesc.usage = RHI::TextureUsage::RenderTarget | RHI::TextureUsage::Sampled;
        texDesc.debugName = "Bloom_Mip" + std::to_string(i);

        auto mipTexture = m_device->createTexture(texDesc);
        if (!mipTexture) {
            std::cerr << "[BloomPassRHI] Failed to create mip " << i << " texture" << std::endl;
            break;
        }

        // Create framebuffer for this mip
        if (m_renderPass) {
            RHI::FramebufferDesc fbDesc{};
            fbDesc.renderPass = m_renderPass.get();
            fbDesc.width = texDesc.width;
            fbDesc.height = texDesc.height;
            fbDesc.colorAttachments.push_back({mipTexture.get(), 0, 0});

            auto fb = m_device->createFramebuffer(fbDesc);
            m_mipFramebuffers.push_back(std::move(fb));
        }

        m_mipChain.push_back(std::move(mipTexture));

        mipWidth /= 2;
        mipHeight /= 2;
    }

    // Create output texture at full resolution
    RHI::TextureDesc outDesc{};
    outDesc.type = RHI::TextureType::Texture2D;
    outDesc.format = RHI::Format::RGBA16_FLOAT;
    outDesc.width = width;
    outDesc.height = height;
    outDesc.depth = 1;
    outDesc.arrayLayers = 1;
    outDesc.mipLevels = 1;
    outDesc.samples = 1;
    outDesc.usage = RHI::TextureUsage::RenderTarget | RHI::TextureUsage::Sampled;
    outDesc.debugName = "Bloom_Output";

    m_outputTexture = m_device->createTexture(outDesc);

    if (m_renderPass && m_outputTexture) {
        RHI::FramebufferDesc fbDesc{};
        fbDesc.renderPass = m_renderPass.get();
        fbDesc.width = width;
        fbDesc.height = height;
        fbDesc.colorAttachments.push_back({m_outputTexture.get(), 0, 0});
        m_outputFramebuffer = m_device->createFramebuffer(fbDesc);
    }
}

void BloomPassRHI::destroyMipChain() {
    m_mipChain.clear();
    m_mipFramebuffers.clear();
    m_mipDescriptorSets.clear();
    m_outputTexture.reset();
    m_outputFramebuffer.reset();
}

void BloomPassRHI::execute(RHI::RHICommandBuffer* cmd, RenderContext& context) {
    if (!m_enabled || !m_inputTexture) return;
    if (m_mipChain.empty()) return;

    // Update bloom parameters
    if (m_bloomUBO) {
        struct BloomParams {
            float threshold;
            float softThreshold;
            float intensity;
            float exposure;
            glm::vec2 texelSize;
            float blendFactor;
            float padding;
        } params;

        params.threshold = m_threshold;
        params.softThreshold = m_softThreshold;
        params.intensity = m_intensity;
        params.exposure = 1.0f;
        params.texelSize = glm::vec2(1.0f / m_width, 1.0f / m_height);
        params.blendFactor = 0.5f;

        void* mapped = m_bloomUBO->map();
        if (mapped) {
            memcpy(mapped, &params, sizeof(params));
            m_bloomUBO->unmap();
        }
    }

    // For now, this is a placeholder that copies input to output
    // Full implementation would do:
    // 1. Extract bright pixels from input
    // 2. Downsample through mip chain
    // 3. Upsample back up, blurring at each level
    // 4. Combine with original scene

    // In hybrid mode with OpenGL, the actual bloom work can be done via GL calls
    // This pass structure supports future full-RHI implementation

    // The bloom output will be the input scene with bloom applied
    // For now, just mark it as available
}

} // namespace Render
