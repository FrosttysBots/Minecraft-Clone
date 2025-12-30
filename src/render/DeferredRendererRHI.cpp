#include "DeferredRendererRHI.h"
#include "BackendSelector.h"
#include "../core/Config.h"
#include "../core/CrashHandler.h"

// stb_easy_font for text rendering (generates quad vertices for ASCII text)
#include "stb_easy_font.h"

#include <glad/gl.h>
#include <iostream>
#include <chrono>
#include <fstream>
#include <sstream>
#include <array>
#include <random>
#include <algorithm>

// GLM matrix functions for test camera
#include <glm/gtc/matrix_transform.hpp>

// For PackedChunkVertex
#include "ChunkMesh.h"

// World class for accessing meshes
#include "../world/World.h"

// WIP: Vulkan includes for swapchain blit (disabled while focusing on OpenGL)
#ifndef DISABLE_VULKAN
#include "rhi/vulkan/VKTexture.h"
#include "rhi/vulkan/VKCommandBuffer.h"
#include "rhi/vulkan/VKFramebuffer.h"
#include "rhi/vulkan/VKDevice.h"
#include "rhi/vulkan/VKBuffer.h"
#include "rhi/vulkan/VKPipeline.h"
#endif

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
    m_precipitationPass = std::make_unique<PrecipitationPassRHI>(m_device.get());
    m_bloomPass = std::make_unique<BloomPassRHI>(m_device.get());
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

    if (!m_precipitationPass->initialize(config)) {
        std::cerr << "[DeferredRendererRHI] Failed to initialize precipitation pass" << std::endl;
        return false;
    }

    if (!m_bloomPass->initialize(config)) {
        std::cerr << "[DeferredRendererRHI] Failed to initialize bloom pass" << std::endl;
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

    // Connect water pass to world renderer and target framebuffer
    m_waterPass->setWorldRenderer(m_worldRenderer.get());
    m_waterPass->setTargetFramebuffer(m_compositePass->getFramebuffer());

    // Connect precipitation pass to target framebuffer
    m_precipitationPass->setTargetFramebuffer(m_compositePass->getFramebuffer());

    // Connect bloom pass to composite output
    m_bloomPass->setInputTexture(m_compositePass->getOutputTexture());

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

    // Clean up test resources
    m_testPipeline.reset();
    m_testShaderProgram.reset();

    // Clean up terrain test resources
    m_terrainTestPipeline.reset();
    m_terrainTestShader.reset();
    m_testCubeVBO.reset();
    m_testCameraUBO.reset();
    m_terrainAtlas.reset();
    m_terrainSampler.reset();
#ifndef DISABLE_VULKAN
    if (m_terrainDescriptorPool != VK_NULL_HANDLE && m_device) {
        auto* vkDevice = static_cast<RHI::VKDevice*>(m_device.get());
        if (vkDevice) {
            vkDestroyDescriptorPool(vkDevice->getDevice(), m_terrainDescriptorPool, nullptr);
        }
        m_terrainDescriptorPool = VK_NULL_HANDLE;
        m_terrainDescriptorSet = VK_NULL_HANDLE;  // Destroyed with pool
    }
    if (m_terrainDescriptorLayout != VK_NULL_HANDLE && m_device) {
        auto* vkDevice = static_cast<RHI::VKDevice*>(m_device.get());
        if (vkDevice) {
            vkDestroyDescriptorSetLayout(vkDevice->getDevice(), m_terrainDescriptorLayout, nullptr);
        }
        m_terrainDescriptorLayout = VK_NULL_HANDLE;
    }
#endif
    // Note: terrainPipelineLayout is created as local variable and stored in pipeline
    // The pipeline itself will clean it up when destroyed

    // Clean up UI resources
    m_uiPipeline.reset();
    m_uiShader.reset();
    m_uiQuadVBO.reset();
    m_uiUniformBuffer.reset();
#ifndef DISABLE_VULKAN
    if (m_uiPipelineLayout != VK_NULL_HANDLE && m_device) {
        auto* vkDevice = static_cast<RHI::VKDevice*>(m_device.get());
        if (vkDevice) {
            vkDestroyPipelineLayout(vkDevice->getDevice(), m_uiPipelineLayout, nullptr);
        }
        m_uiPipelineLayout = VK_NULL_HANDLE;
    }
#endif
    m_uiResourcesInitialized = false;

    // Clean up UI text resources
    m_uiTextPipeline.reset();
    m_uiTextShader.reset();
    m_uiTextVBO.reset();
#ifndef DISABLE_VULKAN
    if (m_uiTextPipelineLayout != VK_NULL_HANDLE && m_device) {
        auto* vkDevice = static_cast<RHI::VKDevice*>(m_device.get());
        if (vkDevice) {
            vkDestroyPipelineLayout(vkDevice->getDevice(), m_uiTextPipelineLayout, nullptr);
        }
        m_uiTextPipelineLayout = VK_NULL_HANDLE;
    }
#endif
    m_uiTextResourcesInitialized = false;

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
    m_precipitationPass->resize(m_renderWidth, m_renderHeight);
    m_bloomPass->resize(m_renderWidth, m_renderHeight);
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
    // Wait for this frame's fence (ensures GPU finished with resources from this frame slot)
    if (m_device->getBackend() == RHI::Backend::Vulkan) {
        // For Vulkan, the swapchain handles synchronization in acquireNextImage()
        // which waits on its own per-frame fences
    } else {
        // OpenGL: use our own fences
        if (m_currentFrame >= m_frameFences.size() || !m_frameFences[m_currentFrame]) {
            std::cerr << "[DeferredRendererRHI] ERROR: Invalid fence at frame " << m_currentFrame << std::endl;
            return;
        }
        m_frameFences[m_currentFrame]->wait(UINT64_MAX);
        m_frameFences[m_currentFrame]->reset();
    }

    // Acquire next swapchain image
    if (!m_swapchain) {
        std::cerr << "[DeferredRendererRHI] ERROR: Swapchain is null!" << std::endl;
        return;
    }
    if (!m_swapchain->acquireNextImage()) {
        // Swapchain out of date, resize
        int width, height;
        glfwGetFramebufferSize(m_window, &width, &height);
        resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        m_swapchain->acquireNextImage();
    }

    // Begin command buffer recording
    if (m_currentFrame >= m_commandBuffers.size() || !m_commandBuffers[m_currentFrame]) {
        std::cerr << "[DeferredRendererRHI] ERROR: Invalid command buffer at frame " << m_currentFrame << std::endl;
        return;
    }
    m_commandBuffers[m_currentFrame]->reset();
    m_commandBuffers[m_currentFrame]->begin();

    m_frameNumber++;
}

void DeferredRendererRHI::render(::World& world, const CameraData& camera) {
    static bool firstFrame = true;
    if (firstFrame) {
        LOG_DEBUG("RHI", "DeferredRendererRHI::render starting first frame");
    }

    auto* cmd = m_commandBuffers[m_currentFrame].get();
    if (!cmd) {
        LOG_ERROR("RHI", "Command buffer is null!");
        return;
    }

    // WIP: Vulkan rendering path (disabled while focusing on OpenGL)
#ifndef DISABLE_VULKAN
    // For Vulkan: Render terrain using camera matrices
    // TODO: This path should eventually merge with the OpenGL deferred path
    if (m_device->getBackend() == RHI::Backend::Vulkan) {
        // Get swapchain render pass and framebuffer through RHI interface
        RHI::RHIRenderPass* renderPass = m_swapchain->getSwapchainRenderPass();
        RHI::RHIFramebuffer* framebuffer = m_swapchain->getCurrentFramebufferRHI();
        uint32_t width = m_swapchain->getWidth();
        uint32_t height = m_swapchain->getHeight();

        if (firstFrame) {
            std::cout << "[RHI] Vulkan terrain render path:" << std::endl;
            std::cout << "  terrainTestPipeline = " << (void*)m_terrainTestPipeline.get() << std::endl;
            std::cout << "  testCubeVBO = " << (void*)m_testCubeVBO.get() << std::endl;
            std::cout << "  testCameraUBO = " << (void*)m_testCameraUBO.get() << std::endl;
        }

        // Camera UBO is updated per-chunk in the render loop below
        if (firstFrame && m_testCameraUBO) {
            std::cout << "[RHI] Camera position: " << camera.position.x << ", "
                      << camera.position.y << ", " << camera.position.z << std::endl;
        }

        if (renderPass && framebuffer) {
            // Begin render pass using RHI command buffer
            std::vector<RHI::ClearValue> clearValues = {
                RHI::ClearValue::Color(0.4f, 0.6f, 0.9f, 1.0f),  // Sky blue background
                RHI::ClearValue::DepthStencil(1.0f, 0)           // Clear depth to 1.0 (far)
            };
            cmd->beginRenderPass(renderPass, framebuffer, clearValues);

            // Set viewport and scissor using RHI command buffer
            RHI::Viewport viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
            cmd->setViewport(viewport);

            RHI::Scissor scissor{0, 0, width, height};
            cmd->setScissor(scissor);

            // Draw terrain chunks
            if (m_terrainTestPipeline && m_terrainDescriptorSet) {
                // Bind pipeline using RHI command buffer
                cmd->bindGraphicsPipeline(m_terrainTestPipeline.get());

                // Update UBO once per frame with camera matrices (no per-chunk data)
                struct CameraUBO {
                    glm::mat4 view;
                    glm::mat4 projection;
                    glm::mat4 viewProjection;
                };

                glm::mat4 vulkanProj = camera.projection;
                vulkanProj[1][1] *= -1.0f;  // Flip Y for Vulkan

                // Convert OpenGL depth range [-1,1] to Vulkan [0,1]
                // This remaps the Z component: vulkan_z = (opengl_z + 1) / 2
                // Applied as: new_proj = depthRemap * old_proj
                glm::mat4 depthRemap(1.0f);
                depthRemap[2][2] = 0.5f;  // Scale Z by 0.5
                depthRemap[3][2] = 0.5f;  // Offset Z by 0.5
                vulkanProj = depthRemap * vulkanProj;

                CameraUBO uboData;
                uboData.view = camera.view;
                uboData.projection = vulkanProj;
                uboData.viewProjection = vulkanProj * camera.view;

                void* mappedUBO = m_testCameraUBO->map();
                if (mappedUBO) {
                    memcpy(mappedUBO, &uboData, sizeof(CameraUBO));
                    m_testCameraUBO->unmap();
                }

                // Bind descriptor set once (camera UBO is shared)
                // TODO: Convert to RHI descriptor set when fully abstracted
                auto* vkCmd = static_cast<RHI::VKCommandBuffer*>(cmd);
                auto* vkPipeline = static_cast<RHI::VKGraphicsPipeline*>(m_terrainTestPipeline.get());
                vkCmdBindDescriptorSets(vkCmd->getVkCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        vkPipeline->getVkLayout(), 0, 1, &m_terrainDescriptorSet, 0, nullptr);

                int chunksDrawn = 0;
                int totalVertices = 0;
                int totalMeshes = static_cast<int>(world.meshes.size());

                // Debug: log mesh count periodically
                static int debugCounter = 0;
                if (debugCounter++ % 300 == 0) {
                    std::cout << "[RHI] Iterating " << totalMeshes << " meshes, VBO cache size: " << m_chunkVBOCache.size() << std::endl;
                }

                // Iterate through all chunks in the world
                for (auto& [chunkPos, mesh] : world.meshes) {
                    if (!mesh) continue;

                    // For each sub-chunk in the column
                    for (int subY = 0; subY < 16; subY++) {
                        auto& subChunk = mesh->subChunks[subY];
                        if (subChunk.isEmpty || subChunk.cachedVertices.empty()) continue;

                        // Calculate chunk world offset
                        glm::vec4 chunkOffset(
                            static_cast<float>(chunkPos.x * 16),
                            static_cast<float>(subY * 16),
                            static_cast<float>(chunkPos.y * 16),
                            0.0f
                        );

                        // Push chunk offset using RHI command buffer
                        cmd->pushConstants(RHI::ShaderStage::Vertex, 0, sizeof(glm::vec4), &chunkOffset);

                        // Use cached VBO or create new one
                        ChunkVBOKey key{chunkPos.x, chunkPos.y, subY};
                        size_t vertexDataSize = subChunk.cachedVertices.size() * sizeof(PackedChunkVertex);
                        size_t dataHash = subChunk.cachedVertices.size();  // Simple hash based on vertex count

                        // Throttle VBO uploads per frame to avoid GPU stalls
                        static int vboUploadsThisFrame = 0;
                        static uint64_t lastFrameNumber = 0;
                        if (m_frameNumber != lastFrameNumber) {
                            vboUploadsThisFrame = 0;
                            lastFrameNumber = m_frameNumber;
                        }

                        auto it = m_chunkVBOCache.find(key);
                        bool needsUpload = (it == m_chunkVBOCache.end() || it->second.dataHash != dataHash);

                        if (needsUpload) {
                            const int MAX_VBO_UPLOADS_PER_FRAME = 16;
                            if (vboUploadsThisFrame >= MAX_VBO_UPLOADS_PER_FRAME) {
                                // Skip this chunk for now, will be uploaded next frame
                                continue;
                            }

                            // Queue old VBO for deletion (if exists) - don't delete immediately as GPU may still be using it
                            if (it != m_chunkVBOCache.end() && it->second.buffer) {
                                PendingDeletion pending;
                                pending.buffer = std::move(it->second.buffer);
                                pending.frameQueued = m_frameNumber;
                                m_pendingVBODeletions.push_back(std::move(pending));
                            }

                            // Create new VBO
                            RHI::BufferDesc vboDesc{};
                            vboDesc.size = vertexDataSize;
                            vboDesc.usage = RHI::BufferUsage::Vertex;
                            vboDesc.memory = RHI::MemoryUsage::CpuToGpu;
                            vboDesc.debugName = "ChunkVBO";

                            auto newVBO = m_device->createBuffer(vboDesc);
                            if (!newVBO) continue;

                            newVBO->uploadData(subChunk.cachedVertices.data(), vertexDataSize);

                            CachedVBO cached;
                            cached.buffer = std::move(newVBO);
                            cached.vertexCount = static_cast<uint32_t>(subChunk.cachedVertices.size());
                            cached.dataHash = dataHash;

                            m_chunkVBOCache[key] = std::move(cached);
                            it = m_chunkVBOCache.find(key);
                            vboUploadsThisFrame++;
                        }

                        // Bind vertex buffer and draw (only if VBO is ready)
                        if (it != m_chunkVBOCache.end() && it->second.buffer) {
                            cmd->bindVertexBuffer(0, it->second.buffer.get(), 0);
                            cmd->draw(it->second.vertexCount, 1, 0, 0);
                            chunksDrawn++;
                            totalVertices += it->second.vertexCount;
                        }
                    }
                }

                if (firstFrame) {
                    std::cout << "[RHI] Vulkan terrain rendering: " << chunksDrawn << " chunks, " << totalVertices << " vertices" << std::endl;
                }
            } else if (firstFrame) {
                LOG_DEBUG("RHI", "Vulkan: Terrain pipeline or descriptor set not ready");
            }

            // End render pass - BUT if menu mode is enabled, keep it open for UI drawing
            if (!m_menuMode) {
                cmd->endRenderPass();
            } else {
                // Keep render pass open - UI will be drawn, then endUIOverlay() closes it
                static bool menuModeLog = true;
                if (menuModeLog) {
                    std::cout << "[RHI] Menu mode: keeping render pass open for UI" << std::endl;
                    menuModeLog = false;
                }
            }
        } else if (firstFrame) {
            LOG_ERROR("RHI", "Vulkan: renderPass or framebuffer is NULL!");
        }

        firstFrame = false;
        return;  // Skip other render passes for now
    }
#endif // DISABLE_VULKAN

    // Setup render context
    m_context.world = &world;
    m_context.camera = &camera;
    m_context.lighting = &m_lighting;
    m_context.fog = &m_fog;
    m_context.config = &m_config;
    m_context.frameNumber = m_frameNumber;

    // Reset stats for this frame
    m_context.stats = RenderStats{};

    if (firstFrame) LOG_DEBUG("RHI", "Context setup complete");

    // Execute render passes

    // 1. Shadow Pass
    if (m_config.enableShadows) {
        if (firstFrame) LOG_DEBUG("RHI", "Starting Shadow Pass");
        if (!m_shadowPass) { LOG_ERROR("RHI", "m_shadowPass is null!"); return; }
        m_shadowPass->execute(cmd, m_context);
        if (firstFrame) LOG_DEBUG("RHI", "Shadow Pass complete");
    }

    // 2. G-Buffer Pass - use split begin/end for hybrid World rendering
    if (firstFrame) LOG_DEBUG("RHI", "Starting G-Buffer Pass");
    if (!m_gBufferPass) { LOG_ERROR("RHI", "m_gBufferPass is null!"); return; }
    m_gBufferPass->beginPass(cmd, m_context);

    // DEBUG: Check if shader is bound after beginPass
    if (firstFrame) {
        GLint currentProg = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &currentProg);
        std::cout << "[DEBUG] After G-Buffer beginPass: GL_CURRENT_PROGRAM = " << currentProg << std::endl;

        GLint currentFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFBO);
        std::cout << "[DEBUG] After G-Buffer beginPass: GL_FRAMEBUFFER_BINDING = " << currentFBO << std::endl;

        std::cout << "[DEBUG] Texture atlas ID = " << m_context.textureAtlas << std::endl;
    }

    // Render world geometry into G-Buffer (hybrid path - uses OpenGL)
    // The framebuffer is bound by beginPass, so World's GL calls render to RHI G-Buffer
    if (m_worldRenderer && m_context.world) {
        if (firstFrame) LOG_DEBUG("RHI", "Starting WorldRenderer::renderSolid");
        WorldRenderParams params;
        params.cameraPosition = camera.position;
        params.viewProjection = camera.viewProjection;
        params.mode = m_config.enableGPUCulling ? WorldRenderMode::GPUCulled : WorldRenderMode::Batched;
        params.renderWater = false;  // Water rendered in separate pass

        m_worldRenderer->renderSolid(cmd, *m_context.world, params);

        if (firstFrame) {
            LOG_DEBUG("RHI", "WorldRenderer::renderSolid complete");
            std::cout << "[DEBUG] Chunks rendered: " << m_worldRenderer->getRenderedSubChunks()
                      << ", culled: " << m_worldRenderer->getCulledSubChunks() << std::endl;
        }

        // Update stats from world renderer
        m_context.stats.chunksRendered = m_worldRenderer->getRenderedSubChunks();
        m_context.stats.chunksCulled = m_worldRenderer->getCulledSubChunks();
    } else {
        if (firstFrame) {
            std::cout << "[DEBUG] WorldRenderer or world is null! worldRenderer="
                      << (void*)m_worldRenderer.get() << ", world=" << (void*)m_context.world << std::endl;
        }
    }

    // End G-Buffer pass and store texture handles
    m_gBufferPass->endPass(cmd);
    m_gBufferPass->storeTextureHandles(m_context);
    if (firstFrame) LOG_DEBUG("RHI", "G-Buffer Pass complete");

    // 3. Hi-Z Pass (for occlusion culling)
    if (m_config.enableHiZCulling) {
        if (firstFrame) LOG_DEBUG("RHI", "Starting Hi-Z Pass");
        if (!m_hiZPass) { LOG_ERROR("RHI", "m_hiZPass is null!"); return; }
        m_hiZPass->execute(cmd, m_context);
        if (firstFrame) LOG_DEBUG("RHI", "Hi-Z Pass complete");
    }

    // 4. GPU Culling Pass
    if (m_config.enableGPUCulling) {
        if (firstFrame) LOG_DEBUG("RHI", "Starting GPU Culling Pass");
        if (!m_gpuCullingPass) { LOG_ERROR("RHI", "m_gpuCullingPass is null!"); return; }
        m_gpuCullingPass->execute(cmd, m_context);
        if (firstFrame) LOG_DEBUG("RHI", "GPU Culling Pass complete");
    }

    // 5. SSAO Pass
    if (m_config.enableSSAO) {
        if (firstFrame) LOG_DEBUG("RHI", "Starting SSAO Pass");
        if (!m_ssaoPass) { LOG_ERROR("RHI", "m_ssaoPass is null!"); return; }
        m_ssaoPass->execute(cmd, m_context);
        if (firstFrame) LOG_DEBUG("RHI", "SSAO Pass complete");
    }

    // 6. Composite Pass (lighting calculation)
    if (firstFrame) LOG_DEBUG("RHI", "Starting Composite Pass");
    if (!m_compositePass) { LOG_ERROR("RHI", "m_compositePass is null!"); return; }
    m_compositePass->execute(cmd, m_context);
    if (firstFrame) LOG_DEBUG("RHI", "Composite Pass complete");

    // 7. Sky Pass (rendered into composite output)
    if (firstFrame) LOG_DEBUG("RHI", "Starting Sky Pass");
    if (!m_skyPass) { LOG_ERROR("RHI", "m_skyPass is null!"); return; }
    m_skyPass->execute(cmd, m_context);
    if (firstFrame) LOG_DEBUG("RHI", "Sky Pass complete");

    // 8. Water Pass (forward rendered, semi-transparent)
    if (firstFrame) LOG_DEBUG("RHI", "Starting Water Pass");
    if (!m_waterPass) { LOG_ERROR("RHI", "m_waterPass is null!"); return; }
    m_waterPass->execute(cmd, m_context);
    if (firstFrame) LOG_DEBUG("RHI", "Water Pass complete");

    // 9. Precipitation Pass (rain/snow particles)
    if (firstFrame) LOG_DEBUG("RHI", "Starting Precipitation Pass");
    if (!m_precipitationPass) { LOG_ERROR("RHI", "m_precipitationPass is null!"); return; }
    m_precipitationPass->execute(cmd, m_context);
    if (firstFrame) LOG_DEBUG("RHI", "Precipitation Pass complete");

    // 10. Bloom Pass (optional glow effect)
    if (m_config.enableBloom) {
        if (firstFrame) LOG_DEBUG("RHI", "Starting Bloom Pass");
        if (!m_bloomPass) { LOG_ERROR("RHI", "m_bloomPass is null!"); return; }
        m_bloomPass->execute(cmd, m_context);
        if (firstFrame) LOG_DEBUG("RHI", "Bloom Pass complete");
    }

    // 11. FSR Upscaling Pass
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
#ifndef DISABLE_VULKAN
    else if (finalOutput && m_device->getBackend() == RHI::Backend::Vulkan) {
        // Vulkan: Blit the final output to the swapchain image
        static bool firstBlit = true;
        if (firstBlit) std::cout << "[Vulkan Blit] Starting blit to swapchain" << std::endl;

        auto* vkSwapchain = static_cast<RHI::VKSwapchain*>(m_swapchain.get());
        auto* swapchainImage = vkSwapchain->getCurrentTexture();

        if (firstBlit) std::cout << "[Vulkan Blit] swapchainImage=" << (void*)swapchainImage << std::endl;

        if (swapchainImage) {
            auto* vkCmd = static_cast<RHI::VKCommandBuffer*>(cmd);
            auto* vkSrc = static_cast<RHI::VKTexture*>(finalOutput);
            auto* vkDst = static_cast<RHI::VKTexture*>(swapchainImage);
            VkCommandBuffer vkCmdBuffer = vkCmd->getVkCommandBuffer();

            if (firstBlit) {
                std::cout << "[Vulkan Blit] vkSrc image=" << (void*)vkSrc->getVkImage() << std::endl;
                std::cout << "[Vulkan Blit] vkDst image=" << (void*)vkDst->getVkImage() << std::endl;
                std::cout << "[Vulkan Blit] srcRect=" << m_renderWidth << "x" << m_renderHeight << std::endl;
                std::cout << "[Vulkan Blit] dstRect=" << m_displayWidth << "x" << m_displayHeight << std::endl;
            }

            // Clear swapchain with appropriate background color
            vkDst->transitionLayout(vkCmdBuffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            // Use menu clear color (dark gray for menus, or sky color eventually for game)
            VkClearColorValue clearColor = {{m_menuClearColor.r, m_menuClearColor.g, m_menuClearColor.b, m_menuClearColor.a}};
            VkImageSubresourceRange range{};
            range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            range.baseMipLevel = 0;
            range.levelCount = 1;
            range.baseArrayLayer = 0;
            range.layerCount = 1;
            vkCmdClearColorImage(vkCmdBuffer, vkDst->getVkImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);

            // Transition swapchain to present layout
            vkDst->transitionLayout(vkCmdBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

            if (firstBlit) {
                std::cout << "[Vulkan Blit] Blit complete" << std::endl;
                firstBlit = false;
            }
        } else {
            if (firstBlit) std::cout << "[Vulkan Blit] ERROR: swapchainImage is null!" << std::endl;
        }
    }
#endif // DISABLE_VULKAN

    if (firstFrame) {
        LOG_DEBUG("RHI", "First frame render complete");
        firstFrame = false;
    }

    // Copy accumulated stats
    m_stats = m_context.stats;
}

void DeferredRendererRHI::endFrame() {
    static bool firstEndFrame = true;
    if (firstEndFrame) LOG_DEBUG("RHI", "endFrame starting");

    auto* cmd = m_commandBuffers[m_currentFrame].get();

    // End command buffer recording
    if (firstEndFrame) LOG_DEBUG("RHI", "Ending command buffer");
    cmd->end();

    // Submit command buffer
    if (firstEndFrame) LOG_DEBUG("RHI", "Submitting command buffer");
#ifndef DISABLE_VULKAN
    if (m_device->getBackend() == RHI::Backend::Vulkan) {
        // Vulkan: Submit with synchronization to coordinate with swapchain
        auto* vkSwapchain = static_cast<RHI::VKSwapchain*>(m_swapchain.get());
        auto* vkQueue = static_cast<RHI::VKQueue*>(m_device->getGraphicsQueue());
        vkQueue->submitWithSync({cmd},
            vkSwapchain->getImageAvailableSemaphore(),
            vkSwapchain->getRenderFinishedSemaphore(),
            vkSwapchain->getInFlightFence());
    } else
#endif // DISABLE_VULKAN
    {
        m_device->getGraphicsQueue()->submit({cmd});
    }

    // Present
    if (firstEndFrame) LOG_DEBUG("RHI", "Presenting swapchain");
    if (!m_swapchain->present()) {
        // Swapchain out of date
        int width, height;
        glfwGetFramebufferSize(m_window, &width, &height);
        resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    }

    // Advance frame
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

    // Process pending VBO deletions - delete VBOs that are at least 3 frames old
    // This ensures the GPU has finished using them (with 2 frames in flight)
    uint64_t currentFrame = m_frameNumber;
    m_pendingVBODeletions.erase(
        std::remove_if(m_pendingVBODeletions.begin(), m_pendingVBODeletions.end(),
            [currentFrame](const PendingDeletion& p) {
                return (currentFrame - p.frameQueued) >= 3;
            }),
        m_pendingVBODeletions.end()
    );

    if (firstEndFrame) {
        LOG_DEBUG("RHI", "endFrame complete");
        firstEndFrame = false;
    }
}

void DeferredRendererRHI::beginUIOverlay() {
    static bool firstBegin = true;
    if (firstBegin) {
        std::cout << "[UI] beginUIOverlay called (no-op for Vulkan - UI drawn in main pass)" << std::endl;
        firstBegin = false;
    }

    if (m_uiOverlayActive) return;
    if (!m_device || !m_swapchain) return;

    // Reset text VBO offset for new frame
    m_textVBOOffset = 0;

    // For Vulkan, UI is drawn within the main render pass (see render())
    // So beginUIOverlay is now a no-op - we just set the flag
    if (m_device->getBackend() == RHI::Backend::Vulkan) {
        // No render pass to begin - UI will be drawn in the main render pass
        // The main render pass is kept open by setting m_uiOverlayActive = true
        // and render() checks this flag before calling endRenderPass
    } else {
        // OpenGL: Just set up 2D rendering state
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, m_displayWidth, m_displayHeight);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    m_uiOverlayActive = true;
}

void DeferredRendererRHI::endUIOverlay() {
    if (!m_uiOverlayActive) return;

    auto* cmd = m_commandBuffers[m_currentFrame].get();

    if (m_device->getBackend() == RHI::Backend::Vulkan) {
        // End the main render pass (which was kept open for UI drawing)
        cmd->endRenderPass();

        static bool endLog = true;
        if (endLog) {
            std::cout << "[UI] endUIOverlay: render pass ended" << std::endl;
            endLog = false;
        }
    } else {
        // OpenGL: Restore state
        glEnable(GL_DEPTH_TEST);
    }

    m_uiOverlayActive = false;
}

void DeferredRendererRHI::drawUIRect(float x, float y, float w, float h, const glm::vec4& color) {
#ifdef DISABLE_VULKAN
    // WIP: Vulkan UI not available - use OpenGL menu rendering system instead
    (void)x; (void)y; (void)w; (void)h; (void)color;
    return;
#else
    static bool firstCall = true;
    if (firstCall) {
        std::cout << "[UI] drawUIRect called: overlay=" << m_uiOverlayActive
                  << ", initialized=" << m_uiResourcesInitialized << std::endl;
        firstCall = false;
    }

    if (!m_uiOverlayActive) return;

    auto* cmd = m_commandBuffers[m_currentFrame].get();
    if (!cmd) return;

    // Initialize UI resources on first use
    if (!m_uiResourcesInitialized) {
        // Create UI shader with screen-space positioning via push constants
        const char* uiVertShader = R"(
            #version 450
            layout(push_constant) uniform PushConstants {
                vec4 rect;       // x, y, width, height in pixels
                vec4 screenSize; // screenWidth, screenHeight, 0, 0
                vec4 color;
            } pc;
            layout(location = 0) in vec2 aPos;
            void main() {
                // Unit quad vertices based on gl_VertexIndex (0-5 for two triangles)
                vec2 quadPos[6] = vec2[](
                    vec2(0.0, 0.0),
                    vec2(1.0, 0.0),
                    vec2(1.0, 1.0),
                    vec2(0.0, 0.0),
                    vec2(1.0, 1.0),
                    vec2(0.0, 1.0)
                );
                vec2 uv = quadPos[gl_VertexIndex];

                // Convert to screen pixels
                vec2 pixelPos = pc.rect.xy + uv * pc.rect.zw;

                // Convert to NDC: x from 0..width to -1..+1, y from 0..height to -1..+1
                vec2 ndc;
                ndc.x = (pixelPos.x / pc.screenSize.x) * 2.0 - 1.0;
                ndc.y = (pixelPos.y / pc.screenSize.y) * 2.0 - 1.0;

                gl_Position = vec4(ndc, 0.0, 1.0);
            }
        )";

        const char* uiFragShader = R"(
            #version 450
            layout(push_constant) uniform PushConstants {
                vec4 rect;
                vec4 screenSize;
                vec4 color;
            } pc;
            layout(location = 0) out vec4 FragColor;
            void main() {
                FragColor = pc.color;
            }
        )";

        RHI::ShaderProgramDesc shaderDesc;
        shaderDesc.debugName = "UI_Rect";
        shaderDesc.stages.push_back(RHI::ShaderSource::fromGLSL(RHI::ShaderStage::Vertex, uiVertShader));
        shaderDesc.stages.push_back(RHI::ShaderSource::fromGLSL(RHI::ShaderStage::Fragment, uiFragShader));

        m_uiShader = m_device->createShaderProgram(shaderDesc);
        if (!m_uiShader) {
            std::cerr << "[UI] Failed to create UI shader" << std::endl;
            return;
        }

        // Create quad VBO (unit quad)
        float quadVerts[] = {
            0.0f, 0.0f,
            1.0f, 0.0f,
            1.0f, 1.0f,
            0.0f, 0.0f,
            1.0f, 1.0f,
            0.0f, 1.0f
        };

        RHI::BufferDesc vboDesc;
        vboDesc.size = sizeof(quadVerts);
        vboDesc.usage = RHI::BufferUsage::Vertex;
        vboDesc.memory = RHI::MemoryUsage::GpuOnly;
        vboDesc.debugName = "UI_QuadVBO";

        m_uiQuadVBO = m_device->createBuffer(vboDesc);
        if (m_uiQuadVBO) {
            m_uiQuadVBO->uploadData(quadVerts, sizeof(quadVerts));
        }

        // Create UI pipeline using swapchain render pass (compatible with main rendering)
        auto* vkSwapchain = static_cast<RHI::VKSwapchain*>(m_swapchain.get());
        auto* renderPass = vkSwapchain->getSwapchainRenderPass();

        if (renderPass && m_uiShader && m_uiQuadVBO) {
            // Create pipeline layout with push constants (rect + screenSize + color = 48 bytes)
            auto* vkDevice = static_cast<RHI::VKDevice*>(m_device.get());
            VkDevice device = vkDevice->getDevice();

            VkPushConstantRange pushRange{};
            pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            pushRange.offset = 0;
            pushRange.size = sizeof(glm::vec4) * 3;  // 48 bytes

            VkPipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layoutInfo.setLayoutCount = 0;
            layoutInfo.pSetLayouts = nullptr;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;

            if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_uiPipelineLayout) != VK_SUCCESS) {
                std::cerr << "[UI] Failed to create UI pipeline layout" << std::endl;
                return;
            }
            std::cout << "[UI] Created pipeline layout (48 bytes)" << std::endl;

            RHI::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.shaderProgram = m_uiShader.get();
            pipelineDesc.renderPass = renderPass;
            pipelineDesc.debugName = "UI_RectPipeline";
            pipelineDesc.nativePipelineLayout = m_uiPipelineLayout;  // Use our custom layout

            RHI::VertexBinding binding;
            binding.binding = 0;
            binding.stride = sizeof(float) * 2;
            binding.inputRate = RHI::VertexInputRate::Vertex;
            pipelineDesc.vertexInput.bindings.push_back(binding);

            RHI::VertexAttribute posAttr;
            posAttr.location = 0;
            posAttr.binding = 0;
            posAttr.format = RHI::Format::RG32_FLOAT;
            posAttr.offset = 0;
            pipelineDesc.vertexInput.attributes.push_back(posAttr);

            pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

            RHI::RasterizerState raster;
            raster.cullMode = RHI::CullMode::None;
            raster.polygonMode = RHI::PolygonMode::Fill;
            pipelineDesc.rasterizer = raster;

            RHI::DepthStencilState depth;
            depth.depthTestEnable = false;
            depth.depthWriteEnable = false;
            pipelineDesc.depthStencil = depth;

            RHI::BlendState blend;
            blend.enable = true;
            blend.srcColorFactor = RHI::BlendFactor::SrcAlpha;
            blend.dstColorFactor = RHI::BlendFactor::OneMinusSrcAlpha;
            blend.colorOp = RHI::BlendOp::Add;
            blend.srcAlphaFactor = RHI::BlendFactor::One;
            blend.dstAlphaFactor = RHI::BlendFactor::OneMinusSrcAlpha;
            blend.alphaOp = RHI::BlendOp::Add;
            pipelineDesc.colorBlendStates.push_back(blend);

            pipelineDesc.dynamicViewport = true;
            pipelineDesc.dynamicScissor = true;

            m_uiPipeline = m_device->createGraphicsPipeline(pipelineDesc);
            if (m_uiPipeline) {
                std::cout << "[UI] UI pipeline created successfully with push constants" << std::endl;
                m_uiResourcesInitialized = true;
            } else {
                std::cerr << "[UI] Failed to create UI pipeline" << std::endl;
            }
        }

        // Calculate UI projection matrix for Vulkan (origin at top-left)
        // Vulkan has Y pointing down and Z from 0 to 1
        // Using glm::orthoLH_ZO for left-handed, zero-to-one depth
        m_uiProjection = glm::orthoLH_ZO(0.0f, static_cast<float>(m_displayWidth),
                                          static_cast<float>(m_displayHeight), 0.0f,
                                          0.0f, 1.0f);

        std::cout << "[UI] Projection matrix for " << m_displayWidth << "x" << m_displayHeight << std::endl;
    }

    if (!m_uiResourcesInitialized || !m_uiPipeline || !m_uiQuadVBO) {
        static bool warnOnce = true;
        if (warnOnce) {
            std::cout << "[UI] Resources not ready: init=" << m_uiResourcesInitialized
                      << ", pipeline=" << (m_uiPipeline ? "yes" : "no")
                      << ", vbo=" << (m_uiQuadVBO ? "yes" : "no") << std::endl;
            warnOnce = false;
        }
        return;
    }

    // Set viewport and scissor for UI (ensure correct state after terrain rendering)
    RHI::Viewport uiViewport{0.0f, 0.0f, static_cast<float>(m_displayWidth), static_cast<float>(m_displayHeight), 0.0f, 1.0f};
    cmd->setViewport(uiViewport);
    RHI::Scissor uiScissor{0, 0, m_displayWidth, m_displayHeight};
    cmd->setScissor(uiScissor);

    // Bind pipeline and VBO
    cmd->bindGraphicsPipeline(m_uiPipeline.get());
    cmd->bindVertexBuffer(0, m_uiQuadVBO.get());

    // Push constants: rect, screenSize, color
    struct UIPushConstants {
        glm::vec4 rect;       // x, y, w, h
        glm::vec4 screenSize; // width, height, 0, 0
        glm::vec4 color;
    };
    UIPushConstants pc;
    pc.rect = glm::vec4(x, y, w, h);
    pc.screenSize = glm::vec4(static_cast<float>(m_displayWidth), static_cast<float>(m_displayHeight), 0.0f, 0.0f);
    pc.color = color;

    auto* vkCmd = static_cast<RHI::VKCommandBuffer*>(cmd);
    VkCommandBuffer vkCmdBuf = vkCmd->getVkCommandBuffer();
    vkCmdPushConstants(vkCmdBuf, m_uiPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(UIPushConstants), &pc);

    static int drawCount = 0;
    if (drawCount < 5) {
        std::cout << "[UI] Drawing rect at (" << x << "," << y << ") size (" << w << "x" << h
                  << ") color=(" << color.r << "," << color.g << "," << color.b << "," << color.a << ")" << std::endl;
        drawCount++;
    }

    cmd->draw(6);
#endif // DISABLE_VULKAN
}

void DeferredRendererRHI::drawUIText(const std::string& text, float x, float y, const glm::vec4& color, float scale) {
#ifdef DISABLE_VULKAN
    // WIP: Vulkan UI not available - use OpenGL menu rendering system instead
    (void)text; (void)x; (void)y; (void)color; (void)scale;
    return;
#else
    if (!m_uiOverlayActive || text.empty()) return;

    auto* cmd = m_commandBuffers[m_currentFrame].get();
    if (!cmd) return;

    // Initialize text resources on first use
    if (!m_uiTextResourcesInitialized) {
        // Create text shader that reads vertex positions from VBO
        const char* textVertShader = R"(
            #version 450
            layout(push_constant) uniform PushConstants {
                vec4 positionScale;  // x, y, scaleX, scaleY
                vec4 screenSize;     // screenWidth, screenHeight, 0, 0
                vec4 color;
            } pc;
            layout(location = 0) in vec2 aPos;
            void main() {
                // Apply scale then offset
                vec2 pixelPos = aPos * pc.positionScale.zw + pc.positionScale.xy;
                // Convert to NDC
                vec2 ndc;
                ndc.x = (pixelPos.x / pc.screenSize.x) * 2.0 - 1.0;
                ndc.y = (pixelPos.y / pc.screenSize.y) * 2.0 - 1.0;
                gl_Position = vec4(ndc, 0.0, 1.0);
            }
        )";

        const char* textFragShader = R"(
            #version 450
            layout(push_constant) uniform PushConstants {
                vec4 positionScale;
                vec4 screenSize;
                vec4 color;
            } pc;
            layout(location = 0) out vec4 FragColor;
            void main() {
                FragColor = pc.color;
            }
        )";

        RHI::ShaderProgramDesc shaderDesc;
        shaderDesc.debugName = "UI_Text";
        shaderDesc.stages.push_back(RHI::ShaderSource::fromGLSL(RHI::ShaderStage::Vertex, textVertShader));
        shaderDesc.stages.push_back(RHI::ShaderSource::fromGLSL(RHI::ShaderStage::Fragment, textFragShader));

        m_uiTextShader = m_device->createShaderProgram(shaderDesc);
        if (!m_uiTextShader) {
            std::cerr << "[UI] Failed to create text shader" << std::endl;
            return;
        }

        // Create dynamic text VBO
        RHI::BufferDesc vboDesc;
        vboDesc.size = TEXT_VBO_SIZE;
        vboDesc.usage = RHI::BufferUsage::Vertex;
        vboDesc.memory = RHI::MemoryUsage::CpuToGpu;  // Dynamic updates
        vboDesc.debugName = "UI_TextVBO";

        m_uiTextVBO = m_device->createBuffer(vboDesc);
        if (!m_uiTextVBO) {
            std::cerr << "[UI] Failed to create text VBO" << std::endl;
            return;
        }

        // Create text pipeline
        auto* vkSwapchain = static_cast<RHI::VKSwapchain*>(m_swapchain.get());
        auto* renderPass = vkSwapchain->getSwapchainRenderPass();

        if (renderPass && m_uiTextShader && m_uiTextVBO) {
            // Create pipeline layout with push constants (48 bytes)
            auto* vkDevice = static_cast<RHI::VKDevice*>(m_device.get());
            VkDevice device = vkDevice->getDevice();

            VkPushConstantRange pushRange{};
            pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            pushRange.offset = 0;
            pushRange.size = sizeof(glm::vec4) * 3;  // 48 bytes

            VkPipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layoutInfo.setLayoutCount = 0;
            layoutInfo.pSetLayouts = nullptr;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;

            if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_uiTextPipelineLayout) != VK_SUCCESS) {
                std::cerr << "[UI] Failed to create text pipeline layout" << std::endl;
                return;
            }

            RHI::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.shaderProgram = m_uiTextShader.get();
            pipelineDesc.renderPass = renderPass;
            pipelineDesc.debugName = "UI_TextPipeline";
            pipelineDesc.nativePipelineLayout = m_uiTextPipelineLayout;

            RHI::VertexBinding binding;
            binding.binding = 0;
            binding.stride = sizeof(float) * 2;  // Just x, y
            binding.inputRate = RHI::VertexInputRate::Vertex;
            pipelineDesc.vertexInput.bindings.push_back(binding);

            RHI::VertexAttribute posAttr;
            posAttr.location = 0;
            posAttr.binding = 0;
            posAttr.format = RHI::Format::RG32_FLOAT;
            posAttr.offset = 0;
            pipelineDesc.vertexInput.attributes.push_back(posAttr);

            pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

            RHI::RasterizerState raster;
            raster.cullMode = RHI::CullMode::None;
            raster.polygonMode = RHI::PolygonMode::Fill;
            pipelineDesc.rasterizer = raster;

            RHI::DepthStencilState depth;
            depth.depthTestEnable = false;
            depth.depthWriteEnable = false;
            pipelineDesc.depthStencil = depth;

            RHI::BlendState blend;
            blend.enable = true;
            blend.srcColorFactor = RHI::BlendFactor::SrcAlpha;
            blend.dstColorFactor = RHI::BlendFactor::OneMinusSrcAlpha;
            blend.colorOp = RHI::BlendOp::Add;
            blend.srcAlphaFactor = RHI::BlendFactor::One;
            blend.dstAlphaFactor = RHI::BlendFactor::OneMinusSrcAlpha;
            blend.alphaOp = RHI::BlendOp::Add;
            pipelineDesc.colorBlendStates.push_back(blend);

            pipelineDesc.dynamicViewport = true;
            pipelineDesc.dynamicScissor = true;

            m_uiTextPipeline = m_device->createGraphicsPipeline(pipelineDesc);
            if (m_uiTextPipeline) {
                std::cout << "[UI] Text pipeline created successfully" << std::endl;
                m_uiTextResourcesInitialized = true;
            } else {
                std::cerr << "[UI] Failed to create text pipeline" << std::endl;
            }
        }
    }

    if (!m_uiTextResourcesInitialized || !m_uiTextPipeline || !m_uiTextVBO) {
        return;
    }

    // Generate text vertices using stb_easy_font
    static std::vector<float> vertexBuffer(60000);  // Raw stb output: 4 verts * 4 floats each per quad
    int numQuads = stb_easy_font_print(0, 0, const_cast<char*>(text.c_str()), nullptr,
                                        vertexBuffer.data(), static_cast<int>(vertexBuffer.size() * sizeof(float)));
    if (numQuads == 0) return;

    // Convert quads to triangles (6 vertices per quad, 2 floats per vertex)
    std::vector<float> triangleVerts;
    triangleVerts.reserve(numQuads * 6 * 2);

    float* ptr = vertexBuffer.data();
    for (int q = 0; q < numQuads; q++) {
        // stb_easy_font outputs quads as: v0(x,y,z,color), v1, v2, v3
        // Each vertex is 4 floats (x, y, z, color packed as float)
        float x0 = ptr[0], y0 = ptr[1];   // Vertex 0
        float x1 = ptr[4], y1 = ptr[5];   // Vertex 1
        float x2 = ptr[8], y2 = ptr[9];   // Vertex 2
        float x3 = ptr[12], y3 = ptr[13]; // Vertex 3

        // Triangle 1: 0, 1, 2
        triangleVerts.push_back(x0); triangleVerts.push_back(y0);
        triangleVerts.push_back(x1); triangleVerts.push_back(y1);
        triangleVerts.push_back(x2); triangleVerts.push_back(y2);

        // Triangle 2: 0, 2, 3
        triangleVerts.push_back(x0); triangleVerts.push_back(y0);
        triangleVerts.push_back(x2); triangleVerts.push_back(y2);
        triangleVerts.push_back(x3); triangleVerts.push_back(y3);

        ptr += 16;  // 4 vertices * 4 floats each
    }

    // Upload to VBO at current offset
    size_t uploadSize = triangleVerts.size() * sizeof(float);
    if (m_textVBOOffset + uploadSize > TEXT_VBO_SIZE) {
        std::cerr << "[UI] Text VBO overflow: offset=" << m_textVBOOffset
                  << " + size=" << uploadSize << " > " << TEXT_VBO_SIZE << std::endl;
        return;
    }

    size_t currentOffset = m_textVBOOffset;
    m_uiTextVBO->uploadData(triangleVerts.data(), uploadSize, currentOffset);
    m_textVBOOffset += uploadSize;  // Advance for next text string

    // Set viewport and scissor
    RHI::Viewport uiViewport{0.0f, 0.0f, static_cast<float>(m_displayWidth), static_cast<float>(m_displayHeight), 0.0f, 1.0f};
    cmd->setViewport(uiViewport);
    RHI::Scissor uiScissor{0, 0, m_displayWidth, m_displayHeight};
    cmd->setScissor(uiScissor);

    // Bind text pipeline and VBO with offset
    cmd->bindGraphicsPipeline(m_uiTextPipeline.get());
    cmd->bindVertexBuffer(0, m_uiTextVBO.get(), currentOffset);

    // Push constants: positionScale (x, y, scaleX, scaleY), screenSize, color
    // stb_easy_font uses ~6 pixels per char, scale 2.0 gives readable size like OpenGL
    float finalScale = scale * 2.0f;
    struct TextPushConstants {
        glm::vec4 positionScale;  // x, y, scaleX, scaleY
        glm::vec4 screenSize;     // width, height, 0, 0
        glm::vec4 color;
    };
    TextPushConstants pc;
    pc.positionScale = glm::vec4(x, y, finalScale, finalScale);
    pc.screenSize = glm::vec4(static_cast<float>(m_displayWidth), static_cast<float>(m_displayHeight), 0.0f, 0.0f);
    pc.color = color;

    auto* vkCmd = static_cast<RHI::VKCommandBuffer*>(cmd);
    VkCommandBuffer vkCmdBuf = vkCmd->getVkCommandBuffer();
    vkCmdPushConstants(vkCmdBuf, m_uiTextPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(TextPushConstants), &pc);

    // Draw text triangles
    cmd->draw(static_cast<uint32_t>(triangleVerts.size() / 2));
#endif // DISABLE_VULKAN
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

void DeferredRendererRHI::setTextureAtlas(uint32_t textureID) {
    m_context.textureAtlas = textureID;
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
    swapDesc.windowHandle = m_window;  // CRITICAL: Pass the GLFW window handle!

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

        std::cout << "[DeferredRendererRHI] Creating shader program: " << name << std::endl;
        auto program = m_device->createShaderProgram(progDesc);
        if (program) {
            m_shaderPrograms[name] = std::move(program);
            std::cout << "[DeferredRendererRHI] Shader program created: " << name << std::endl;
        } else {
            std::cerr << "[DeferredRendererRHI] Failed to create shader program: " << name << std::endl;
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

        std::cout << "[DeferredRendererRHI] Creating compute shader program: " << name << std::endl;
        auto program = m_device->createShaderProgram(progDesc);
        if (program) {
            m_shaderPrograms[name] = std::move(program);
            std::cout << "[DeferredRendererRHI] Compute shader program created: " << name << std::endl;
        } else {
            std::cerr << "[DeferredRendererRHI] Failed to create compute shader program: " << name << std::endl;
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

    // Create vertex input layout for chunk geometry (PackedChunkVertex = 16 bytes)
    // struct PackedChunkVertex {
    //     int16_t x, y, z;     // 6 bytes at offset 0
    //     uint16_t u, v;       // 4 bytes at offset 6
    //     uint8_t normalIndex, ao, light, texSlot; // 4 bytes at offset 10
    //     uint16_t padding;    // 2 bytes at offset 14 (implicit for alignment)
    // };
    RHI::VertexInputState chunkVertexInput{};
    chunkVertexInput.bindings = {{
        0,                              // binding
        16,                             // stride (PackedChunkVertex = 16 bytes)
        RHI::VertexInputRate::Vertex
    }};
    chunkVertexInput.attributes = {
        {0, 0, RHI::Format::RGB16_SINT, 0},      // position (int16 x3) at offset 0
        {1, 0, RHI::Format::RG16_UINT, 6},       // texcoord (uint16 x2) at offset 6
        {2, 0, RHI::Format::RGBA8_UINT, 10}      // packed data (uint8 x4) at offset 10
    };

    std::cout << "[DeferredRendererRHI] Creating pipelines..." << std::endl;

    // Create G-Buffer pipeline
    std::cout << "[DeferredRendererRHI] Creating G-Buffer pipeline..." << std::endl;
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

        std::cout << "[DeferredRendererRHI] Calling createGraphicsPipeline for G-Buffer..." << std::endl;
        m_gBufferPipeline = m_device->createGraphicsPipeline(gbufferPipeDesc);
        if (m_gBufferPipeline) {
            std::cout << "[DeferredRendererRHI] G-Buffer pipeline created successfully" << std::endl;
            m_gBufferPass->setPipeline(m_gBufferPipeline.get());
        } else {
            std::cerr << "[DeferredRendererRHI] Failed to create G-Buffer pipeline" << std::endl;
        }
    } else {
        std::cerr << "[DeferredRendererRHI] Skipping G-Buffer pipeline (null prog or renderpass)" << std::endl;
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

    // WIP: Vulkan test pipeline and terrain resources (disabled while focusing on OpenGL)
#ifndef DISABLE_VULKAN
    // Create Vulkan test pipeline (simple fullscreen triangle)
    if (isVulkanBackend) {
        std::cout << "[DeferredRendererRHI] Creating Vulkan test pipeline..." << std::endl;

        // Simple vertex shader - generates fullscreen triangle without vertex buffer
        const char* testVertGLSL = R"(
#version 460

void main() {
    // Generate fullscreen triangle vertices
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
)";

        // Simple fragment shader - outputs solid green
        const char* testFragGLSL = R"(
#version 460

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(0.0, 1.0, 0.0, 1.0);  // Green
}
)";

        // Compile shaders
        auto testVertShader = m_shaderCompiler.compile(testVertGLSL, ShaderStage::Vertex, options, "test_vert");
        auto testFragShader = m_shaderCompiler.compile(testFragGLSL, ShaderStage::Fragment, options, "test_frag");

        if (testVertShader && testFragShader) {
            auto convertSpirvToBytes = [](const std::vector<uint32_t>& spirv) -> std::vector<uint8_t> {
                std::vector<uint8_t> bytes(spirv.size() * sizeof(uint32_t));
                memcpy(bytes.data(), spirv.data(), bytes.size());
                return bytes;
            };

            RHI::ShaderProgramDesc testProgDesc{};
            RHI::ShaderSource vertSrc;
            vertSrc.stage = RHI::ShaderStage::Vertex;
            vertSrc.type = RHI::ShaderSourceType::SPIRV;
            vertSrc.spirv = convertSpirvToBytes(testVertShader->spirv);
            vertSrc.entryPoint = "main";

            RHI::ShaderSource fragSrc;
            fragSrc.stage = RHI::ShaderStage::Fragment;
            fragSrc.type = RHI::ShaderSourceType::SPIRV;
            fragSrc.spirv = convertSpirvToBytes(testFragShader->spirv);
            fragSrc.entryPoint = "main";

            testProgDesc.stages = {vertSrc, fragSrc};
            testProgDesc.debugName = "TestTriangle";

            m_testShaderProgram = m_device->createShaderProgram(testProgDesc);

            if (m_testShaderProgram) {
                std::cout << "[DeferredRendererRHI] Test shader program created" << std::endl;

                // Get swapchain render pass
                auto* vkSwapchain = static_cast<RHI::VKSwapchain*>(m_swapchain.get());
                VkRenderPass swapchainRenderPass = vkSwapchain->getRenderPass();

                // Create pipeline - no vertex buffer needed (vertices generated in shader)
                RHI::VertexInputState emptyVertexInput{};

                RHI::GraphicsPipelineDesc testPipeDesc{};
                testPipeDesc.shaderProgram = m_testShaderProgram.get();
                testPipeDesc.vertexInput = emptyVertexInput;
                testPipeDesc.rasterizer.cullMode = RHI::CullMode::None;
                testPipeDesc.rasterizer.frontFace = RHI::FrontFace::CounterClockwise;
                testPipeDesc.depthStencil.depthTestEnable = false;
                testPipeDesc.depthStencil.depthWriteEnable = false;
                testPipeDesc.colorBlendStates = {{false}};
                testPipeDesc.nativeRenderPass = swapchainRenderPass;  // Use native handle
                testPipeDesc.debugName = "TestTrianglePipeline";

                m_testPipeline = m_device->createGraphicsPipeline(testPipeDesc);
                if (m_testPipeline) {
                    std::cout << "[DeferredRendererRHI] Test pipeline created successfully" << std::endl;
                } else {
                    std::cerr << "[DeferredRendererRHI] Failed to create test pipeline" << std::endl;
                }
            } else {
                std::cerr << "[DeferredRendererRHI] Failed to create test shader program" << std::endl;
            }
        } else {
            std::cerr << "[DeferredRendererRHI] Failed to compile test shaders: " << m_shaderCompiler.getLastError() << std::endl;
        }

        // Create terrain test resources
        std::cout << "[DeferredRendererRHI] Creating terrain test resources..." << std::endl;

        // 1. Create cube vertex buffer
        // Simple vertex format: position (vec3) + color (vec3) = 24 bytes per vertex
        struct SimpleVertex {
            float x, y, z;      // position
            float r, g, b;      // color
        };

        // Cube vertices (36 vertices for 12 triangles, 6 faces)
        // 16x16x16 cube centered at origin (0, 0, 0)
        const float S = 8.0f;   // Half-size (cube is 16 units across)
        std::vector<SimpleVertex> cubeVertices = {
            // Front face (z = +S) - BRIGHT RED (facing camera at z=32)
            {-S, -S,  S,  1.0f, 0.0f, 0.0f}, { S, -S,  S,  1.0f, 0.0f, 0.0f}, { S,  S,  S,  1.0f, 0.0f, 0.0f},
            {-S, -S,  S,  1.0f, 0.0f, 0.0f}, { S,  S,  S,  1.0f, 0.0f, 0.0f}, {-S,  S,  S,  1.0f, 0.0f, 0.0f},
            // Back face (z = -S) - BRIGHT GREEN
            { S, -S, -S,  0.0f, 1.0f, 0.0f}, {-S, -S, -S,  0.0f, 1.0f, 0.0f}, {-S,  S, -S,  0.0f, 1.0f, 0.0f},
            { S, -S, -S,  0.0f, 1.0f, 0.0f}, {-S,  S, -S,  0.0f, 1.0f, 0.0f}, { S,  S, -S,  0.0f, 1.0f, 0.0f},
            // Top face (y = +S) - YELLOW
            {-S,  S,  S,  1.0f, 1.0f, 0.0f}, { S,  S,  S,  1.0f, 1.0f, 0.0f}, { S,  S, -S,  1.0f, 1.0f, 0.0f},
            {-S,  S,  S,  1.0f, 1.0f, 0.0f}, { S,  S, -S,  1.0f, 1.0f, 0.0f}, {-S,  S, -S,  1.0f, 1.0f, 0.0f},
            // Bottom face (y = -S) - MAGENTA
            {-S, -S, -S,  1.0f, 0.0f, 1.0f}, { S, -S, -S,  1.0f, 0.0f, 1.0f}, { S, -S,  S,  1.0f, 0.0f, 1.0f},
            {-S, -S, -S,  1.0f, 0.0f, 1.0f}, { S, -S,  S,  1.0f, 0.0f, 1.0f}, {-S, -S,  S,  1.0f, 0.0f, 1.0f},
            // Right face (x = +S) - CYAN
            { S, -S,  S,  0.0f, 1.0f, 1.0f}, { S, -S, -S,  0.0f, 1.0f, 1.0f}, { S,  S, -S,  0.0f, 1.0f, 1.0f},
            { S, -S,  S,  0.0f, 1.0f, 1.0f}, { S,  S, -S,  0.0f, 1.0f, 1.0f}, { S,  S,  S,  0.0f, 1.0f, 1.0f},
            // Left face (x = -S) - WHITE
            {-S, -S, -S,  1.0f, 1.0f, 1.0f}, {-S, -S,  S,  1.0f, 1.0f, 1.0f}, {-S,  S,  S,  1.0f, 1.0f, 1.0f},
            {-S, -S, -S,  1.0f, 1.0f, 1.0f}, {-S,  S,  S,  1.0f, 1.0f, 1.0f}, {-S,  S, -S,  1.0f, 1.0f, 1.0f},
        };
        m_testCubeVertexCount = static_cast<uint32_t>(cubeVertices.size());

        RHI::BufferDesc vboDesc{};
        vboDesc.size = cubeVertices.size() * sizeof(SimpleVertex);
        vboDesc.usage = RHI::BufferUsage::Vertex;
        vboDesc.memory = RHI::MemoryUsage::CpuToGpu;  // Allow uploads
        vboDesc.debugName = "TestCubeVBO";

        m_testCubeVBO = m_device->createBuffer(vboDesc);
        if (m_testCubeVBO) {
            m_testCubeVBO->uploadData(cubeVertices.data(), vboDesc.size);
            std::cout << "[DeferredRendererRHI] Cube VBO created: " << m_testCubeVertexCount << " vertices" << std::endl;
        }

        // 2. Create camera uniform buffer (includes chunkOffset for terrain rendering)
        struct CameraUBO {
            glm::mat4 view;
            glm::mat4 projection;
            glm::mat4 viewProjection;
            glm::vec4 chunkOffset;  // xyz = chunk world position, w = unused
        };

        RHI::BufferDesc uboDesc{};
        uboDesc.size = sizeof(CameraUBO);
        uboDesc.usage = RHI::BufferUsage::Uniform;
        uboDesc.memory = RHI::MemoryUsage::CpuToGpu;  // Host visible for updates
        uboDesc.persistentMap = true;  // Keep mapped for frame updates
        uboDesc.debugName = "TestCameraUBO";

        m_testCameraUBO = m_device->createBuffer(uboDesc);
        if (m_testCameraUBO) {
            std::cout << "[DeferredRendererRHI] Camera UBO created" << std::endl;
        }

        // 3. Create descriptor set layout (UBO + texture sampler)
        auto* vkDevice = static_cast<RHI::VKDevice*>(m_device.get());
        VkDevice device = vkDevice->getDevice();

        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

        // Binding 0: Camera UBO
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        bindings[0].pImmutableSamplers = nullptr;

        // Binding 1: Texture atlas sampler
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_terrainDescriptorLayout) == VK_SUCCESS) {
            std::cout << "[DeferredRendererRHI] Terrain descriptor layout created" << std::endl;
        }

        // 4. Create descriptor pool (UBO + sampler)
        std::array<VkDescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;

        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_terrainDescriptorPool) == VK_SUCCESS) {
            std::cout << "[DeferredRendererRHI] Terrain descriptor pool created" << std::endl;
        }

        // 5. Create texture atlas
        constexpr int ATLAS_SIZE = 256;  // 16x16 tiles of 16x16 pixels
        std::vector<uint8_t> atlasPixels(ATLAS_SIZE * ATLAS_SIZE * 4, 255);

        // Generate simple procedural textures for each block type
        auto generateTexture = [&](int slot, uint8_t r, uint8_t g, uint8_t b, bool addNoise = true) {
            int tileX = (slot % 16) * 16;
            int tileY = (slot / 16) * 16;
            std::mt19937 rng(slot * 12345);
            for (int y = 0; y < 16; y++) {
                for (int x = 0; x < 16; x++) {
                    int px = tileX + x;
                    int py = tileY + y;
                    int idx = (py * ATLAS_SIZE + px) * 4;
                    int noise = addNoise ? (rng() % 30 - 15) : 0;
                    atlasPixels[idx + 0] = static_cast<uint8_t>(std::clamp(r + noise, 0, 255));
                    atlasPixels[idx + 1] = static_cast<uint8_t>(std::clamp(g + noise, 0, 255));
                    atlasPixels[idx + 2] = static_cast<uint8_t>(std::clamp(b + noise, 0, 255));
                    atlasPixels[idx + 3] = 255;
                }
            }
        };

        // Generate textures: slot 0=stone, 1=dirt, 2=grass_top, 3=grass_side, etc.
        generateTexture(0, 128, 128, 128);   // Stone - gray
        generateTexture(1, 139, 90, 43);     // Dirt - brown
        generateTexture(2, 86, 170, 48);     // Grass top - green
        generateTexture(3, 139, 90, 43);     // Grass side - brown (will overlay green)
        generateTexture(4, 100, 100, 100);   // Cobblestone
        generateTexture(5, 180, 140, 90);    // Planks
        generateTexture(6, 110, 85, 50);     // Log side
        generateTexture(7, 150, 120, 70);    // Log top
        generateTexture(8, 60, 140, 40);     // Leaves
        generateTexture(9, 220, 210, 160);   // Sand
        generateTexture(10, 130, 120, 110);  // Gravel
        generateTexture(11, 50, 100, 200, false);   // Water - blue, no noise
        generateTexture(12, 60, 60, 60);     // Bedrock

        // Create RHI texture
        RHI::TextureDesc atlasDesc{};
        atlasDesc.width = ATLAS_SIZE;
        atlasDesc.height = ATLAS_SIZE;
        atlasDesc.depth = 1;
        atlasDesc.mipLevels = 1;
        atlasDesc.arrayLayers = 1;
        atlasDesc.format = RHI::Format::RGBA8_UNORM;
        atlasDesc.usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst;
        atlasDesc.debugName = "TerrainAtlas";

        m_terrainAtlas = m_device->createTexture(atlasDesc);
        if (m_terrainAtlas) {
            m_terrainAtlas->uploadData(atlasPixels.data(), atlasPixels.size(), 0, 0, 0, 0, 0, ATLAS_SIZE, ATLAS_SIZE, 1);
            std::cout << "[DeferredRendererRHI] Terrain atlas created (" << ATLAS_SIZE << "x" << ATLAS_SIZE << ")" << std::endl;
        }

        // Create sampler (nearest neighbor for pixelated look)
        RHI::SamplerDesc samplerDesc{};
        samplerDesc.minFilter = RHI::Filter::Nearest;
        samplerDesc.magFilter = RHI::Filter::Nearest;
        samplerDesc.mipmapMode = RHI::MipmapMode::Nearest;
        samplerDesc.addressU = RHI::AddressMode::Repeat;
        samplerDesc.addressV = RHI::AddressMode::Repeat;
        samplerDesc.addressW = RHI::AddressMode::Repeat;

        m_terrainSampler = m_device->createSampler(samplerDesc);
        if (m_terrainSampler) {
            std::cout << "[DeferredRendererRHI] Terrain sampler created" << std::endl;
        }

        // 6. Allocate descriptor set
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_terrainDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_terrainDescriptorLayout;

        if (vkAllocateDescriptorSets(device, &allocInfo, &m_terrainDescriptorSet) == VK_SUCCESS) {
            // Update descriptor set with UBO
            auto* vkBuffer = static_cast<RHI::VKBuffer*>(m_testCameraUBO.get());
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = vkBuffer->getVkBuffer();
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(CameraUBO);

            // Update descriptor set with texture
            auto* vkTexture = static_cast<RHI::VKTexture*>(m_terrainAtlas.get());
            auto* vkSampler = static_cast<RHI::VKSampler*>(m_terrainSampler.get());
            VkDescriptorImageInfo imageInfo{};
            imageInfo.sampler = vkSampler->getVkSampler();
            imageInfo.imageView = vkTexture->getVkImageView();
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

            // UBO write
            descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[0].dstSet = m_terrainDescriptorSet;
            descriptorWrites[0].dstBinding = 0;
            descriptorWrites[0].dstArrayElement = 0;
            descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[0].descriptorCount = 1;
            descriptorWrites[0].pBufferInfo = &bufferInfo;

            // Texture write
            descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[1].dstSet = m_terrainDescriptorSet;
            descriptorWrites[1].dstBinding = 1;
            descriptorWrites[1].dstArrayElement = 0;
            descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[1].descriptorCount = 1;
            descriptorWrites[1].pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
            std::cout << "[DeferredRendererRHI] Terrain descriptor set allocated and updated" << std::endl;
        }

        // 6. Create terrain shader for PackedChunkVertex format
        const char* terrainVertGLSL = R"(
#version 460

// PackedChunkVertex format:
// location 0: ivec3 position (int16 x3, divide by 256 for sub-block precision)
// location 1: uvec2 texcoord (uint16 x2)
// location 2: uvec4 packed data (normalIndex, ao, light, texSlot)
layout(location = 0) in ivec3 inPosition;
layout(location = 1) in uvec2 inTexCoord;
layout(location = 2) in uvec4 inPackedData;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out float fragAO;
layout(location = 3) out vec2 fragTexCoord;
layout(location = 4) flat out uint fragTexSlot;

// UBO for camera matrices (shared across all draws)
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
} ubo;

// Push constants for per-chunk data (fast update per draw)
layout(push_constant) uniform PushConstants {
    vec4 chunkOffset;  // xyz = chunk world position, w = unused
} pc;

const vec3 NORMALS[6] = vec3[6](
    vec3(1, 0, 0), vec3(-1, 0, 0),
    vec3(0, 1, 0), vec3(0, -1, 0),
    vec3(0, 0, 1), vec3(0, 0, -1)
);

// Simple color palette based on height and normal
const vec3 FACE_COLORS[6] = vec3[6](
    vec3(0.7, 0.7, 0.7),  // +X gray
    vec3(0.6, 0.6, 0.6),  // -X darker gray
    vec3(0.4, 0.8, 0.3),  // +Y green (top - grass)
    vec3(0.6, 0.4, 0.2),  // -Y brown (bottom - dirt)
    vec3(0.65, 0.65, 0.65), // +Z gray
    vec3(0.55, 0.55, 0.55)  // -Z darker gray
);

void main() {
    // Convert position from fixed point (divide by 256 for sub-block precision)
    vec3 localPos = vec3(inPosition) / 256.0;
    vec3 worldPos = localPos + pc.chunkOffset.xyz;

    gl_Position = ubo.viewProjection * vec4(worldPos, 1.0);
    // Note: GLM perspective with Y-flip already produces correct Vulkan depth

    uint normalIndex = inPackedData.x;
    uint ao = inPackedData.y;

    // Clamp normalIndex to valid range to avoid undefined behavior
    normalIndex = min(normalIndex, 5u);

    fragNormal = NORMALS[normalIndex];
    fragAO = float(ao) / 255.0;

    // Pass through texture coordinates (fixed point 8.8 format)
    fragTexCoord = vec2(inTexCoord) / 256.0;
    fragTexSlot = inPackedData.w;  // texSlot is 4th component

    // Use white/neutral colors - textures provide the actual color
    // Slight tint based on face direction for visual variety
    if (normalIndex == 2u) {
        // Top face (+Y) - slight green tint for grass
        fragColor = vec3(0.9, 1.0, 0.9);
    } else if (normalIndex == 3u) {
        // Bottom face (-Y) - neutral
        fragColor = vec3(1.0, 1.0, 1.0);
    } else {
        // Side faces - neutral
        fragColor = vec3(1.0, 1.0, 1.0);
    }
}
)";

        const char* terrainFragGLSL = R"(
#version 460

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in float fragAO;
layout(location = 3) in vec2 fragTexCoord;
layout(location = 4) flat in uint fragTexSlot;

layout(set = 0, binding = 1) uniform sampler2D texAtlas;

layout(location = 0) out vec4 outColor;

void main() {
    // Calculate atlas UV from texSlot and local UV
    float atlasSize = 16.0;  // 16x16 tiles
    float tileU = float(fragTexSlot % 16u) / atlasSize;
    float tileV = float(fragTexSlot / 16u) / atlasSize;
    float tileSize = 1.0 / atlasSize;

    // Wrap texture coordinates for tiling
    vec2 localUV = fract(fragTexCoord);
    vec2 atlasUV = vec2(tileU, tileV) + localUV * tileSize;

    // Sample texture
    vec4 texColor = texture(texAtlas, atlasUV);

    // Use texture color, modulated by vertex color for variety
    vec3 baseColor = texColor.rgb * fragColor;

    // Sun direction (morning sun from east-ish)
    vec3 sunDir = normalize(vec3(0.4, 0.8, 0.3));

    // Directional lighting
    float NdotL = max(dot(fragNormal, sunDir), 0.0);

    // Hemisphere ambient (sky blue from above, ground brown from below)
    vec3 skyColor = vec3(0.6, 0.7, 0.9);
    vec3 groundColor = vec3(0.3, 0.25, 0.2);
    float hemisphereBlend = fragNormal.y * 0.5 + 0.5;
    vec3 ambient = mix(groundColor, skyColor, hemisphereBlend) * 0.35;

    // Sun diffuse
    vec3 sunColor = vec3(1.0, 0.95, 0.8);
    vec3 diffuse = sunColor * NdotL * 0.65;

    // Apply ambient occlusion (darkens corners and crevices)
    float ao = fragAO;

    // Combine lighting
    vec3 lighting = (ambient + diffuse) * ao;

    // Final color
    vec3 finalColor = baseColor * lighting;

    outColor = vec4(finalColor, 1.0);
}
)";

        auto terrainVertShader = m_shaderCompiler.compile(terrainVertGLSL, ShaderStage::Vertex, options, "terrain_vert");
        auto terrainFragShader = m_shaderCompiler.compile(terrainFragGLSL, ShaderStage::Fragment, options, "terrain_frag");

        if (terrainVertShader && terrainFragShader) {
            auto convertToBytes = [](const std::vector<uint32_t>& spirv) -> std::vector<uint8_t> {
                std::vector<uint8_t> bytes(spirv.size() * sizeof(uint32_t));
                memcpy(bytes.data(), spirv.data(), bytes.size());
                return bytes;
            };

            RHI::ShaderProgramDesc terrainProgDesc{};
            RHI::ShaderSource vertSrc;
            vertSrc.stage = RHI::ShaderStage::Vertex;
            vertSrc.type = RHI::ShaderSourceType::SPIRV;
            vertSrc.spirv = convertToBytes(terrainVertShader->spirv);
            vertSrc.entryPoint = "main";

            RHI::ShaderSource fragSrc;
            fragSrc.stage = RHI::ShaderStage::Fragment;
            fragSrc.type = RHI::ShaderSourceType::SPIRV;
            fragSrc.spirv = convertToBytes(terrainFragShader->spirv);
            fragSrc.entryPoint = "main";

            terrainProgDesc.stages = {vertSrc, fragSrc};
            terrainProgDesc.debugName = "TerrainTest";

            m_terrainTestShader = m_device->createShaderProgram(terrainProgDesc);

            if (m_terrainTestShader) {
                std::cout << "[DeferredRendererRHI] Terrain test shader created" << std::endl;

                // Get swapchain for render pass
                auto* terrainSwapchain = static_cast<RHI::VKSwapchain*>(m_swapchain.get());

                // Create pipeline layout with push constants for per-chunk data
                VkPushConstantRange pushConstantRange{};
                pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
                pushConstantRange.offset = 0;
                pushConstantRange.size = sizeof(glm::vec4);  // chunkOffset

                VkPipelineLayoutCreateInfo pipeLayoutInfo{};
                pipeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                pipeLayoutInfo.setLayoutCount = 1;
                pipeLayoutInfo.pSetLayouts = &m_terrainDescriptorLayout;
                pipeLayoutInfo.pushConstantRangeCount = 1;
                pipeLayoutInfo.pPushConstantRanges = &pushConstantRange;

                VkPipelineLayout terrainPipelineLayout;
                vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &terrainPipelineLayout);

                // Create pipeline with PackedChunkVertex format (16 bytes per vertex)
                // struct PackedChunkVertex {
                //     int16_t x, y, z;     // 6 bytes at offset 0
                //     uint16_t u, v;       // 4 bytes at offset 6
                //     uint8_t normalIndex, ao, light, texSlot; // 4 bytes at offset 10
                //     uint16_t padding;    // 2 bytes at offset 14
                // };
                RHI::VertexInputState terrainVertexInput{};
                terrainVertexInput.bindings = {{0, 16, RHI::VertexInputRate::Vertex}};  // 16 bytes per vertex
                terrainVertexInput.attributes = {
                    {0, 0, RHI::Format::RGB16_SINT, 0},      // position (int16 x3) at offset 0
                    {1, 0, RHI::Format::RG16_UINT, 6},       // texcoord (uint16 x2) at offset 6
                    {2, 0, RHI::Format::RGBA8_UINT, 10}      // packed data (uint8 x4) at offset 10
                };

                RHI::GraphicsPipelineDesc terrainPipeDesc{};
                terrainPipeDesc.shaderProgram = m_terrainTestShader.get();
                terrainPipeDesc.vertexInput = terrainVertexInput;
                terrainPipeDesc.rasterizer.cullMode = RHI::CullMode::Back;
                terrainPipeDesc.rasterizer.frontFace = RHI::FrontFace::CounterClockwise;
                // Enable depth testing
                terrainPipeDesc.depthStencil.depthTestEnable = true;
                terrainPipeDesc.depthStencil.depthWriteEnable = true;
                terrainPipeDesc.depthStencil.depthCompareOp = RHI::CompareOp::LessOrEqual;
                terrainPipeDesc.colorBlendStates = {{false}};
                terrainPipeDesc.nativeRenderPass = terrainSwapchain->getRenderPass();
                terrainPipeDesc.nativePipelineLayout = terrainPipelineLayout;
                terrainPipeDesc.debugName = "TerrainTestPipeline";

                m_terrainTestPipeline = m_device->createGraphicsPipeline(terrainPipeDesc);
                if (m_terrainTestPipeline) {
                    std::cout << "[DeferredRendererRHI] Terrain test pipeline created successfully" << std::endl;
                } else {
                    std::cerr << "[DeferredRendererRHI] Failed to create terrain test pipeline" << std::endl;
                }
            }
        } else {
            std::cerr << "[DeferredRendererRHI] Failed to compile terrain test shaders" << std::endl;
        }
    }
#endif // DISABLE_VULKAN

    std::cout << "[DeferredRendererRHI] Pipelines created successfully" << std::endl;
    return true;
}

} // namespace Render
