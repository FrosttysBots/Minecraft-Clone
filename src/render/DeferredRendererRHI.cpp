#include "DeferredRendererRHI.h"
#include "BackendSelector.h"
#include "../core/Config.h"

#include <iostream>
#include <chrono>

// Global config from main
extern GameConfig g_config;

namespace Render {

DeferredRendererRHI::DeferredRendererRHI() = default;

DeferredRendererRHI::~DeferredRendererRHI() {
    shutdown();
}

bool DeferredRendererRHI::initialize(GLFWwindow* window, const RenderConfig& config) {
    m_window = window;
    m_config = config;

    // Get window dimensions
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    m_displayWidth = static_cast<uint32_t>(width);
    m_displayHeight = static_cast<uint32_t>(height);

    // Calculate render resolution based on upscale mode
    float upscaleFactor = 1.0f;
    switch (config.upscaleMode) {
        case UpscaleMode::QUALITY:     upscaleFactor = 1.5f; break;
        case UpscaleMode::BALANCED:    upscaleFactor = 1.7f; break;
        case UpscaleMode::PERFORMANCE: upscaleFactor = 2.0f; break;
        case UpscaleMode::ULTRA_PERF:  upscaleFactor = 3.0f; break;
        default: break;
    }
    m_renderWidth = static_cast<uint32_t>(m_displayWidth / upscaleFactor);
    m_renderHeight = static_cast<uint32_t>(m_displayHeight / upscaleFactor);

    // Create RHI device
    if (!createDevice(window)) {
        std::cerr << "[DeferredRendererRHI] Failed to create RHI device" << std::endl;
        return false;
    }

    // Create swapchain
    if (!createSwapchain()) {
        std::cerr << "[DeferredRendererRHI] Failed to create swapchain" << std::endl;
        return false;
    }

    // Create descriptor pool
    if (!createDescriptorPools()) {
        std::cerr << "[DeferredRendererRHI] Failed to create descriptor pools" << std::endl;
        return false;
    }

    // Create command buffers and fences
    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_frameFences.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_commandBuffers[i] = m_device->createCommandBuffer(RHI::CommandBufferLevel::Primary);
        m_frameFences[i] = m_device->createFence(true);  // Start signaled
    }

    // Create samplers
    RHI::SamplerDesc linearDesc{};
    linearDesc.minFilter = RHI::Filter::Linear;
    linearDesc.magFilter = RHI::Filter::Linear;
    linearDesc.mipmapMode = RHI::MipmapMode::Linear;
    linearDesc.addressU = RHI::AddressMode::ClampToEdge;
    linearDesc.addressV = RHI::AddressMode::ClampToEdge;
    linearDesc.addressW = RHI::AddressMode::ClampToEdge;
    linearDesc.maxAnisotropy = 1.0f;
    m_linearSampler = m_device->createSampler(linearDesc);

    RHI::SamplerDesc nearestDesc = linearDesc;
    nearestDesc.minFilter = RHI::Filter::Nearest;
    nearestDesc.magFilter = RHI::Filter::Nearest;
    nearestDesc.mipmapMode = RHI::MipmapMode::Nearest;
    m_nearestSampler = m_device->createSampler(nearestDesc);

    RHI::SamplerDesc shadowDesc = linearDesc;
    shadowDesc.compareEnable = true;
    shadowDesc.compareOp = RHI::CompareOp::LessOrEqual;
    shadowDesc.borderColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    shadowDesc.addressU = RHI::AddressMode::ClampToBorder;
    shadowDesc.addressV = RHI::AddressMode::ClampToBorder;
    m_shadowSampler = m_device->createSampler(shadowDesc);

    // Initialize shader compiler
    m_shaderCompiler.setCacheDirectory("shader_cache");

    // Create render passes
    m_shadowPass = std::make_unique<ShadowPassRHI>(m_device.get());
    m_gBufferPass = std::make_unique<GBufferPassRHI>(m_device.get());
    m_hiZPass = std::make_unique<HiZPassRHI>(m_device.get());
    m_ssaoPass = std::make_unique<SSAOPassRHI>(m_device.get());
    m_gpuCullingPass = std::make_unique<GPUCullingPassRHI>(m_device.get());
    m_compositePass = std::make_unique<CompositePassRHI>(m_device.get());
    m_skyPass = std::make_unique<SkyPassRHI>(m_device.get());
    m_fsrPass = std::make_unique<FSRPassRHI>(m_device.get());

    // Initialize render passes
    if (!m_shadowPass->initialize(config)) {
        std::cerr << "[DeferredRendererRHI] Failed to initialize shadow pass" << std::endl;
        return false;
    }

    if (!m_gBufferPass->initialize(config)) {
        std::cerr << "[DeferredRendererRHI] Failed to initialize G-buffer pass" << std::endl;
        return false;
    }

    if (!m_hiZPass->initialize(config)) {
        std::cerr << "[DeferredRendererRHI] Failed to initialize Hi-Z pass" << std::endl;
        return false;
    }

    if (!m_ssaoPass->initialize(config)) {
        std::cerr << "[DeferredRendererRHI] Failed to initialize SSAO pass" << std::endl;
        return false;
    }

    if (!m_gpuCullingPass->initialize(config)) {
        std::cerr << "[DeferredRendererRHI] Failed to initialize GPU culling pass" << std::endl;
        return false;
    }

    if (!m_compositePass->initialize(config)) {
        std::cerr << "[DeferredRendererRHI] Failed to initialize composite pass" << std::endl;
        return false;
    }

    if (!m_skyPass->initialize(config)) {
        std::cerr << "[DeferredRendererRHI] Failed to initialize sky pass" << std::endl;
        return false;
    }

    if (!m_fsrPass->initialize(config)) {
        std::cerr << "[DeferredRendererRHI] Failed to initialize FSR pass" << std::endl;
        return false;
    }

    // Resize passes to initial dimensions
    resize(m_displayWidth, m_displayHeight);

    // Create pipelines
    if (!createPipelines()) {
        std::cerr << "[DeferredRendererRHI] Failed to create pipelines" << std::endl;
        return false;
    }

    // Connect passes
    m_ssaoPass->setGBufferTextures(
        m_gBufferPass->getPositionTexture(),
        m_gBufferPass->getNormalTexture(),
        m_gBufferPass->getDepthTexture()
    );

    m_hiZPass->setDepthTexture(m_gBufferPass->getDepthTexture());

    m_compositePass->setGBufferTextures(
        m_gBufferPass->getPositionTexture(),
        m_gBufferPass->getNormalTexture(),
        m_gBufferPass->getAlbedoTexture(),
        m_gBufferPass->getDepthTexture()
    );
    m_compositePass->setSSAOTexture(m_ssaoPass->getSSAOTexture());
    m_compositePass->setShadowMap(m_shadowPass->getShadowMapArray());

    m_fsrPass->setInputTexture(m_compositePass->getOutputTexture());

    std::cout << "[DeferredRendererRHI] Initialized with "
              << BackendSelector::getBackendName(g_config.renderer)
              << " backend" << std::endl;

    return true;
}

void DeferredRendererRHI::shutdown() {
    if (m_device) {
        m_device->waitIdle();
    }

    // Shutdown render passes
    if (m_fsrPass) m_fsrPass->shutdown();
    if (m_skyPass) m_skyPass->shutdown();
    if (m_compositePass) m_compositePass->shutdown();
    if (m_gpuCullingPass) m_gpuCullingPass->shutdown();
    if (m_ssaoPass) m_ssaoPass->shutdown();
    if (m_hiZPass) m_hiZPass->shutdown();
    if (m_gBufferPass) m_gBufferPass->shutdown();
    if (m_shadowPass) m_shadowPass->shutdown();

    // Clear render passes
    m_fsrPass.reset();
    m_skyPass.reset();
    m_compositePass.reset();
    m_gpuCullingPass.reset();
    m_ssaoPass.reset();
    m_hiZPass.reset();
    m_gBufferPass.reset();
    m_shadowPass.reset();

    // Clear pipelines
    m_shadowPipeline.reset();
    m_gBufferPipeline.reset();
    m_compositePipeline.reset();
    m_skyPipeline.reset();
    m_hiZPipeline.reset();
    m_ssaoPipeline.reset();
    m_ssaoBlurPipeline.reset();
    m_cullingPipeline.reset();
    m_fsrEASUPipeline.reset();
    m_fsrRCASPipeline.reset();

    // Clear shaders
    m_shaderPrograms.clear();

    // Clear samplers
    m_linearSampler.reset();
    m_nearestSampler.reset();
    m_shadowSampler.reset();

    // Clear command buffers and fences
    m_commandBuffers.clear();
    m_frameFences.clear();

    // Clear descriptor pool
    m_descriptorPool.reset();

    // Destroy swapchain
    destroySwapchain();

    // Destroy device
    m_device.reset();

    m_window = nullptr;
}

void DeferredRendererRHI::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;

    m_displayWidth = width;
    m_displayHeight = height;

    // Calculate render resolution
    float upscaleFactor = 1.0f;
    switch (m_config.upscaleMode) {
        case UpscaleMode::QUALITY:     upscaleFactor = 1.5f; break;
        case UpscaleMode::BALANCED:    upscaleFactor = 1.7f; break;
        case UpscaleMode::PERFORMANCE: upscaleFactor = 2.0f; break;
        case UpscaleMode::ULTRA_PERF:  upscaleFactor = 3.0f; break;
        default: break;
    }
    m_renderWidth = static_cast<uint32_t>(width / upscaleFactor);
    m_renderHeight = static_cast<uint32_t>(height / upscaleFactor);

    // Wait for GPU to finish
    m_device->waitIdle();

    // Recreate swapchain
    m_swapchain->resize(width, height);

    // Resize render passes
    m_shadowPass->resize(m_renderWidth, m_renderHeight);
    m_gBufferPass->resize(m_renderWidth, m_renderHeight);
    m_hiZPass->resize(m_renderWidth, m_renderHeight);
    m_ssaoPass->resize(m_renderWidth, m_renderHeight);
    m_gpuCullingPass->resize(m_renderWidth, m_renderHeight);
    m_compositePass->resize(m_renderWidth, m_renderHeight);
    m_skyPass->resize(width, height);
    m_fsrPass->setDimensions(m_renderWidth, m_renderHeight, width, height);

    // Reconnect textures after resize
    m_ssaoPass->setGBufferTextures(
        m_gBufferPass->getPositionTexture(),
        m_gBufferPass->getNormalTexture(),
        m_gBufferPass->getDepthTexture()
    );
    m_hiZPass->setDepthTexture(m_gBufferPass->getDepthTexture());
    m_compositePass->setGBufferTextures(
        m_gBufferPass->getPositionTexture(),
        m_gBufferPass->getNormalTexture(),
        m_gBufferPass->getAlbedoTexture(),
        m_gBufferPass->getDepthTexture()
    );
    m_compositePass->setSSAOTexture(m_ssaoPass->getSSAOTexture());
    m_fsrPass->setInputTexture(m_compositePass->getOutputTexture());
}

void DeferredRendererRHI::beginFrame() {
    // Wait for this frame's fence
    m_frameFences[m_currentFrame]->wait(UINT64_MAX);
    m_frameFences[m_currentFrame]->reset();

    // Acquire next swapchain image
    if (!m_swapchain->acquireNextImage()) {
        // Swapchain out of date, resize
        int width, height;
        glfwGetFramebufferSize(m_window, &width, &height);
        resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        m_swapchain->acquireNextImage();
    }

    // Begin command buffer recording
    m_commandBuffers[m_currentFrame]->reset();
    m_commandBuffers[m_currentFrame]->begin();

    m_frameNumber++;
}

void DeferredRendererRHI::render(::World& world, const CameraData& camera) {
    auto* cmd = m_commandBuffers[m_currentFrame].get();

    // Setup render context
    m_context.world = &world;
    m_context.camera = &camera;
    m_context.lighting = &m_lighting;
    m_context.fog = &m_fog;
    m_context.config = &m_config;
    m_context.frameNumber = m_frameNumber;

    // Reset stats for this frame
    m_context.stats = RenderStats{};

    // Execute render passes
    // 1. Shadow Pass
    if (m_config.enableShadows) {
        m_shadowPass->execute(cmd, m_context);
    }

    // 2. G-Buffer Pass
    m_gBufferPass->execute(cmd, m_context);

    // 3. Hi-Z Pass (for occlusion culling)
    if (m_config.enableHiZCulling) {
        m_hiZPass->execute(cmd, m_context);
    }

    // 4. GPU Culling Pass
    if (m_config.enableGPUCulling) {
        m_gpuCullingPass->execute(cmd, m_context);
    }

    // 5. SSAO Pass
    if (m_config.enableSSAO) {
        m_ssaoPass->execute(cmd, m_context);
    }

    // 6. Composite Pass (lighting calculation)
    m_compositePass->execute(cmd, m_context);

    // 7. Sky Pass (rendered into composite output)
    m_skyPass->execute(cmd, m_context);

    // 8. FSR Upscaling Pass
    if (m_config.upscaleMode != UpscaleMode::NATIVE) {
        m_fsrPass->execute(cmd, m_context);
    }

    // 9. Present to swapchain
    // TODO: Final blit to swapchain

    // Copy accumulated stats
    m_stats = m_context.stats;
}

void DeferredRendererRHI::endFrame() {
    auto* cmd = m_commandBuffers[m_currentFrame].get();

    // End command buffer recording
    cmd->end();

    // Submit command buffer
    m_device->getGraphicsQueue()->submit({cmd});

    // Present
    if (!m_swapchain->present()) {
        // Swapchain out of date
        int width, height;
        glfwGetFramebufferSize(m_window, &width, &height);
        resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    }

    // Advance frame
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void DeferredRendererRHI::setConfig(const RenderConfig& config) {
    m_config = config;

    // Update pass enable states
    m_shadowPass->setEnabled(config.enableShadows);
    m_ssaoPass->setEnabled(config.enableSSAO);
    m_hiZPass->setEnabled(config.enableHiZCulling);
    m_gpuCullingPass->setEnabled(config.enableGPUCulling);

    // Recalculate render resolution if upscale mode changed
    float upscaleFactor = 1.0f;
    switch (config.upscaleMode) {
        case UpscaleMode::QUALITY:     upscaleFactor = 1.5f; break;
        case UpscaleMode::BALANCED:    upscaleFactor = 1.7f; break;
        case UpscaleMode::PERFORMANCE: upscaleFactor = 2.0f; break;
        case UpscaleMode::ULTRA_PERF:  upscaleFactor = 3.0f; break;
        default: break;
    }

    uint32_t newRenderWidth = static_cast<uint32_t>(m_displayWidth / upscaleFactor);
    uint32_t newRenderHeight = static_cast<uint32_t>(m_displayHeight / upscaleFactor);

    if (newRenderWidth != m_renderWidth || newRenderHeight != m_renderHeight) {
        resize(m_displayWidth, m_displayHeight);
    }
}

void DeferredRendererRHI::setLighting(const LightingParams& lighting) {
    m_lighting = lighting;
}

void DeferredRendererRHI::setFog(const FogParams& fog) {
    m_fog = fog;
}

void DeferredRendererRHI::setDebugMode(int mode) {
    m_config.debugMode = mode;
}

bool DeferredRendererRHI::createDevice(GLFWwindow* window) {
    RHI::Backend backend = BackendSelector::toRHIBackend(g_config.renderer);
    m_device = RHI::RHIDevice::create(backend, window);

    if (!m_device) {
        std::cerr << "[DeferredRendererRHI] Failed to create RHI device" << std::endl;
        return false;
    }

    const auto& info = m_device->getInfo();
    std::cout << "[DeferredRendererRHI] Device: " << info.deviceName << std::endl;
    std::cout << "[DeferredRendererRHI] API: " << info.apiVersion << std::endl;

    return true;
}

bool DeferredRendererRHI::createSwapchain() {
    RHI::SwapchainDesc swapDesc{};
    swapDesc.width = m_displayWidth;
    swapDesc.height = m_displayHeight;
    swapDesc.format = RHI::Format::BGRA8_SRGB;
    swapDesc.vsync = g_config.vsync;
    swapDesc.imageCount = 3;  // Triple buffering

    m_swapchain = m_device->createSwapchain(swapDesc);
    return m_swapchain != nullptr;
}

void DeferredRendererRHI::destroySwapchain() {
    m_swapchain.reset();
}

bool DeferredRendererRHI::createDescriptorPools() {
    RHI::DescriptorPoolDesc poolDesc{};
    poolDesc.maxSets = 100;
    poolDesc.poolSizes = {
        {RHI::DescriptorType::UniformBuffer, 50},
        {RHI::DescriptorType::StorageBuffer, 50},
        {RHI::DescriptorType::SampledTexture, 100},
        {RHI::DescriptorType::StorageTexture, 50},
        {RHI::DescriptorType::Sampler, 20}
    };

    m_descriptorPool = m_device->createDescriptorPool(poolDesc);
    return m_descriptorPool != nullptr;
}

bool DeferredRendererRHI::createPipelines() {
    // Pipeline creation requires shader compilation and descriptor set layout creation
    // This is a placeholder - actual implementation would load and compile shaders

    // For now, we mark pipelines as needing to be set externally
    // In a full implementation, we would:
    // 1. Load shader source from files
    // 2. Compile to SPIR-V using ShaderCompiler
    // 3. Create shader modules
    // 4. Create pipeline layouts with descriptor set layouts
    // 5. Create graphics/compute pipelines

    std::cout << "[DeferredRendererRHI] Pipelines would be created here" << std::endl;

    return true;
}

} // namespace Render
