#include "WorldRendererRHI.h"
#include "../world/World.h"
#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

namespace Render {

bool WorldRendererRHI::initialize(RHI::RHIDevice* device) {
    m_device = device;

    if (!createChunkOffsetBuffer()) {
        std::cerr << "[WorldRendererRHI] Failed to create chunk offset buffer" << std::endl;
        return false;
    }

    std::cout << "[WorldRendererRHI] Initialized" << std::endl;
    return true;
}

void WorldRendererRHI::shutdown() {
    m_chunkOffsetUBO.reset();
    m_device = nullptr;
}

bool WorldRendererRHI::createChunkOffsetBuffer() {
    if (!m_device) return false;

    RHI::BufferDesc desc{};
    desc.size = sizeof(glm::vec4);  // vec3 + padding
    desc.usage = RHI::BufferUsage::Uniform;
    desc.memory = RHI::MemoryUsage::CpuToGpu;
    desc.debugName = "WorldRenderer_ChunkOffset";

    m_chunkOffsetUBO = m_device->createBuffer(desc);
    return m_chunkOffsetUBO != nullptr;
}

void WorldRendererRHI::updateChunkOffset(const glm::vec3& offset) {
    if (!m_chunkOffsetUBO) return;

    glm::vec4 data(offset, 0.0f);
    void* mapped = m_chunkOffsetUBO->map();
    if (mapped) {
        memcpy(mapped, glm::value_ptr(data), sizeof(glm::vec4));
        m_chunkOffsetUBO->unmap();
    }
}

void WorldRendererRHI::render(RHI::RHICommandBuffer* cmd, ::World& world, const WorldRenderParams& params) {
    // Update frustum for culling
    world.updateFrustum(params.viewProjection);

    // Render solid geometry
    renderSolid(cmd, world, params);

    // Optionally render water
    if (params.renderWater) {
        renderWater(cmd, world, params);
    }
}

void WorldRendererRHI::renderSolid(RHI::RHICommandBuffer* cmd, ::World& world, const WorldRenderParams& params) {
    (void)cmd;  // Used for RHI state in hybrid mode

    // Skip if no device or Vulkan backend - hybrid path uses OpenGL calls
    // TODO: Implement proper Vulkan rendering path that uses RHI commands
    if (!m_device || m_device->getBackend() == RHI::Backend::Vulkan) {
        return;
    }

    // Set forced LOD if specified
    int previousForcedLOD = world.forcedLOD;
    if (params.forcedLOD >= 0) {
        world.forcedLOD = params.forcedLOD;
    }

    // For now, use hybrid approach - RHI sets up state, World uses OpenGL for draws
    // This works because our GLCommandBuffer translates RHI state to GL state
    if (m_useHybridPath) {
        // Get chunk offset uniform location from current shader program
        // In hybrid mode, we need to query this from OpenGL
        GLint currentProgram = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);

        // DEBUG: Log state before rendering (first frame only)
        static bool firstRender = true;
        if (firstRender) {
            GLint currentFBO = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFBO);
            std::cout << "[WorldRendererRHI] Before draw - Program=" << currentProgram
                      << ", FBO=" << currentFBO << std::endl;

            // Check if MRT is configured
            GLint drawBuffer0 = 0, drawBuffer1 = 0, drawBuffer2 = 0;
            glGetIntegerv(GL_DRAW_BUFFER0, &drawBuffer0);
            glGetIntegerv(GL_DRAW_BUFFER1, &drawBuffer1);
            glGetIntegerv(GL_DRAW_BUFFER2, &drawBuffer2);
            std::cout << "[WorldRendererRHI] Draw buffers: " << std::hex
                      << drawBuffer0 << ", " << drawBuffer1 << ", " << drawBuffer2
                      << std::dec << std::endl;

            // Check depth state
            GLboolean depthTest, depthMask;
            glGetBooleanv(GL_DEPTH_TEST, &depthTest);
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
            std::cout << "[WorldRendererRHI] DepthTest=" << (depthTest ? "ON" : "OFF")
                      << ", DepthMask=" << (depthMask ? "ON" : "OFF") << std::endl;

            firstRender = false;
        }

        if (currentProgram > 0) {
            m_glChunkOffsetLoc = glGetUniformLocation(currentProgram, "chunkOffset");

            // DEBUG: Log uniform location and VAO state (first render only)
            static bool firstUniformLog = true;
            if (firstUniformLog) {
                std::cout << "[WorldRendererRHI] chunkOffset uniform location = " << m_glChunkOffsetLoc << std::endl;

                // Check VAO binding
                GLint currentVAO = 0;
                glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &currentVAO);
                std::cout << "[WorldRendererRHI] Current VAO before mesh draw = " << currentVAO << std::endl;
                firstUniformLog = false;
            }
        }

        // Choose rendering mode based on params
        switch (params.mode) {
            case WorldRenderMode::GPUCulled:
                if (world.gpuCullingEnabled && world.gpuCullingInitialized) {
                    world.renderSubChunksGPUCulled(params.cameraPosition, params.viewProjection, m_glChunkOffsetLoc);
                } else {
                    world.renderSubChunksBatched(params.cameraPosition, m_glChunkOffsetLoc);
                }
                break;

            case WorldRenderMode::Batched:
                world.renderSubChunksBatched(params.cameraPosition, m_glChunkOffsetLoc);
                break;

            case WorldRenderMode::MeshShader:
                if (g_meshShadersAvailable && g_enableMeshShaders) {
                    world.renderSubChunksMeshShader(params.cameraPosition, params.viewProjection);
                } else {
                    world.renderSubChunks(params.cameraPosition, m_glChunkOffsetLoc);
                }
                break;

            case WorldRenderMode::Standard:
            default:
                world.renderSubChunks(params.cameraPosition, m_glChunkOffsetLoc);
                break;
        }

        // Update stats
        m_lastRenderedSubChunks = world.lastRenderedSubChunks;
        m_lastCulledSubChunks = world.lastCulledSubChunks;
    }

    // Restore forced LOD
    world.forcedLOD = previousForcedLOD;
}

void WorldRendererRHI::renderWater(RHI::RHICommandBuffer* cmd, ::World& world, const WorldRenderParams& params) {
    (void)cmd;  // Used for RHI state in hybrid mode

    // Skip if no device or Vulkan backend
    if (!m_device || m_device->getBackend() == RHI::Backend::Vulkan) {
        return;
    }

    if (m_useHybridPath) {
        // Get chunk offset uniform location
        GLint currentProgram = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);

        GLint waterChunkOffsetLoc = -1;
        if (currentProgram > 0) {
            waterChunkOffsetLoc = glGetUniformLocation(currentProgram, "chunkOffset");
        }

        // Render water using World's existing method
        world.renderWaterSubChunks(params.cameraPosition, waterChunkOffsetLoc);
    }
}

void WorldRendererRHI::renderShadow(RHI::RHICommandBuffer* cmd, ::World& world, const WorldRenderParams& params,
                                     int maxShadowDistance) {
    (void)cmd;

    // Skip if no device or Vulkan backend
    if (!m_device || m_device->getBackend() == RHI::Backend::Vulkan) {
        return;
    }

    if (m_useHybridPath) {
        GLint currentProgram = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);

        GLint shadowChunkOffsetLoc = -1;
        if (currentProgram > 0) {
            shadowChunkOffsetLoc = glGetUniformLocation(currentProgram, "chunkOffset");
        }

        // Use World's shadow render method (handles LOD and distance override)
        world.renderForShadow(params.cameraPosition, shadowChunkOffsetLoc, maxShadowDistance);
    }
}

} // namespace Render
