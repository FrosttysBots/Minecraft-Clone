#pragma once

// WorldRendererRHI - Bridge between World rendering and RHI command buffers
// This allows the deferred renderer to submit world geometry through the RHI abstraction

#include "rhi/RHI.h"
#include "Renderer.h"
#include <glm/glm.hpp>

// Forward declarations
class World;

namespace Render {

// Rendering mode for world geometry
enum class WorldRenderMode {
    Standard,       // Basic sub-chunk rendering with CPU frustum culling
    Batched,        // Sodium-style batched rendering
    GPUCulled,      // GPU compute shader frustum culling
    MeshShader      // NVidia mesh shader path
};

// World rendering context passed to render calls
struct WorldRenderParams {
    glm::vec3 cameraPosition;
    glm::mat4 viewProjection;
    RHI::RHIGraphicsPipeline* pipeline = nullptr;
    RHI::RHIDescriptorSet* descriptorSet = nullptr;
    WorldRenderMode mode = WorldRenderMode::Standard;
    int forcedLOD = -1;  // -1 for automatic LOD
    bool renderWater = false;
};

// WorldRendererRHI wraps World rendering for use with RHI command buffers
// Currently uses hybrid approach: RHI for state setup, OpenGL for actual draws
// Future: Full RHI draw commands when VertexPool is ported
class WorldRendererRHI {
public:
    WorldRendererRHI() = default;
    ~WorldRendererRHI() = default;

    // Initialize with RHI device
    bool initialize(RHI::RHIDevice* device);
    void shutdown();

    // Main render method - records world geometry into command buffer
    // For OpenGL backend, this sets up state via RHI then calls World's GL methods
    // For Vulkan backend (future), this will use RHI draw commands
    void render(RHI::RHICommandBuffer* cmd, ::World& world, const WorldRenderParams& params);

    // Render solid geometry only (for G-Buffer pass)
    void renderSolid(RHI::RHICommandBuffer* cmd, ::World& world, const WorldRenderParams& params);

    // Render water geometry only (for transparency pass)
    void renderWater(RHI::RHICommandBuffer* cmd, ::World& world, const WorldRenderParams& params);

    // Render for shadow pass (reduced distance, fixed LOD)
    void renderShadow(RHI::RHICommandBuffer* cmd, ::World& world, const WorldRenderParams& params,
                      int maxShadowDistance);

    // Create chunk offset UBO for push constants / uniform buffer
    bool createChunkOffsetBuffer();
    void updateChunkOffset(const glm::vec3& offset);

    // Get stats from last render
    int getRenderedSubChunks() const { return m_lastRenderedSubChunks; }
    int getCulledSubChunks() const { return m_lastCulledSubChunks; }

private:
    RHI::RHIDevice* m_device = nullptr;

    // Chunk offset uniform buffer (for passing per-draw chunk position)
    std::unique_ptr<RHI::RHIBuffer> m_chunkOffsetUBO;

    // Cached chunk offset location for OpenGL fallback
    int32_t m_glChunkOffsetLoc = -1;

    // Stats
    int m_lastRenderedSubChunks = 0;
    int m_lastCulledSubChunks = 0;

    // Flag for using hybrid OpenGL path (until full RHI port)
    bool m_useHybridPath = true;
};

} // namespace Render
