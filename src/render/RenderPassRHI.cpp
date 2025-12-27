#include "RenderPassRHI.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <random>
#include <algorithm>
#include <cmath>

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
    if (!m_enabled) return;
    // TODO: Implement shadow pass execution
    (void)cmd;
    (void)context;
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
    (void)camera;
    (void)nearSplit;
    (void)farSplit;
    (void)lightDir;
    return glm::mat4(1.0f);  // TODO: Implement cascade matrix calculation
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
    if (!m_enabled) return;
    (void)cmd;
    (void)context;
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
    if (!m_enabled) return;
    (void)cmd;
    (void)context;
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
    if (!m_enabled) return;
    (void)cmd;
    (void)context;
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

    generateKernelAndNoise();

    std::cout << "[SSAOPassRHI] Created SSAO buffer (" << width << "x" << height
              << ", " << m_kernelSize << " samples)" << std::endl;
}

void SSAOPassRHI::destroySSAOBuffers() {
    m_ssaoTexture.reset();
    m_ssaoBlurred.reset();
    m_noiseTexture.reset();
    m_kernelBuffer.reset();
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
    if (!m_enabled) return;
    (void)cmd;
    (void)context;
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
    if (!m_enabled) return;
    (void)cmd;
    (void)context;
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

    std::cout << "[CompositePassRHI] Created scene buffer (" << width << "x" << height << ")" << std::endl;
}

void CompositePassRHI::destroySceneBuffer() {
    m_framebuffer.reset();
    m_renderPass.reset();
    m_sceneColor.reset();
    m_sceneDepth.reset();
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
    RHI::BufferDesc uboDesc{};
    uboDesc.size = sizeof(glm::mat4) * 2 + sizeof(glm::vec4) * 4;
    uboDesc.usage = RHI::BufferUsage::Uniform;
    uboDesc.memory = RHI::MemoryUsage::CpuToGpu;
    uboDesc.debugName = "SkyUBO";
    m_skyUBO = m_device->createBuffer(uboDesc);

    if (!m_skyUBO) {
        std::cerr << "[SkyPassRHI] Failed to create sky UBO" << std::endl;
        return false;
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
    (void)cmd;
    (void)context;
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
    if (!m_enabled) return;
    (void)cmd;
    (void)context;
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

} // namespace Render
