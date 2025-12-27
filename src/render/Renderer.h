#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <string>

// Forward declarations
class World;
class Camera;
struct GLFWwindow;

namespace Render {

// Forward declarations
class RenderPass;

// Rendering statistics for profiling
struct RenderStats {
    float shadowTime = 0.0f;
    float gbufferTime = 0.0f;
    float hizTime = 0.0f;
    float ssaoTime = 0.0f;
    float compositeTime = 0.0f;
    float waterTime = 0.0f;
    float skyTime = 0.0f;
    float totalTime = 0.0f;

    uint32_t drawCalls = 0;
    uint32_t triangles = 0;
    uint32_t chunksRendered = 0;
    uint32_t chunksTotal = 0;
    uint32_t chunksCulled = 0;
};

// Lighting parameters
struct LightingParams {
    glm::vec3 lightDir = glm::vec3(0.5f, 0.8f, 0.3f);
    glm::vec3 lightColor = glm::vec3(1.0f, 0.95f, 0.85f);
    glm::vec3 ambientColor = glm::vec3(0.3f, 0.35f, 0.4f);
    glm::vec3 skyColor = glm::vec3(0.5f, 0.7f, 1.0f);
    float shadowStrength = 0.6f;
    float time = 0.0f;
};

// Fog parameters
struct FogParams {
    float density = 0.01f;
    float heightFalloff = 0.015f;
    float baseHeight = 64.0f;
    float renderDistance = 256.0f;  // In blocks
    bool isUnderwater = false;
};

// Camera data for rendering
struct CameraData {
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 viewProjection;
    glm::mat4 invView;
    glm::mat4 invProjection;
    glm::mat4 invViewProjection;
    glm::vec3 position;
    glm::vec3 forward;
    float nearPlane;
    float farPlane;
    float fov;
    float aspectRatio;
};

// Render configuration
struct RenderConfig {
    uint32_t renderWidth = 1280;
    uint32_t renderHeight = 720;
    uint32_t displayWidth = 1280;
    uint32_t displayHeight = 720;

    // Feature toggles
    bool enableSSAO = true;
    bool enableShadows = true;
    bool enableFSR = false;
    bool enableHiZCulling = true;
    bool useDeferredRendering = true;

    // Quality settings
    uint32_t shadowResolution = 2048;
    uint32_t numCascades = 3;
    uint32_t ssaoSamples = 16;
    float ssaoRadius = 0.5f;
    float ssaoBias = 0.025f;

    // Debug
    int debugMode = 0;  // 0=normal, 1-8=debug views
};

// Abstract renderer interface
class Renderer {
public:
    virtual ~Renderer() = default;

    // Lifecycle
    virtual bool initialize(GLFWwindow* window, const RenderConfig& config) = 0;
    virtual void shutdown() = 0;
    virtual void resize(uint32_t width, uint32_t height) = 0;

    // Per-frame operations
    virtual void beginFrame() = 0;
    virtual void render(World& world, const CameraData& camera) = 0;
    virtual void endFrame() = 0;

    // Configuration
    virtual void setConfig(const RenderConfig& config) = 0;
    virtual const RenderConfig& getConfig() const = 0;

    // Lighting and effects
    virtual void setLighting(const LightingParams& lighting) = 0;
    virtual void setFog(const FogParams& fog) = 0;

    // Stats
    virtual const RenderStats& getStats() const = 0;

    // Debug
    virtual void setDebugMode(int mode) = 0;
    virtual int getDebugMode() const = 0;

    // Factory method
    static std::unique_ptr<Renderer> create(bool useDeferred = true);
};

// Render context passed to each render pass
// Contains shared state and resources
struct RenderContext {
    // Window
    GLFWwindow* window = nullptr;

    // Current frame data
    const CameraData* camera = nullptr;
    const LightingParams* lighting = nullptr;
    const FogParams* fog = nullptr;
    const RenderConfig* config = nullptr;

    // World reference
    World* world = nullptr;

    // Frame timing
    float deltaTime = 0.0f;
    float time = 0.0f;
    uint64_t frameNumber = 0;

    // G-Buffer textures (for passes that need them)
    uint32_t gPosition = 0;
    uint32_t gNormal = 0;
    uint32_t gAlbedo = 0;
    uint32_t gDepth = 0;

    // Shadow maps
    uint32_t cascadeShadowMaps = 0;
    glm::mat4 cascadeMatrices[4];
    float cascadeSplits[4];

    // SSAO
    uint32_t ssaoTexture = 0;

    // Scene color (for post-processing)
    uint32_t sceneColor = 0;
    uint32_t sceneDepth = 0;

    // Hi-Z
    uint32_t hiZTexture = 0;
    int hiZMipLevels = 0;

    // Texture atlas
    uint32_t textureAtlas = 0;

    // Accumulator for stats
    RenderStats stats;
};

} // namespace Render
