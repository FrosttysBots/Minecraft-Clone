#include "DeferredRendererRHI.h"
#include "BackendSelector.h"
#include "../core/Config.h"

#include <glad/gl.h>
#include <iostream>
#include <chrono>
#include <fstream>
#include <sstream>

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
    m_waterPass = std::make_unique<WaterPassRHI>(m_device.get());
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

    if (!m_waterPass->initialize(config)) {
        std::cerr << "[DeferredRendererRHI] Failed to initialize water pass" << std::endl;
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

    // Initialize world renderer
    m_worldRenderer = std::make_unique<WorldRendererRHI>();
    if (!m_worldRenderer->initialize(m_device.get())) {
        std::cerr << "[DeferredRendererRHI] Failed to initialize world renderer" << std::endl;
        return false;
    }

    // Connect water pass to world renderer
    m_waterPass->setWorldRenderer(m_worldRenderer.get());

    // Initialize RHI vertex pool
    m_vertexPool = std::make_unique<VertexPoolRHI>();
    if (!m_vertexPool->initialize(m_device.get())) {
        std::cerr << "[DeferredRendererRHI] Failed to initialize vertex pool" << std::endl;
        // Not fatal - World can still use legacy VertexPool
        m_vertexPool.reset();
    }

    // Report actual backend being used (may differ from config if fallback occurred)
    const char* backendName = (m_device->getBackend() == RHI::Backend::Vulkan) ? "Vulkan" : "OpenGL 4.6";
    std::cout << "[DeferredRendererRHI] Initialized with " << backendName << " backend" << std::endl;

    return true;
}

void DeferredRendererRHI::shutdown() {
    if (m_device) {
        m_device->waitIdle();
    }

    // Cleanup OpenGL blit FBO
    if (m_blitFBO != 0) {
        glDeleteFramebuffers(1, &m_blitFBO);
        m_blitFBO = 0;
    }

    // Shutdown world renderer
    if (m_worldRenderer) {
        m_worldRenderer->shutdown();
        m_worldRenderer.reset();
    }

    // Shutdown vertex pool
    if (m_vertexPool) {
        m_vertexPool->shutdown();
        m_vertexPool.reset();
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

    // 2. G-Buffer Pass - use split begin/end for hybrid World rendering
    m_gBufferPass->beginPass(cmd, m_context);

    // Render world geometry into G-Buffer (hybrid path - uses OpenGL)
    // The framebuffer is bound by beginPass, so World's GL calls render to RHI G-Buffer
    if (m_worldRenderer && m_context.world) {
        WorldRenderParams params;
        params.cameraPosition = camera.position;
        params.viewProjection = camera.viewProjection;
        params.mode = m_config.enableGPUCulling ? WorldRenderMode::GPUCulled : WorldRenderMode::Batched;
        params.renderWater = false;  // Water rendered in separate pass

        m_worldRenderer->renderSolid(cmd, *m_context.world, params);

        // Update stats from world renderer
        m_context.stats.chunksRendered = m_worldRenderer->getRenderedSubChunks();
        m_context.stats.chunksCulled = m_worldRenderer->getCulledSubChunks();
    }

    // End G-Buffer pass and store texture handles
    m_gBufferPass->endPass(cmd);
    m_gBufferPass->storeTextureHandles(m_context);

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

    // 8. Water Pass (forward rendered, semi-transparent)
    m_waterPass->execute(cmd, m_context);

    // 9. FSR Upscaling Pass
    if (m_config.upscaleMode != UpscaleMode::NATIVE) {
        m_fsrPass->execute(cmd, m_context);
    }

    // 9. Final blit to default framebuffer (screen)
    // Get the final output texture (FSR output if enabled, composite output otherwise)
    RHI::RHITexture* finalOutput = nullptr;
    RHI::RHIFramebuffer* sourceFramebuffer = nullptr;

    if (m_config.upscaleMode != UpscaleMode::NATIVE && m_fsrPass) {
        finalOutput = m_fsrPass->getOutputTexture();
        // FSR doesn't have a framebuffer, need to use its output texture
    } else {
        finalOutput = m_compositePass->getOutputTexture();
        sourceFramebuffer = m_compositePass->getFramebuffer();
    }

    if (finalOutput && m_device->getBackend() == RHI::Backend::OpenGL) {
        // OpenGL: Blit the output to the default framebuffer
        GLuint srcTexture = static_cast<GLuint>(reinterpret_cast<uintptr_t>(finalOutput->getNativeHandle()));

        // Get depth texture from G-Buffer for forward pass compatibility
        RHI::RHITexture* depthTexture = m_gBufferPass->getDepthTexture();
        GLuint srcDepth = depthTexture ?
            static_cast<GLuint>(reinterpret_cast<uintptr_t>(depthTexture->getNativeHandle())) : 0;

        // Create temporary FBO for reading if needed
        if (m_blitFBO == 0) {
            glGenFramebuffers(1, &m_blitFBO);
        }

        // Attach source textures to read FBO
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_blitFBO);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcTexture, 0);
        if (srcDepth != 0) {
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, srcDepth, 0);
        }

        // Bind default framebuffer for drawing
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

        // Blit color from RHI output to screen
        glBlitFramebuffer(
            0, 0, m_renderWidth, m_renderHeight,    // Source rect (render resolution)
            0, 0, m_displayWidth, m_displayHeight,  // Dest rect (display resolution)
            GL_COLOR_BUFFER_BIT,
            GL_LINEAR  // Linear filtering for upscaling
        );

        // Blit depth buffer for forward passes (water, particles, etc.)
        if (srcDepth != 0) {
            glBlitFramebuffer(
                0, 0, m_renderWidth, m_renderHeight,    // Source rect
                0, 0, m_displayWidth, m_displayHeight,  // Dest rect
                GL_DEPTH_BUFFER_BIT,
                GL_NEAREST  // Depth must use nearest filtering
            );
        }

        // Restore framebuffer binding
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

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
    // Check if window has OpenGL context - if so, we must use OpenGL backend
    // Vulkan requires GLFW_NO_API and won't work with an OpenGL window
    RHI::Backend backend = BackendSelector::toRHIBackend(g_config.renderer);

    // If requested Vulkan but window has OpenGL context, fallback to OpenGL
    if (backend == RHI::Backend::Vulkan) {
        if (glfwGetWindowAttrib(window, GLFW_CLIENT_API) != GLFW_NO_API) {
            std::cout << "[DeferredRendererRHI] Window has OpenGL context, using OpenGL backend" << std::endl;
            backend = RHI::Backend::OpenGL;
        }
    }

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
    // Initialize shader compiler
    ShaderCompiler::initialize();

    // Determine backend from actual device (may differ from config if fallback occurred)
    bool isVulkanBackend = (m_device->getBackend() == RHI::Backend::Vulkan);

    ShaderCompileOptions options;
    options.glslVersion = 460;
    options.vulkanSemantics = isVulkanBackend;
    options.optimizePerformance = true;

    bool useSpirv = isVulkanBackend;

    // Helper to read file contents
    auto readFileContents = [](const std::filesystem::path& path) -> std::string {
        std::ifstream file(path);
        if (!file.is_open()) return "";
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    };

    // Helper lambda to load and create a shader program
    auto loadShaderProgram = [this, &options, useSpirv, &readFileContents](const std::string& name,
                                               const std::filesystem::path& vertPath,
                                               const std::filesystem::path& fragPath) -> RHI::RHIShaderProgram* {
        RHI::ShaderProgramDesc progDesc{};

        if (useSpirv) {
            // Vulkan: compile to SPIR-V
            auto vertShader = m_shaderCompiler.loadShader(vertPath, ShaderStage::Vertex, options);
            auto fragShader = m_shaderCompiler.loadShader(fragPath, ShaderStage::Fragment, options);

            if (!vertShader || !fragShader) {
                std::cerr << "[DeferredRendererRHI] Failed to load shaders for " << name << std::endl;
                std::cerr << "  Error: " << m_shaderCompiler.getLastError() << std::endl;
                return nullptr;
            }

            auto convertSpirvToBytes = [](const std::vector<uint32_t>& spirv) -> std::vector<uint8_t> {
                std::vector<uint8_t> bytes(spirv.size() * sizeof(uint32_t));
                memcpy(bytes.data(), spirv.data(), bytes.size());
                return bytes;
            };

            RHI::ShaderSource vertSrc;
            vertSrc.stage = RHI::ShaderStage::Vertex;
            vertSrc.type = RHI::ShaderSourceType::SPIRV;
            vertSrc.spirv = convertSpirvToBytes(vertShader->spirv);
            vertSrc.entryPoint = "main";

            RHI::ShaderSource fragSrc;
            fragSrc.stage = RHI::ShaderStage::Fragment;
            fragSrc.type = RHI::ShaderSourceType::SPIRV;
            fragSrc.spirv = convertSpirvToBytes(fragShader->spirv);
            fragSrc.entryPoint = "main";

            progDesc.stages = {vertSrc, fragSrc};
        } else {
            // OpenGL: use GLSL source directly
            std::string vertGlsl = readFileContents(vertPath);
            std::string fragGlsl = readFileContents(fragPath);

            if (vertGlsl.empty() || fragGlsl.empty()) {
                std::cerr << "[DeferredRendererRHI] Failed to read shader files for " << name << std::endl;
                return nullptr;
            }

            RHI::ShaderSource vertSrc = RHI::ShaderSource::fromGLSL(RHI::ShaderStage::Vertex, vertGlsl);
            RHI::ShaderSource fragSrc = RHI::ShaderSource::fromGLSL(RHI::ShaderStage::Fragment, fragGlsl);

            progDesc.stages = {vertSrc, fragSrc};
        }

        progDesc.debugName = name;

        auto program = m_device->createShaderProgram(progDesc);
        if (program) {
            m_shaderPrograms[name] = std::move(program);
        }
        return m_shaderPrograms[name].get();
    };

    // Helper lambda to load compute shaders
    auto loadComputeProgram = [this, &options, useSpirv, &readFileContents](const std::string& name,
                                                const std::filesystem::path& compPath) -> RHI::RHIShaderProgram* {
        RHI::ShaderProgramDesc progDesc{};

        if (useSpirv) {
            // Vulkan: compile to SPIR-V
            auto compShader = m_shaderCompiler.loadShader(compPath, ShaderStage::Compute, options);

            if (!compShader) {
                std::cerr << "[DeferredRendererRHI] Failed to load compute shader for " << name << std::endl;
                std::cerr << "  Error: " << m_shaderCompiler.getLastError() << std::endl;
                return nullptr;
            }

            auto convertSpirvToBytes = [](const std::vector<uint32_t>& spirv) -> std::vector<uint8_t> {
                std::vector<uint8_t> bytes(spirv.size() * sizeof(uint32_t));
                memcpy(bytes.data(), spirv.data(), bytes.size());
                return bytes;
            };

            RHI::ShaderSource compSrc;
            compSrc.stage = RHI::ShaderStage::Compute;
            compSrc.type = RHI::ShaderSourceType::SPIRV;
            compSrc.spirv = convertSpirvToBytes(compShader->spirv);
            compSrc.entryPoint = "main";

            progDesc.stages = {compSrc};
        } else {
            // OpenGL: use GLSL source directly
            std::string compGlsl = readFileContents(compPath);

            if (compGlsl.empty()) {
                std::cerr << "[DeferredRendererRHI] Failed to read compute shader for " << name << std::endl;
                return nullptr;
            }

            RHI::ShaderSource compSrc = RHI::ShaderSource::fromGLSL(RHI::ShaderStage::Compute, compGlsl);
            progDesc.stages = {compSrc};
        }

        progDesc.debugName = name;

        auto program = m_device->createShaderProgram(progDesc);
        if (program) {
            m_shaderPrograms[name] = std::move(program);
        }
        return m_shaderPrograms[name].get();
    };

    // Load all shader programs
    std::cout << "[DeferredRendererRHI] Loading shaders..." << std::endl;

    // G-Buffer shaders
    auto gbufferProg = loadShaderProgram("gbuffer",
        "shaders/deferred/gbuffer.vert",
        "shaders/deferred/gbuffer.frag");

    // Shadow shaders
    auto shadowProg = loadShaderProgram("shadow",
        "shaders/forward/shadow.vert",
        "shaders/forward/shadow.frag");

    // Composite shaders
    auto compositeProg = loadShaderProgram("composite",
        "shaders/deferred/composite.vert",
        "shaders/deferred/composite.frag");

    // SSAO shaders (using fragment shader approach)
    auto ssaoProg = loadShaderProgram("ssao",
        "shaders/postprocess/ssao.vert",
        "shaders/postprocess/ssao.frag");

    auto ssaoBlurProg = loadShaderProgram("ssao_blur",
        "shaders/postprocess/ssao.vert",
        "shaders/postprocess/ssao_blur.frag");

    // FSR shaders
    auto fsrEasuProg = loadShaderProgram("fsr_easu",
        "shaders/postprocess/fsr_easu.vert",
        "shaders/postprocess/fsr_easu.frag");

    auto fsrRcasProg = loadShaderProgram("fsr_rcas",
        "shaders/postprocess/fsr_easu.vert",
        "shaders/postprocess/fsr_rcas.frag");

    // Compute shaders
    auto hiZProg = loadComputeProgram("hiz_downsample",
        "shaders/compute/hiz_downsample.comp");

    auto cullingProg = loadComputeProgram("occlusion_cull",
        "shaders/compute/occlusion_cull.comp");

    // Create vertex input layout for chunk geometry
    RHI::VertexInputState chunkVertexInput{};
    chunkVertexInput.bindings = {{
        0,                              // binding
        sizeof(float) * 3 + sizeof(float) * 2 + sizeof(uint32_t),  // stride (pos + uv + packed data)
        RHI::VertexInputRate::Vertex
    }};
    chunkVertexInput.attributes = {
        {0, 0, RHI::Format::RGB32_FLOAT, 0},                          // position
        {1, 0, RHI::Format::RG32_FLOAT, sizeof(float) * 3},           // texcoord
        {2, 0, RHI::Format::RGBA8_UINT, sizeof(float) * 5}            // packed data
    };

    // Create G-Buffer pipeline
    if (gbufferProg && m_gBufferPass->getRenderPass()) {
        RHI::GraphicsPipelineDesc gbufferPipeDesc{};
        gbufferPipeDesc.shaderProgram = gbufferProg;
        gbufferPipeDesc.vertexInput = chunkVertexInput;
        gbufferPipeDesc.rasterizer.cullMode = RHI::CullMode::Back;
        gbufferPipeDesc.rasterizer.frontFace = RHI::FrontFace::CounterClockwise;
        gbufferPipeDesc.depthStencil.depthTestEnable = true;
        gbufferPipeDesc.depthStencil.depthWriteEnable = true;
        gbufferPipeDesc.depthStencil.depthCompareOp = RHI::CompareOp::LessOrEqual;
        gbufferPipeDesc.colorBlendStates = {
            {false},  // Position
            {false},  // Normal
            {false}   // Albedo
        };
        gbufferPipeDesc.renderPass = m_gBufferPass->getRenderPass();
        gbufferPipeDesc.debugName = "GBufferPipeline";

        m_gBufferPipeline = m_device->createGraphicsPipeline(gbufferPipeDesc);
        if (m_gBufferPipeline) {
            m_gBufferPass->setPipeline(m_gBufferPipeline.get());
        }
    }

    // Create shadow pipeline
    if (shadowProg && m_shadowPass->getRenderPass()) {
        RHI::GraphicsPipelineDesc shadowPipeDesc{};
        shadowPipeDesc.shaderProgram = shadowProg;
        shadowPipeDesc.vertexInput = chunkVertexInput;
        shadowPipeDesc.rasterizer.cullMode = RHI::CullMode::Front;  // Front face culling for shadows
        shadowPipeDesc.rasterizer.depthBiasEnable = true;
        shadowPipeDesc.depthStencil.depthTestEnable = true;
        shadowPipeDesc.depthStencil.depthWriteEnable = true;
        shadowPipeDesc.renderPass = m_shadowPass->getRenderPass();
        shadowPipeDesc.debugName = "ShadowPipeline";

        m_shadowPipeline = m_device->createGraphicsPipeline(shadowPipeDesc);
        if (m_shadowPipeline) {
            m_shadowPass->setPipeline(m_shadowPipeline.get());
        }
    }

    // Create composite pipeline (fullscreen quad)
    if (compositeProg && m_compositePass->getRenderPass()) {
        RHI::VertexInputState quadVertexInput{};
        quadVertexInput.bindings = {{0, sizeof(float) * 2, RHI::VertexInputRate::Vertex}};
        quadVertexInput.attributes = {{0, 0, RHI::Format::RG32_FLOAT, 0}};

        RHI::GraphicsPipelineDesc compositePipeDesc{};
        compositePipeDesc.shaderProgram = compositeProg;
        compositePipeDesc.vertexInput = quadVertexInput;
        compositePipeDesc.rasterizer.cullMode = RHI::CullMode::None;
        compositePipeDesc.depthStencil.depthTestEnable = false;
        compositePipeDesc.depthStencil.depthWriteEnable = false;
        compositePipeDesc.colorBlendStates = {{false}};
        compositePipeDesc.renderPass = m_compositePass->getRenderPass();
        compositePipeDesc.debugName = "CompositePipeline";

        m_compositePipeline = m_device->createGraphicsPipeline(compositePipeDesc);
        if (m_compositePipeline) {
            m_compositePass->setPipeline(m_compositePipeline.get());
        }
    }

    // Create Hi-Z compute pipeline
    if (hiZProg) {
        RHI::ComputePipelineDesc hiZPipeDesc{};
        hiZPipeDesc.shaderProgram = hiZProg;
        hiZPipeDesc.debugName = "HiZPipeline";

        m_hiZPipeline = m_device->createComputePipeline(hiZPipeDesc);
        if (m_hiZPipeline) {
            m_hiZPass->setComputePipeline(m_hiZPipeline.get());
        }
    }

    // Create GPU culling compute pipeline
    if (cullingProg) {
        RHI::ComputePipelineDesc cullingPipeDesc{};
        cullingPipeDesc.shaderProgram = cullingProg;
        cullingPipeDesc.debugName = "GPUCullingPipeline";

        m_cullingPipeline = m_device->createComputePipeline(cullingPipeDesc);
        if (m_cullingPipeline) {
            m_gpuCullingPass->setComputePipeline(m_cullingPipeline.get());
        }
    }

    std::cout << "[DeferredRendererRHI] Pipelines created successfully" << std::endl;
    return true;
}

} // namespace Render
