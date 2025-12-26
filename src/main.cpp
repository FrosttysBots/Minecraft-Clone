#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "core/Camera.h"
#include "core/Player.h"
#include "core/Raycast.h"
#include "core/Config.h"
#include "world/World.h"
#include "render/Crosshair.h"
#include "render/BlockHighlight.h"
#include "render/TextureAtlas.h"
#include "render/VertexPool.h"

#include <iostream>
#include <iomanip>
#include <string>
#include <optional>
#include <cstdio>
#include <cmath>
#include <random>
#include <algorithm>
#include <limits>
#include <fstream>
#include <chrono>

// Force high-performance GPU on laptops (NVIDIA Optimus / AMD Switchable)
#ifdef _WIN32
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

// Window dimensions (loaded from config)
int WINDOW_WIDTH = 1280;
int WINDOW_HEIGHT = 720;
const char* WINDOW_TITLE = "Voxel Engine";

// Render resolution (for FSR upscaling)
int RENDER_WIDTH = 1280;
int RENDER_HEIGHT = 720;
float g_renderScale = 1.0f;  // 1.0 = native, 0.5 = 50% resolution

// Global state
Camera camera(glm::vec3(8.0f, 100.0f, 8.0f));
Player* player = nullptr;  // Will be initialized after world generation
float lastX = WINDOW_WIDTH / 2.0f;
float lastY = WINDOW_HEIGHT / 2.0f;
bool firstMouse = true;
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// World
World world;

// Input state
bool flyTogglePressed = false;

// Block interaction
std::optional<RaycastHit> currentTarget;
constexpr float REACH_DISTANCE = 5.0f;

// Selected block type for placing
BlockType selectedBlock = BlockType::STONE;
int selectedSlot = 0;

// Hotbar blocks
BlockType hotbar[] = {
    BlockType::STONE,
    BlockType::DIRT,
    BlockType::GRASS,
    BlockType::COBBLESTONE,
    BlockType::WOOD_PLANKS,
    BlockType::WOOD_LOG,
    BlockType::WATER,
    BlockType::GLASS,
    BlockType::SAND
};
constexpr int HOTBAR_SIZE = sizeof(hotbar) / sizeof(hotbar[0]);

// Wireframe toggle
bool wireframeMode = false;
bool wireframeKeyPressed = false;

// Deferred rendering toggles
bool g_useDeferredRendering = true;  // Master toggle
bool g_enableSSAO = true;            // SSAO toggle
int g_deferredDebugMode = 0;         // 0=normal, 1=albedo, 2=normals, 3=position, 4=depth

// Daylight cycle toggle
bool doDaylightCycle = true;
int cloudStyle = 0;  // 0 = simple, 1 = volumetric
bool cloudTogglePressed = false;
bool daylightTogglePressed = false;

// Weather system
enum class WeatherType { CLEAR, RAIN, SNOW, THUNDERSTORM };
WeatherType currentWeather = WeatherType::CLEAR;
bool weatherTogglePressed = false;
float weatherIntensity = 0.0f;        // 0 = clear, 1 = full intensity
float targetWeatherIntensity = 0.0f;  // For smooth transitions
float lightningFlash = 0.0f;          // Lightning brightness
float nextLightningTime = 0.0f;       // When next lightning strikes
float thunderTimer = 0.0f;            // For thunder delay after lightning

// Mouse button state for click detection
bool leftMousePressed = false;
bool rightMousePressed = false;

// Game state for loading screen
enum class GameState { LOADING, PLAYING };
GameState gameState = GameState::LOADING;
int totalChunksToLoad = 0;
int chunksLoaded = 0;

// ============================================
// DEFERRED RENDERING INFRASTRUCTURE
// ============================================

// G-Buffer textures and FBO
GLuint gBufferFBO = 0;
GLuint gPosition = 0;    // RGB16F: world position, A: vertex AO
GLuint gNormal = 0;      // RGB16F: world normal, A: light level
GLuint gAlbedo = 0;      // RGBA8: albedo RGB, emission flag A
GLuint gDepth = 0;       // DEPTH32F: linear depth

// FSR (FidelityFX Super Resolution) upscaling
GLuint sceneFBO = 0;           // Render target at render resolution (before FSR)
GLuint sceneColorTexture = 0;  // RGB16F: scene color at render resolution
GLuint sceneDepthRBO = 0;      // Depth renderbuffer for scene FBO
bool g_enableFSR = false;      // FSR enable toggle (runtime)

// Mesh shader support (GL_NV_mesh_shader)
bool g_meshShadersAvailable = false;   // Hardware support detected
bool g_enableMeshShaders = false;      // Runtime toggle
GLuint meshShaderProgram = 0;          // Mesh shader program (task + mesh + fragment)
GLuint meshShaderVertexSSBO = 0;       // SSBO for vertex data
GLuint meshShaderMeshletSSBO = 0;      // SSBO for meshlet descriptors
GLuint meshShaderDataUBO = 0;          // UBO for mesh shader uniforms
GLuint frustumPlanesUBO = 0;           // UBO for frustum planes (culling)

// Sodium-style batched rendering
bool g_enableBatchedRendering = true;  // Column-batched rendering (reduces uniform updates)

// GL_NV_mesh_shader extension constants (not in glad by default)
#ifndef GL_TASK_SHADER_NV
#define GL_TASK_SHADER_NV 0x955A
#endif
#ifndef GL_MESH_SHADER_NV
#define GL_MESH_SHADER_NV 0x9559
#endif

// Function pointer for glDrawMeshTasksNV (use GLAD's if available, otherwise define our own)
#ifndef GLAD_GL_NV_mesh_shader
typedef void (APIENTRY* PFNGLDRAWMESHTASKSNVPROC_LOCAL)(GLuint first, GLuint count);
PFNGLDRAWMESHTASKSNVPROC_LOCAL pfn_glDrawMeshTasksNV = nullptr;
#define glDrawMeshTasksNV_ptr pfn_glDrawMeshTasksNV
#else
// GLAD has mesh shader support, use its function pointer
#define glDrawMeshTasksNV_ptr glad_glDrawMeshTasksNV
#endif

// Cascade shadow maps (3 cascades)
const int NUM_CASCADES = 3;
const unsigned int CASCADE_RESOLUTION = 2048;
GLuint cascadeShadowFBO = 0;
GLuint cascadeShadowMaps = 0;  // GL_TEXTURE_2D_ARRAY
float cascadeSplitDepths[NUM_CASCADES] = {0.0f};
glm::mat4 cascadeLightSpaceMatrices[NUM_CASCADES];

// Hi-Z occlusion culling
GLuint hiZTexture = 0;
GLuint hiZFBO = 0;
int hiZLevels = 0;
bool g_enableHiZCulling = true;       // Hi-Z occlusion culling toggle
bool g_enableSubChunkCulling = true;  // Sub-chunk vertical culling toggle
GLuint chunkBoundsSSBO = 0;           // SSBO for chunk bounding boxes

// OPTIMIZATION: Double-buffered visibility SSBOs to avoid GPU stalls
// Frame N writes to visibilitySSBO[writeIndex], reads from visibilitySSBO[1-writeIndex]
GLuint visibilitySSBO[2] = {0, 0};    // Double-buffered visibility results
int visibilityWriteIndex = 0;          // Which buffer to write to this frame
GLsync visibilityFence[2] = {nullptr, nullptr};  // Fence for async readback
std::vector<GLuint> cachedVisibilityResults;     // Cached results from previous frame
std::vector<glm::ivec3> cachedSubChunkPositions; // Cached positions from previous frame
int lastOccludedChunks = 0;           // Stats: chunks hidden by Hi-Z

// OPTIMIZATION: Hi-Z frame skipping - only regenerate every N frames
int g_hiZUpdateInterval = 2;          // Update Hi-Z every N frames (1 = every frame, 2 = every other, etc.)
int g_hiZFrameCounter = 0;            // Current frame counter for Hi-Z updates

// OPTIMIZATION: Shadow cascade frame skipping and distance limiting
int g_shadowFrameCounter = 0;                    // Current frame counter for shadow updates
int g_cascadeUpdateIntervals[3] = {1, 2, 4};     // Update interval per cascade (near=every frame, mid=every 2, far=every 4)
int g_cascadeShadowDistances[3] = {6, 10, 14};   // Shadow render distance per cascade (in chunks)
bool g_cascadeNeedsUpdate[3] = {true, true, true}; // Track which cascades need updating this frame

// SSAO
const int SSAO_KERNEL_SIZE = 32;
const int SSAO_NOISE_SIZE = 4;
GLuint ssaoFBO = 0;
GLuint ssaoBlurFBO = 0;
GLuint ssaoColorBuffer = 0;
GLuint ssaoBlurBuffer = 0;
GLuint ssaoNoiseTexture = 0;
GLuint ssaoKernelUBO = 0;  // OPTIMIZATION: UBO for SSAO kernel (uploaded once)
std::vector<glm::vec3> ssaoKernel;

// Full-screen quad for deferred passes
GLuint quadVAO = 0;
GLuint quadVBO = 0;

// Debug visualization mode
int debugMode = 0;  // 0=off, 1=position, 2=normal, 3=albedo, 4=SSAO, 5=cascade
int meshesBuilt = 0;
std::string loadingMessage = "Initializing...";

// ============================================
// PERFORMANCE PROFILING
// ============================================
struct PerformanceStats {
    // Frame timing
    double frameTimeMs = 0.0;
    double fps = 0.0;

    // GPU timing (in milliseconds)
    double shadowPassMs = 0.0;
    double gBufferPassMs = 0.0;
    double hiZPassMs = 0.0;
    double ssaoPassMs = 0.0;
    double compositePassMs = 0.0;
    double waterPassMs = 0.0;
    double precipPassMs = 0.0;
    double skyPassMs = 0.0;
    double uiPassMs = 0.0;
    double totalGpuMs = 0.0;

    // CPU timing for additional operations (in milliseconds)
    double worldUpdateMs = 0.0;
    double inputProcessMs = 0.0;
    double particleUpdateMs = 0.0;
    double swapBuffersMs = 0.0;

    // Chunk stats
    int chunksRendered = 0;
    int chunksFrustumCulled = 0;
    int chunksHiZCulled = 0;
    int subChunksRendered = 0;     // Sub-chunks rendered (when using sub-chunk culling)
    int subChunksFrustumCulled = 0; // Sub-chunks frustum culled
    int waterSubChunksRendered = 0;  // Water sub-chunks rendered
    int waterSubChunksCulled = 0;    // Water sub-chunks frustum culled
    int totalVertices = 0;
    int drawCalls = 0;

    // Memory
    size_t chunksLoaded = 0;
    size_t meshesLoaded = 0;
};

PerformanceStats g_perfStats;
bool g_showPerfStats = true;  // Toggle with F11

// GPU Timer queries (double-buffered to avoid stalls)
const int NUM_GPU_TIMERS = 9;  // Shadow, GBuffer, HiZ, SSAO, Composite, Water, Precip, Sky, UI
GLuint gpuTimerQueries[2][NUM_GPU_TIMERS];  // [frame % 2][timer]
int currentTimerFrame = 0;
bool gpuTimersReady = false;

enum GPUTimer {
    TIMER_SHADOW = 0,
    TIMER_GBUFFER = 1,
    TIMER_HIZ = 2,
    TIMER_SSAO = 3,
    TIMER_COMPOSITE = 4,
    TIMER_WATER = 5,
    TIMER_PRECIP = 6,
    TIMER_SKY = 7,
    TIMER_UI = 8
};

// Render timing file output
std::ofstream g_renderTimeFile;
bool g_logRenderTiming = true;
int g_frameNumber = 0;

void initRenderTimingLog() {
    g_renderTimeFile.open("RenderTime.txt", std::ios::out | std::ios::trunc);
    if (g_renderTimeFile.is_open()) {
        g_renderTimeFile << "=== Voxel Engine Render Timing Log ===" << std::endl;
        g_renderTimeFile << "Started: " << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << std::endl;
        g_renderTimeFile << std::endl;
        g_renderTimeFile << "Frame,FPS,FrameTimeMs,ShadowMs,GBufferMs,HiZMs,SSAOMs,CompositeMs,WaterMs,PrecipMs,SkyMs,UIMs,TotalGPUMs,WorldUpdateMs,InputMs,ParticleMs,SwapMs,ChunksRendered,SubChunksRendered,WaterSubChunks" << std::endl;
    }
}

void logRenderTiming() {
    if (!g_renderTimeFile.is_open() || !g_logRenderTiming) return;

    g_renderTimeFile << std::fixed << std::setprecision(3);
    g_renderTimeFile << g_frameNumber << ","
                     << g_perfStats.fps << ","
                     << g_perfStats.frameTimeMs << ","
                     << g_perfStats.shadowPassMs << ","
                     << g_perfStats.gBufferPassMs << ","
                     << g_perfStats.hiZPassMs << ","
                     << g_perfStats.ssaoPassMs << ","
                     << g_perfStats.compositePassMs << ","
                     << g_perfStats.waterPassMs << ","
                     << g_perfStats.precipPassMs << ","
                     << g_perfStats.skyPassMs << ","
                     << g_perfStats.uiPassMs << ","
                     << g_perfStats.totalGpuMs << ","
                     << g_perfStats.worldUpdateMs << ","
                     << g_perfStats.inputProcessMs << ","
                     << g_perfStats.particleUpdateMs << ","
                     << g_perfStats.swapBuffersMs << ","
                     << g_perfStats.chunksRendered << ","
                     << g_perfStats.subChunksRendered << ","
                     << g_perfStats.waterSubChunksRendered << std::endl;

    // Flush every 100 frames to ensure data is written
    if (g_frameNumber % 100 == 0) {
        g_renderTimeFile.flush();
    }
}

void closeRenderTimingLog() {
    if (g_renderTimeFile.is_open()) {
        g_renderTimeFile << std::endl;
        g_renderTimeFile << "=== End of Render Timing Log ===" << std::endl;
        g_renderTimeFile << "Total frames logged: " << g_frameNumber << std::endl;
        g_renderTimeFile.close();
    }
}

// Callback for window resize
void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    (void)window;
    glViewport(0, 0, width, height);
}

// Callback for mouse movement
void mouseCallback(GLFWwindow* window, double xposIn, double yposIn) {
    (void)window;
    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xOffset = xpos - lastX;
    float yOffset = lastY - ypos;

    lastX = xpos;
    lastY = ypos;

    camera.processMouseMovement(xOffset, yOffset);
}

// Callback for mouse scroll - now used for hotbar selection
void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    (void)window;
    (void)xoffset;

    // Scroll through hotbar
    selectedSlot -= static_cast<int>(yoffset);
    if (selectedSlot < 0) selectedSlot = HOTBAR_SIZE - 1;
    if (selectedSlot >= HOTBAR_SIZE) selectedSlot = 0;
    selectedBlock = hotbar[selectedSlot];
}

// Callback for mouse buttons
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    (void)window;
    (void)mods;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS && !leftMousePressed) {
            leftMousePressed = true;
            // Break block
            if (currentTarget.has_value()) {
                glm::ivec3 pos = currentTarget->blockPos;
                BlockType block = world.getBlock(pos.x, pos.y, pos.z);
                if (block != BlockType::BEDROCK) { // Can't break bedrock
                    world.setBlock(pos.x, pos.y, pos.z, BlockType::AIR);
                }
            }
        } else if (action == GLFW_RELEASE) {
            leftMousePressed = false;
        }
    }

    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS && !rightMousePressed) {
            rightMousePressed = true;
            // Place block
            if (currentTarget.has_value() && player) {
                glm::ivec3 placePos = currentTarget->blockPos + currentTarget->normal;

                // Don't place block inside player
                float halfW = Player::WIDTH / 2.0f;
                glm::vec3 playerMin = player->position - glm::vec3(halfW, 0.0f, halfW);
                glm::vec3 playerMax = player->position + glm::vec3(halfW, Player::HEIGHT, halfW);
                glm::vec3 blockMin = glm::vec3(placePos);
                glm::vec3 blockMax = glm::vec3(placePos) + glm::vec3(1.0f);

                bool collision =
                    playerMin.x < blockMax.x && playerMax.x > blockMin.x &&
                    playerMin.y < blockMax.y && playerMax.y > blockMin.y &&
                    playerMin.z < blockMax.z && playerMax.z > blockMin.z;

                if (!collision && placePos.y >= 0 && placePos.y < CHUNK_SIZE_Y) {
                    world.setBlock(placePos.x, placePos.y, placePos.z, selectedBlock);
                }
            }
        } else if (action == GLFW_RELEASE) {
            rightMousePressed = false;
        }
    }
}

// Process keyboard input - returns input state for player
struct InputState {
    bool forward = false;
    bool backward = false;
    bool left = false;
    bool right = false;
    bool jump = false;
    bool descend = false;
    bool sprint = false;
};

InputState processInput(GLFWwindow* window) {
    InputState input;

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }

    // Movement keys
    input.forward = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
    input.backward = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
    input.left = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
    input.right = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;
    input.jump = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    input.descend = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
    input.sprint = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;

    // Fly mode toggle (F2)
    if (glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS) {
        if (!flyTogglePressed && player) {
            player->toggleFlying();
            flyTogglePressed = true;
        }
    } else {
        flyTogglePressed = false;
    }

    // Wireframe toggle (F1)
    if (glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS) {
        if (!wireframeKeyPressed) {
            wireframeMode = !wireframeMode;
            glPolygonMode(GL_FRONT_AND_BACK, wireframeMode ? GL_LINE : GL_FILL);
            wireframeKeyPressed = true;
        }
    } else {
        wireframeKeyPressed = false;
    }

    // Daylight cycle toggle (F3)
    if (glfwGetKey(window, GLFW_KEY_F3) == GLFW_PRESS) {
        if (!daylightTogglePressed) {
            doDaylightCycle = !doDaylightCycle;
            std::cout << "Daylight cycle: " << (doDaylightCycle ? "ON" : "OFF") << std::endl;
            daylightTogglePressed = true;
        }
    } else {
        daylightTogglePressed = false;
    }

    // Cloud style toggle (F4)
    if (glfwGetKey(window, GLFW_KEY_F4) == GLFW_PRESS) {
        if (!cloudTogglePressed) {
            cloudStyle = (cloudStyle + 1) % 2;
            std::cout << "Cloud style: " << (cloudStyle == 0 ? "Simple" : "Volumetric") << std::endl;
            cloudTogglePressed = true;
        }
    } else {
        cloudTogglePressed = false;
    }

    // Weather toggle (F5)
    if (glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS) {
        if (!weatherTogglePressed) {
            int w = static_cast<int>(currentWeather);
            w = (w + 1) % 4;
            currentWeather = static_cast<WeatherType>(w);
            targetWeatherIntensity = (currentWeather == WeatherType::CLEAR) ? 0.0f : 1.0f;
            const char* weatherNames[] = {"Clear", "Rain", "Snow", "Thunderstorm"};
            std::cout << "Weather: " << weatherNames[w] << std::endl;
            weatherTogglePressed = true;
        }
    } else {
        weatherTogglePressed = false;
    }

    // Noclip toggle (F6)
    static bool noclipTogglePressed = false;
    if (glfwGetKey(window, GLFW_KEY_F6) == GLFW_PRESS) {
        if (!noclipTogglePressed && player) {
            player->toggleNoclip();
            std::cout << "Noclip: " << (player->isNoclip ? "ON" : "OFF") << std::endl;
            noclipTogglePressed = true;
        }
    } else {
        noclipTogglePressed = false;
    }

    // Deferred rendering toggle (F7)
    static bool deferredTogglePressed = false;
    if (glfwGetKey(window, GLFW_KEY_F7) == GLFW_PRESS) {
        if (!deferredTogglePressed) {
            g_useDeferredRendering = !g_useDeferredRendering;
            std::cout << "Deferred rendering: " << (g_useDeferredRendering ? "ON" : "OFF") << std::endl;
            deferredTogglePressed = true;
        }
    } else {
        deferredTogglePressed = false;
    }

    // Sub-chunk culling toggle (F9)
    static bool subChunkTogglePressed = false;
    if (glfwGetKey(window, GLFW_KEY_F9) == GLFW_PRESS) {
        if (!subChunkTogglePressed) {
            g_enableSubChunkCulling = !g_enableSubChunkCulling;
            std::cout << "Sub-chunk Culling: " << (g_enableSubChunkCulling ? "ON" : "OFF") << std::endl;
            subChunkTogglePressed = true;
        }
    } else {
        subChunkTogglePressed = false;
    }

    // Hi-Z occlusion culling toggle (F10)
    static bool hiZTogglePressed = false;
    if (glfwGetKey(window, GLFW_KEY_F10) == GLFW_PRESS) {
        if (!hiZTogglePressed) {
            g_enableHiZCulling = !g_enableHiZCulling;
            std::cout << "Hi-Z Occlusion Culling: " << (g_enableHiZCulling ? "ON" : "OFF") << std::endl;
            hiZTogglePressed = true;
        }
    } else {
        hiZTogglePressed = false;
    }

    // SSAO toggle (F8)
    static bool ssaoTogglePressed = false;
    if (glfwGetKey(window, GLFW_KEY_F8) == GLFW_PRESS) {
        if (!ssaoTogglePressed) {
            g_enableSSAO = !g_enableSSAO;
            std::cout << "SSAO: " << (g_enableSSAO ? "ON" : "OFF") << std::endl;
            ssaoTogglePressed = true;
        }
    } else {
        ssaoTogglePressed = false;
    }

    // Debug mode cycle (F9)
    static bool debugTogglePressed = false;
    if (glfwGetKey(window, GLFW_KEY_F9) == GLFW_PRESS) {
        if (!debugTogglePressed) {
            g_deferredDebugMode = (g_deferredDebugMode + 1) % 5;
            const char* modeNames[] = {"Normal", "Albedo", "Normals", "Position", "Depth"};
            std::cout << "Debug mode: " << modeNames[g_deferredDebugMode] << std::endl;
            debugTogglePressed = true;
        }
    } else {
        debugTogglePressed = false;
    }

    // Performance stats toggle (F11)
    static bool perfStatsTogglePressed = false;
    if (glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS) {
        if (!perfStatsTogglePressed) {
            g_showPerfStats = !g_showPerfStats;
            std::cout << "Performance Stats: " << (g_showPerfStats ? "ON" : "OFF") << std::endl;
            perfStatsTogglePressed = true;
        }
    } else {
        perfStatsTogglePressed = false;
    }

    // FSR toggle (F12) - Note: Only affects runtime, FBOs are created at startup
    static bool fsrTogglePressed = false;
    if (glfwGetKey(window, GLFW_KEY_F12) == GLFW_PRESS) {
        if (!fsrTogglePressed) {
            // FSR can only be toggled if scene FBO was created at startup
            if (sceneFBO != 0) {
                g_enableFSR = !g_enableFSR;
                std::cout << "FSR Upscaling: " << (g_enableFSR ? "ON" : "OFF");
                if (g_enableFSR) {
                    std::cout << " (render " << RENDER_WIDTH << "x" << RENDER_HEIGHT << " -> " << WINDOW_WIDTH << "x" << WINDOW_HEIGHT << ")";
                }
                std::cout << std::endl;
            } else {
                std::cout << "FSR: Not available (enable in settings.cfg and restart)" << std::endl;
            }
            fsrTogglePressed = true;
        }
    } else {
        fsrTogglePressed = false;
    }

    // Mesh Shader toggle (M key) - NVIDIA Turing+ only
    static bool meshShaderTogglePressed = false;
    if (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS) {
        if (!meshShaderTogglePressed) {
            if (g_meshShadersAvailable && meshShaderProgram != 0) {
                g_enableMeshShaders = !g_enableMeshShaders;
                g_generateMeshlets = g_enableMeshShaders;
                std::cout << "Mesh Shaders: " << (g_enableMeshShaders ? "ON" : "OFF") << std::endl;
            } else {
                std::cout << "Mesh Shaders: Not available (requires NVIDIA Turing+ GPU)" << std::endl;
            }
            meshShaderTogglePressed = true;
        }
    } else {
        meshShaderTogglePressed = false;
    }

    // Batched Rendering toggle (B key) - Sodium-style column batching
    static bool batchedRenderingTogglePressed = false;
    if (glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS) {
        if (!batchedRenderingTogglePressed) {
            g_enableBatchedRendering = !g_enableBatchedRendering;
            std::cout << "Batched Rendering: " << (g_enableBatchedRendering ? "ON" : "OFF") << std::endl;
            batchedRenderingTogglePressed = true;
        }
    } else {
        batchedRenderingTogglePressed = false;
    }

    // Number keys for hotbar
    for (int i = 0; i < HOTBAR_SIZE && i < 9; i++) {
        if (glfwGetKey(window, GLFW_KEY_1 + i) == GLFW_PRESS) {
            selectedSlot = i;
            selectedBlock = hotbar[i];
        }
    }

    return input;
}

// Shader sources with day/night cycle support
const char* vertexShaderSource = R"(
#version 460 core
// Packed vertex format (16 bytes total - 3x smaller than before)
layout (location = 0) in vec3 aPackedPos;     // int16 * 3, scaled by 256
layout (location = 1) in vec2 aPackedTexCoord; // uint16 * 2, 8.8 fixed point
layout (location = 2) in uvec4 aPackedData;   // normalIndex, ao, light, texSlot

out vec2 texCoord;
out vec2 texSlotBase;  // Pass to fragment shader for tiling
out vec3 fragNormal;
out vec3 fragPos;
out float aoFactor;
out float lightLevel;
out float fogDepth;
out vec2 screenPos;
out vec4 fragPosLightSpace;

uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;
uniform vec3 chunkOffset;  // World position of chunk origin

// Normal lookup table (matches CPU-side NORMAL_LOOKUP)
const vec3 NORMALS[6] = vec3[6](
    vec3(1, 0, 0),   // 0: +X
    vec3(-1, 0, 0),  // 1: -X
    vec3(0, 1, 0),   // 2: +Y
    vec3(0, -1, 0),  // 3: -Y
    vec3(0, 0, 1),   // 4: +Z
    vec3(0, 0, -1)   // 5: -Z
);

// Texture atlas constants
const float ATLAS_SIZE = 16.0;
const float SLOT_SIZE = 1.0 / ATLAS_SIZE;

void main() {
    // Decode packed position (divide by 256 to get actual position, add chunk offset)
    vec3 worldPos = aPackedPos / 256.0 + chunkOffset;

    // Decode packed texcoord (8.8 fixed point - divide by 256)
    texCoord = aPackedTexCoord / 256.0;

    // Decode packed data
    uint normalIndex = aPackedData.x;
    uint aoValue = aPackedData.y;
    uint lightValue = aPackedData.z;
    uint texSlot = aPackedData.w;

    // Look up normal from table
    fragNormal = NORMALS[normalIndex];

    // Decode AO and light (0-255 to 0.0-1.0)
    aoFactor = float(aoValue) / 255.0;
    lightLevel = float(lightValue) / 255.0;

    // Calculate texture slot base UV from slot index
    float slotX = float(texSlot % 16u);
    float slotY = float(texSlot / 16u);
    texSlotBase = vec2(slotX * SLOT_SIZE, slotY * SLOT_SIZE);

    // Transform to clip space
    vec4 viewPos = view * vec4(worldPos, 1.0);
    gl_Position = projection * viewPos;

    fragPos = worldPos;
    fogDepth = length(viewPos.xyz);
    screenPos = gl_Position.xy / gl_Position.w;
    fragPosLightSpace = lightSpaceMatrix * vec4(worldPos, 1.0);
}
)";

const char* fragmentShaderSource = R"(
#version 460 core
in vec2 texCoord;
in vec2 texSlotBase;  // Base UV of texture slot for greedy meshing tiling
in vec3 fragNormal;
in vec3 fragPos;
in float aoFactor;
in float lightLevel;
in float fogDepth;
in vec2 screenPos;
in vec4 fragPosLightSpace;

out vec4 FragColor;

uniform sampler2D texAtlas;

// Texture atlas constants for greedy meshing tiling
const float ATLAS_SIZE = 16.0;
const float SLOT_SIZE = 1.0 / ATLAS_SIZE;  // 0.0625
uniform sampler2D shadowMap;
uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec3 ambientColor;
uniform vec3 skyColor;
uniform vec3 cameraPos;
uniform float fogDensity;
uniform float isUnderwater;
uniform float time;
uniform float shadowStrength;
uniform float renderDistanceBlocks;  // For LOD-hiding fog

// ============================================================
// Shadow Mapping with PCF (Percentage Closer Filtering)
// ============================================================
float calculateShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    // Perform perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;

    // Transform to [0,1] range
    projCoords = projCoords * 0.5 + 0.5;

    // Check if outside shadow map
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0) {
        return 0.0;  // Not in shadow
    }

    // Get current fragment depth
    float currentDepth = projCoords.z;

    // Calculate bias based on surface angle to light
    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001);

    // PCF - sample surrounding texels for soft shadows
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);

    for (int x = -2; x <= 2; x++) {
        for (int y = -2; y <= 2; y++) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 25.0;  // 5x5 kernel

    // Fade shadow at distance
    float distFade = smoothstep(100.0, 200.0, length(fragPos - cameraPos));
    shadow *= (1.0 - distFade);

    return shadow * shadowStrength;
}

// ============================================================
// Volumetric Fog System
// Height-based density with light scattering
// ============================================================

// Fog parameters
const float FOG_HEIGHT_FALLOFF = 0.015;   // How quickly fog thins with height
const float FOG_BASE_HEIGHT = 64.0;       // Sea level - fog is densest here
const float FOG_DENSITY_SCALE = 0.8;      // Overall fog intensity
const float FOG_INSCATTER_STRENGTH = 0.4; // Light scattering intensity

// Calculate fog density at a given height
float getFogDensity(float y) {
    // Exponential falloff above base height
    float heightAboveBase = max(y - FOG_BASE_HEIGHT, 0.0);
    float heightFactor = exp(-heightAboveBase * FOG_HEIGHT_FALLOFF);

    // Slightly denser below base height (valleys/water)
    float belowBase = max(FOG_BASE_HEIGHT - y, 0.0);
    float valleyFactor = 1.0 + belowBase * 0.02;

    return heightFactor * valleyFactor;
}

// Analytical integration of exponential height fog along a ray
// Based on: https://iquilezles.org/articles/fog/
// Enhanced with LOD-hiding fog that intensifies at render distance edge
vec2 computeVolumetricFog(vec3 rayStart, vec3 rayEnd, vec3 sunDir) {
    vec3 rayDir = rayEnd - rayStart;
    float rayLength = length(rayDir);

    if (rayLength < 0.001) return vec2(1.0, 0.0);

    rayDir /= rayLength;

    // Sample fog along the ray (simplified integration)
    const int FOG_STEPS = 8;
    float stepSize = rayLength / float(FOG_STEPS);

    float transmittance = 1.0;
    float inScatter = 0.0;

    // Henyey-Greenstein phase function approximation for forward scattering
    float cosTheta = dot(rayDir, sunDir);
    float g = 0.7; // Forward scattering bias
    float phase = (1.0 - g*g) / (4.0 * 3.14159 * pow(1.0 + g*g - 2.0*g*cosTheta, 1.5));

    for (int i = 0; i < FOG_STEPS; i++) {
        float t = (float(i) + 0.5) * stepSize;
        vec3 samplePos = rayStart + rayDir * t;

        // Get fog density at this height
        float density = getFogDensity(samplePos.y) * fogDensity * FOG_DENSITY_SCALE;

        // Beer's law extinction
        float extinction = exp(-density * stepSize * 2.0);

        // Light contribution at this point (simplified - assumes light reaches all points)
        // In reality would need shadow marching, but too expensive
        float heightLight = clamp((samplePos.y - FOG_BASE_HEIGHT) / 40.0 + 0.5, 0.3, 1.0);
        float lightContrib = phase * heightLight;

        // Accumulate in-scattering (light scattered toward camera)
        inScatter += transmittance * (1.0 - extinction) * lightContrib * FOG_INSCATTER_STRENGTH;

        // Update transmittance
        transmittance *= extinction;
    }

    // LOD-hiding fog: extra fog at 70-100% of render distance
    // This hides the LOD transition zone - Distant Horizons style
    float lodStartDist = renderDistanceBlocks * 0.7;
    float lodEndDist = renderDistanceBlocks;
    float lodFogFactor = smoothstep(lodStartDist, lodEndDist, rayLength);
    // Reduce transmittance by up to 40% at the far edge
    transmittance *= (1.0 - lodFogFactor * 0.4);

    return vec2(transmittance, inScatter);
}

// Check if texture coordinates indicate an emissive block
// Texture atlas is 16x16, each slot is 1/16 = 0.0625
// Glowstone = slot 22 (row 1, col 6), Lava = slot 23 (row 1, col 7)
float getEmission(vec2 uv) {
    float slotSize = 1.0 / 16.0;
    int col = int(uv.x / slotSize);
    int row = int(uv.y / slotSize);
    int slot = row * 16 + col;

    if (slot == 22) return 1.0;  // Glowstone
    if (slot == 23) return 0.95; // Lava
    return 0.0;
}

void main() {
    // Sample texture with greedy meshing tiling support
    // fract(texCoord) tiles within each block, then offset to correct atlas slot
    vec2 tiledUV = texSlotBase + fract(texCoord) * SLOT_SIZE;
    vec4 texColor = texture(texAtlas, tiledUV);

    // Discard very transparent pixels (for glass, leaves)
    if (texColor.a < 0.1) discard;

    // Check for emissive blocks (use texSlotBase to identify block type)
    float emission = getEmission(texSlotBase);

    // Lighting calculation
    vec3 norm = normalize(fragNormal);
    vec3 lightDirection = normalize(lightDir);

    // Calculate shadow
    float shadow = calculateShadow(fragPosLightSpace, norm, lightDirection);

    // Ambient lighting (sky contribution) - not affected by shadow
    vec3 ambient = ambientColor * 0.6;

    // Diffuse lighting (sun/moon) - affected by shadow
    float diff = max(dot(norm, lightDirection), 0.0);
    vec3 diffuse = diff * lightColor * 0.6 * (1.0 - shadow);

    // Point light contribution from emissive blocks (glowstone, lava)
    // Light level is 0-1, add warm colored light - not affected by shadow
    vec3 pointLight = lightLevel * vec3(1.0, 0.85, 0.6) * 1.2;

    // Combine lighting with smooth AO
    // Point lights are added on top (not multiplied by AO for better effect near sources)
    vec3 lighting = (ambient + diffuse) * aoFactor + pointLight;

    // Apply lighting to texture (emissive blocks ignore lighting and shadows)
    vec3 result;
    if (emission > 0.0) {
        // Emissive blocks glow with their natural color, plus a brightness boost
        vec3 glowColor = texColor.rgb * (1.5 + emission * 0.5);
        // Add slight pulsing effect for lava
        if (emission < 1.0) {
            float pulse = sin(time * 2.0) * 0.1 + 1.0;
            glowColor *= pulse;
        }
        result = glowColor;
    } else {
        result = texColor.rgb * lighting;
    }

    // Apply underwater effects (different fog system)
    if (isUnderwater > 0.5) {
        // Underwater uses simple dense fog
        float underwaterFogFactor = 1.0 - exp(-fogDensity * 16.0 * fogDepth * fogDepth);
        underwaterFogFactor = clamp(underwaterFogFactor, 0.0, 1.0);

        vec3 underwaterFogColor = vec3(0.05, 0.2, 0.35);
        result = mix(result, underwaterFogColor, underwaterFogFactor);

        // Strong blue-green color grading
        result = mix(result, result * vec3(0.4, 0.7, 0.9), 0.4);

        // Depth-based light absorption (deeper = darker and more blue)
        float depthDarkening = exp(-fogDepth * 0.02);
        result *= mix(vec3(0.3, 0.5, 0.7), vec3(1.0), depthDarkening);

        // Vignette effect (darker edges like diving mask)
        float vignette = 1.0 - length(screenPos) * 0.5;
        vignette = clamp(vignette, 0.0, 1.0);
        vignette = smoothstep(0.0, 1.0, vignette);
        result *= mix(0.4, 1.0, vignette);

        // Wavy light caustics effect
        float caustic1 = sin(fragPos.x * 3.0 + fragPos.z * 2.0 + time * 2.5) * 0.5 + 0.5;
        float caustic2 = sin(fragPos.x * 2.0 - fragPos.z * 3.0 + time * 1.8) * 0.5 + 0.5;
        float caustic3 = sin((fragPos.x + fragPos.z) * 4.0 + time * 3.2) * 0.5 + 0.5;
        float caustics = (caustic1 + caustic2 + caustic3) / 3.0;
        caustics = caustics * 0.25 + 0.85;
        result *= caustics;

        // Subtle color shimmer
        float shimmer = sin(fragPos.x * 5.0 + fragPos.y * 3.0 + time * 4.0) * 0.02;
        result.b += shimmer;
        result.g += shimmer * 0.5;
    } else {
        // Volumetric fog for above water
        vec2 fogResult = computeVolumetricFog(cameraPos, fragPos, lightDirection);
        float transmittance = fogResult.x;
        float inScatter = fogResult.y;

        // Emissive blocks resist fog - they pierce through it
        if (emission > 0.0) {
            transmittance = mix(transmittance, 1.0, emission * 0.7);
            inScatter *= (1.0 - emission * 0.5);
        }

        // Fog color based on sun position and sky
        float sunUp = max(lightDirection.y, 0.0);
        vec3 fogScatterColor = mix(
            vec3(0.9, 0.85, 0.7),   // Warm scattered light
            lightColor * 0.8,       // Sun color contribution
            sunUp * 0.5
        );

        // Blend fog color with sky color for ambient fog
        vec3 fogAmbientColor = mix(skyColor, fogScatterColor, 0.3);

        // Apply fog: attenuate object color and add in-scattered light
        result = result * transmittance + fogAmbientColor * (1.0 - transmittance) + fogScatterColor * inScatter;
    }

    FragColor = vec4(result, texColor.a);
}
)";

// Water vertex shader with noise-based wave animation
const char* waterVertexShaderSource = R"(
#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in float aAO;
layout (location = 4) in float aLightLevel;
layout (location = 5) in vec2 aTexSlotBase;  // Base UV of texture slot for greedy meshing

out vec2 texCoord;
out vec2 texSlotBase;
out vec3 fragNormal;
out vec3 fragPos;
out float aoFactor;
out float fogDepth;

uniform mat4 view;
uniform mat4 projection;
uniform float time;

// ============================================================
// Simplex 2D Noise for vertex displacement
// Simplified version for vertex shader performance
// ============================================================
vec3 permute(vec3 x) { return mod(((x*34.0)+1.0)*x, 289.0); }

float snoise(vec2 v) {
    const vec4 C = vec4(0.211324865405187, 0.366025403784439,
                        -0.577350269189626, 0.024390243902439);
    vec2 i  = floor(v + dot(v, C.yy));
    vec2 x0 = v - i + dot(i, C.xx);
    vec2 i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;
    i = mod(i, 289.0);
    vec3 p = permute(permute(i.y + vec3(0.0, i1.y, 1.0)) + i.x + vec3(0.0, i1.x, 1.0));
    vec3 m = max(0.5 - vec3(dot(x0,x0), dot(x12.xy,x12.xy), dot(x12.zw,x12.zw)), 0.0);
    m = m*m;
    m = m*m;
    vec3 x = 2.0 * fract(p * C.www) - 1.0;
    vec3 h = abs(x) - 0.5;
    vec3 ox = floor(x + 0.5);
    vec3 a0 = x - ox;
    m *= 1.79284291400159 - 0.85373472095314 * (a0*a0 + h*h);
    vec3 g;
    g.x = a0.x * x0.x + h.x * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g);
}

// Simplified FBM for vertex shader (fewer octaves for performance)
float fbmVertex(vec2 p, float t) {
    float value = 0.0;
    float amplitude = 0.5;
    mat2 rot = mat2(0.80, 0.60, -0.60, 0.80);

    // 3 octaves for vertex displacement
    for (int i = 0; i < 3; i++) {
        float speed = 0.4 + float(i) * 0.2;
        float dir = (mod(float(i), 2.0) == 0.0) ? 1.0 : -0.6;
        vec2 animP = p + t * speed * dir * vec2(0.3, 0.2);
        value += amplitude * snoise(animP);
        p = rot * p * 2.03;
        amplitude *= 0.5;
    }
    return value;
}

void main() {
    vec3 pos = aPos;

    // Only animate the top surface of water (normal pointing up)
    if (aNormal.y > 0.5) {
        vec2 samplePos = pos.xz;

        // Large gentle waves (slow, big motion)
        float largeWave = fbmVertex(samplePos * 0.06, time * 0.3) * 0.18;

        // Medium waves (different direction/speed)
        float medWave = snoise(samplePos * 0.12 + time * vec2(-0.2, 0.35)) * 0.10;

        // Small choppy waves (faster, adds detail)
        float smallWave = snoise(samplePos * 0.3 + time * vec2(0.5, -0.3)) * 0.05;

        // Very fine ripples
        float ripples = snoise(samplePos * 0.8 + time * vec2(-0.4, 0.6)) * 0.02;

        // Combine all wave layers
        pos.y += largeWave + medWave + smallWave + ripples;
    }

    vec4 viewPos = view * vec4(pos, 1.0);
    gl_Position = projection * viewPos;
    texCoord = aTexCoord;
    texSlotBase = aTexSlotBase;
    fragNormal = aNormal;
    fragPos = pos;
    aoFactor = aAO;
    fogDepth = length(viewPos.xyz);
}
)";

// Water fragment shader with enhanced underwater effects and seamless tiling
// Uses Simplex noise and FBM for natural-looking procedural water
// LOD system reduces quality for distant water to improve performance
const char* waterFragmentShaderSource = R"(
#version 460 core
in vec2 texCoord;
in vec2 texSlotBase;  // Base UV of texture slot for greedy meshing tiling
in vec3 fragNormal;
in vec3 fragPos;
in float aoFactor;
in float fogDepth;

out vec4 FragColor;

uniform sampler2D texAtlas;
uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec3 ambientColor;
uniform vec3 skyColor;
uniform vec3 cameraPos;
uniform float fogDensity;
uniform float isUnderwater;
uniform float time;
uniform vec4 waterTexBounds;
uniform float waterLodDistance;  // Distance threshold for LOD transitions

// Texture atlas constants for greedy meshing tiling
const float ATLAS_SIZE = 16.0;
const float SLOT_SIZE = 1.0 / ATLAS_SIZE;  // 0.0625

// ============================================================
// Volumetric Fog System (shared with main shader)
// ============================================================
const float FOG_HEIGHT_FALLOFF = 0.015;
const float FOG_BASE_HEIGHT = 64.0;
const float FOG_DENSITY_SCALE = 0.8;
const float FOG_INSCATTER_STRENGTH = 0.4;

float getFogDensityW(float y) {
    float heightAboveBase = max(y - FOG_BASE_HEIGHT, 0.0);
    float heightFactor = exp(-heightAboveBase * FOG_HEIGHT_FALLOFF);
    float belowBase = max(FOG_BASE_HEIGHT - y, 0.0);
    float valleyFactor = 1.0 + belowBase * 0.02;
    return heightFactor * valleyFactor;
}

// LOD-aware volumetric fog - fewer steps for distant water
vec2 computeVolumetricFogW(vec3 rayStart, vec3 rayEnd, vec3 sunDir, int fogSteps) {
    vec3 rayDir = rayEnd - rayStart;
    float rayLength = length(rayDir);
    if (rayLength < 0.001) return vec2(1.0, 0.0);
    rayDir /= rayLength;

    float stepSize = rayLength / float(fogSteps);
    float transmittance = 1.0;
    float inScatter = 0.0;

    float cosTheta = dot(rayDir, sunDir);
    float g = 0.7;
    float phase = (1.0 - g*g) / (4.0 * 3.14159 * pow(1.0 + g*g - 2.0*g*cosTheta, 1.5));

    for (int i = 0; i < fogSteps; i++) {
        float t = (float(i) + 0.5) * stepSize;
        vec3 samplePos = rayStart + rayDir * t;
        float density = getFogDensityW(samplePos.y) * fogDensity * FOG_DENSITY_SCALE;
        float extinction = exp(-density * stepSize * 2.0);
        float heightLight = clamp((samplePos.y - FOG_BASE_HEIGHT) / 40.0 + 0.5, 0.3, 1.0);
        float lightContrib = phase * heightLight;
        inScatter += transmittance * (1.0 - extinction) * lightContrib * FOG_INSCATTER_STRENGTH;
        transmittance *= extinction;
    }
    return vec2(transmittance, inScatter);
}

// ============================================================
// Simplex 2D Noise - by Ian McEwan, Stefan Gustavson
// https://gist.github.com/patriciogonzalezvivo/670c22f3966e662d2f83
// ============================================================
vec3 permute(vec3 x) { return mod(((x*34.0)+1.0)*x, 289.0); }

float snoise(vec2 v) {
    const vec4 C = vec4(0.211324865405187, 0.366025403784439,
                        -0.577350269189626, 0.024390243902439);
    vec2 i  = floor(v + dot(v, C.yy));
    vec2 x0 = v - i + dot(i, C.xx);
    vec2 i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;
    i = mod(i, 289.0);
    vec3 p = permute(permute(i.y + vec3(0.0, i1.y, 1.0)) + i.x + vec3(0.0, i1.x, 1.0));
    vec3 m = max(0.5 - vec3(dot(x0,x0), dot(x12.xy,x12.xy), dot(x12.zw,x12.zw)), 0.0);
    m = m*m;
    m = m*m;
    vec3 x = 2.0 * fract(p * C.www) - 1.0;
    vec3 h = abs(x) - 0.5;
    vec3 ox = floor(x + 0.5);
    vec3 a0 = x - ox;
    m *= 1.79284291400159 - 0.85373472095314 * (a0*a0 + h*h);
    vec3 g;
    g.x = a0.x * x0.x + h.x * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g);
}

// ============================================================
// FBM (Fractal Brownian Motion) with domain rotation
// Based on techniques from Inigo Quilez: https://iquilezles.org/articles/fbm/
// LOD-aware: octaves parameter controls quality
// ============================================================
float fbm(vec2 p, float t, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    // Rotation matrix to prevent pattern alignment between octaves
    mat2 rot = mat2(0.80, 0.60, -0.60, 0.80);  // ~37 degree rotation

    for (int i = 0; i < octaves; i++) {
        float timeOffset = t * (0.3 + float(i) * 0.15) * (mod(float(i), 2.0) == 0.0 ? 1.0 : -0.7);
        vec2 animatedP = p * frequency + vec2(timeOffset * 0.5, timeOffset * 0.3);

        value += amplitude * snoise(animatedP);

        p = rot * p;
        frequency *= 2.03;
        amplitude *= 0.49;
    }

    return value;
}

// Secondary FBM with different parameters for variety
float fbm2(vec2 p, float t, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 0.7;

    mat2 rot = mat2(0.70, 0.71, -0.71, 0.70);

    for (int i = 0; i < octaves; i++) {
        float timeOffset = t * (0.2 + float(i) * 0.1) * (mod(float(i), 2.0) == 0.0 ? -1.0 : 0.8);
        vec2 animatedP = p * frequency + vec2(timeOffset * -0.3, timeOffset * 0.6);

        value += amplitude * snoise(animatedP);

        p = rot * p;
        frequency *= 1.97;
        amplitude *= 0.52;
    }

    return value;
}

void main() {
    // Sample position for noise (world space XZ)
    vec2 pos = fragPos.xz;

    // ============================================================
    // LOD calculation based on distance from camera
    // ============================================================
    float distToCamera = length(fragPos - cameraPos);
    float lodFactor = clamp(distToCamera / waterLodDistance, 0.0, 1.0);

    // LOD levels:
    // 0.0-0.3: Full quality (5 octaves, all detail)
    // 0.3-0.6: Medium quality (3 octaves, no fine detail)
    // 0.6-1.0: Low quality (2 octaves, simple waves only)
    int mainOctaves = lodFactor < 0.3 ? 5 : (lodFactor < 0.6 ? 3 : 2);
    int secondaryOctaves = lodFactor < 0.3 ? 4 : (lodFactor < 0.6 ? 2 : 1);
    bool doFineDetail = lodFactor < 0.3;
    bool doSparkle = lodFactor < 0.5;

    // ============================================================
    // Layer multiple FBM noise patterns for complex water surface
    // LOD reduces octaves and skips fine detail for distant water
    // ============================================================

    // Large slow-moving waves (main water motion)
    float largeWaves = fbm(pos * 0.08, time * 0.4, mainOctaves) * 0.6;

    // Medium waves moving in different direction
    float mediumWaves = fbm2(pos * 0.15, time * 0.6, secondaryOctaves) * 0.3;

    // Small detail ripples (only for close water)
    float smallRipples = doFineDetail ? fbm(pos * 0.4, time * 1.2, 3) * 0.15 : 0.0;

    // Very fine surface detail (only for close water)
    float fineDetail = doFineDetail ? snoise(pos * 1.5 + time * vec2(0.3, -0.2)) * 0.08 : 0.0;

    // Combine all wave layers
    float combinedWaves = largeWaves + mediumWaves + smallRipples + fineDetail;

    // Normalize to 0-1 range (noise returns roughly -1 to 1)
    float wavePattern = combinedWaves * 0.5 + 0.5;
    wavePattern = clamp(wavePattern, 0.0, 1.0);

    // ============================================================
    // Water coloring based on wave patterns
    // ============================================================
    vec3 waterDeep = vec3(0.05, 0.20, 0.45);      // Deep blue in troughs
    vec3 waterMid = vec3(0.12, 0.35, 0.60);       // Mid blue
    vec3 waterSurface = vec3(0.25, 0.50, 0.75);   // Lighter blue at peaks
    vec3 waterHighlight = vec3(0.45, 0.70, 0.90); // Highlights/foam hints

    // Multi-step color blending based on wave height
    vec3 waterColor;
    if (wavePattern < 0.4) {
        waterColor = mix(waterDeep, waterMid, wavePattern / 0.4);
    } else if (wavePattern < 0.7) {
        waterColor = mix(waterMid, waterSurface, (wavePattern - 0.4) / 0.3);
    } else {
        waterColor = mix(waterSurface, waterHighlight, (wavePattern - 0.7) / 0.3);
    }

    // Add subtle sparkle effect at wave peaks using high-frequency noise (LOD: skip for distant water)
    if (doSparkle) {
        float sparkleNoise = snoise(pos * 3.0 + time * vec2(1.5, -1.2));
        float sparkle = smoothstep(0.7, 0.95, wavePattern) * smoothstep(0.5, 0.9, sparkleNoise) * 0.3;
        waterColor += vec3(sparkle);
    }

    vec4 texColor = vec4(waterColor, 0.78);  // Semi-transparent

    // Lighting calculation
    vec3 norm = normalize(fragNormal);
    vec3 lightDirection = normalize(lightDir);

    // Ambient lighting (sky contribution)
    vec3 ambient = ambientColor * 0.6;

    // Diffuse lighting (sun/moon)
    float diff = max(dot(norm, lightDirection), 0.0);
    vec3 diffuse = diff * lightColor * 0.6;

    // Combine lighting with smooth AO
    vec3 lighting = (ambient + diffuse) * aoFactor;

    // Apply lighting to texture
    vec3 result = texColor.rgb * lighting;

    // Apply underwater effects (different fog system)
    if (isUnderwater > 0.5) {
        // Underwater uses simple dense fog
        float underwaterFogFactor = 1.0 - exp(-fogDensity * 16.0 * fogDepth * fogDepth);
        underwaterFogFactor = clamp(underwaterFogFactor, 0.0, 1.0);

        vec3 underwaterFogColor = vec3(0.05, 0.2, 0.35);
        result = mix(result, underwaterFogColor, underwaterFogFactor);

        // Strong blue-green color grading
        result = mix(result, result * vec3(0.4, 0.7, 0.9), 0.4);

        // Depth-based light absorption
        float depthDarkening = exp(-fogDepth * 0.02);
        result *= mix(vec3(0.3, 0.5, 0.7), vec3(1.0), depthDarkening);

        // Wavy light caustics on water surfaces
        float caustic1 = sin(fragPos.x * 3.0 + fragPos.z * 2.0 + time * 2.5) * 0.5 + 0.5;
        float caustic2 = sin(fragPos.x * 2.0 - fragPos.z * 3.0 + time * 1.8) * 0.5 + 0.5;
        float caustics = (caustic1 + caustic2) / 2.0;
        caustics = caustics * 0.2 + 0.9;
        result *= caustics;
    } else {
        // Volumetric fog for above water (LOD: fewer steps for distant water)
        int fogSteps = lodFactor < 0.3 ? 6 : (lodFactor < 0.6 ? 4 : 2);
        vec2 fogResult = computeVolumetricFogW(cameraPos, fragPos, lightDirection, fogSteps);
        float transmittance = fogResult.x;
        float inScatter = fogResult.y;

        // Fog color based on sun position and sky
        float sunUp = max(lightDirection.y, 0.0);
        vec3 fogScatterColor = mix(
            vec3(0.9, 0.85, 0.7),
            lightColor * 0.8,
            sunUp * 0.5
        );
        vec3 fogAmbientColor = mix(skyColor, fogScatterColor, 0.3);

        // Apply fog: attenuate object color and add in-scattered light
        result = result * transmittance + fogAmbientColor * (1.0 - transmittance) + fogScatterColor * inScatter;
    }

    FragColor = vec4(result, texColor.a);
}
)";

// ============================================================
// Sky/Cloud Shaders - Volumetric ray marched clouds
// ============================================================
const char* skyVertexShaderSource = R"(
#version 460 core
layout (location = 0) in vec2 aPos;

out vec2 screenPos;

void main() {
    screenPos = aPos;
    gl_Position = vec4(aPos, 0.9999, 1.0);  // Far plane
}
)";

const char* skyFragmentShaderSource = R"(
#version 460 core
in vec2 screenPos;
out vec4 FragColor;

uniform mat4 invView;
uniform mat4 invProjection;
uniform vec3 cameraPos;
uniform vec3 sunDirection;
uniform vec3 skyColorTop;
uniform vec3 skyColorBottom;
uniform float time;
uniform int cloudStyle;  // 0 = simple, 1 = volumetric
uniform float cloudRenderDistance;  // Limit cloud rendering to render distance

// Simple cloud settings (3D rounded shapes)
const float SIMPLE_CLOUD_MIN = 110.0;
const float SIMPLE_CLOUD_MAX = 160.0;     // 50 block thickness for puffy clouds
const float SIMPLE_CLOUD_THICKNESS = 50.0;
const int SIMPLE_CLOUD_STEPS = 12;        // More steps for better 3D shapes
const float SIMPLE_CLOUD_SCALE = 0.012;   // Scale for cloud size

// Volumetric cloud settings
const float CLOUD_MIN = 100.0;
const float CLOUD_MAX = 220.0;
const float CLOUD_THICKNESS = 120.0;
const int CLOUD_STEPS = 40;
const int LIGHT_STEPS = 5;
const float CLOUD_DENSITY = 0.25;
const float CLOUD_COVERAGE = 0.35;
const float ABSORPTION = 0.45;
const float SCATTERING_FORWARD = 0.75;
const float SCATTERING_BACK = 0.25;
const float AMBIENT_STRENGTH = 0.6;
const float CLOUD_SCALE = 0.003;

// ============================================================
// 2D Simplex Noise (for simple clouds)
// ============================================================
vec3 permute(vec3 x) { return mod(((x*34.0)+1.0)*x, 289.0); }

float snoise2D(vec2 v) {
    const vec4 C = vec4(0.211324865405187, 0.366025403784439,
                        -0.577350269189626, 0.024390243902439);
    vec2 i = floor(v + dot(v, C.yy));
    vec2 x0 = v - i + dot(i, C.xx);
    vec2 i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;
    i = mod(i, 289.0);
    vec3 p = permute(permute(i.y + vec3(0.0, i1.y, 1.0)) + i.x + vec3(0.0, i1.x, 1.0));
    vec3 m = max(0.5 - vec3(dot(x0,x0), dot(x12.xy,x12.xy), dot(x12.zw,x12.zw)), 0.0);
    m = m*m;
    m = m*m;
    vec3 x = 2.0 * fract(p * C.www) - 1.0;
    vec3 h = abs(x) - 0.5;
    vec3 ox = floor(x + 0.5);
    vec3 a0 = x - ox;
    m *= 1.79284291400159 - 0.85373472095314 * (a0*a0 + h*h);
    vec3 g;
    g.x = a0.x * x0.x + h.x * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g);
}

// ============================================================
// 3D Simplex Noise (for volumetric clouds)
// ============================================================
vec4 permute4(vec4 x) { return mod(((x*34.0)+1.0)*x, 289.0); }
vec4 taylorInvSqrt(vec4 r) { return 1.79284291400159 - 0.85373472095314 * r; }

float snoise3D(vec3 v) {
    const vec2 C = vec2(1.0/6.0, 1.0/3.0);
    const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);

    vec3 i = floor(v + dot(v, C.yyy));
    vec3 x0 = v - i + dot(i, C.xxx);

    vec3 g = step(x0.yzx, x0.xyz);
    vec3 l = 1.0 - g;
    vec3 i1 = min(g.xyz, l.zxy);
    vec3 i2 = max(g.xyz, l.zxy);

    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + C.yyy;
    vec3 x3 = x0 - D.yyy;

    i = mod(i, 289.0);
    vec4 p = permute4(permute4(permute4(
        i.z + vec4(0.0, i1.z, i2.z, 1.0))
        + i.y + vec4(0.0, i1.y, i2.y, 1.0))
        + i.x + vec4(0.0, i1.x, i2.x, 1.0));

    float n_ = 1.0/7.0;
    vec3 ns = n_ * D.wyz - D.xzx;
    vec4 j = p - 49.0 * floor(p * ns.z * ns.z);
    vec4 x_ = floor(j * ns.z);
    vec4 y_ = floor(j - 7.0 * x_);
    vec4 x = x_ * ns.x + ns.yyyy;
    vec4 y = y_ * ns.x + ns.yyyy;
    vec4 h = 1.0 - abs(x) - abs(y);
    vec4 b0 = vec4(x.xy, y.xy);
    vec4 b1 = vec4(x.zw, y.zw);
    vec4 s0 = floor(b0)*2.0 + 1.0;
    vec4 s1 = floor(b1)*2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));
    vec4 a0 = b0.xzyw + s0.xzyw*sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw*sh.zzww;
    vec3 p0 = vec3(a0.xy, h.x);
    vec3 p1 = vec3(a0.zw, h.y);
    vec3 p2 = vec3(a1.xy, h.z);
    vec3 p3 = vec3(a1.zw, h.w);
    vec4 norm = taylorInvSqrt(vec4(dot(p0,p0), dot(p1,p1), dot(p2,p2), dot(p3,p3)));
    p0 *= norm.x; p1 *= norm.y; p2 *= norm.z; p3 *= norm.w;
    vec4 m = max(0.6 - vec4(dot(x0,x0), dot(x1,x1), dot(x2,x2), dot(x3,x3)), 0.0);
    m = m * m;
    return 42.0 * dot(m*m, vec4(dot(p0,x0), dot(p1,x1), dot(p2,x2), dot(p3,x3)));
}
)" R"(
// ============================================================
// Simple Minecraft-style clouds (3D rounded puffy shapes)
// ============================================================
float getSimpleCloudDensity3D(vec3 pos) {
    // Slow wind animation
    vec3 windOffset = vec3(time * 2.0, 0.0, time * 0.8);

    // UNIFORM 3D scaling - this is key for rounded shapes
    vec3 samplePos = (pos + windOffset) * SIMPLE_CLOUD_SCALE;

    // Multi-octave 3D noise for puffy, rounded shapes
    float n1 = snoise3D(samplePos) * 0.5;
    float n2 = snoise3D(samplePos * 2.02 + vec3(50.0, 30.0, 80.0)) * 0.25;
    float n3 = snoise3D(samplePos * 4.01 + vec3(100.0, 60.0, 40.0)) * 0.125;
    float n4 = snoise3D(samplePos * 8.03 + vec3(25.0, 90.0, 120.0)) * 0.0625;

    float noise = n1 + n2 + n3 + n4;

    // Height profile for puffy cumulus shape:
    // - Flat bottom (sharp cutoff)
    // - Rounded puffy top
    float heightNorm = (pos.y - SIMPLE_CLOUD_MIN) / SIMPLE_CLOUD_THICKNESS;

    // Sharp flat bottom, gradual rounded top
    float bottomCutoff = smoothstep(0.0, 0.1, heightNorm);
    float topRoundoff = 1.0 - pow(max(heightNorm - 0.3, 0.0) / 0.7, 2.0);
    topRoundoff = max(topRoundoff, 0.0);

    float heightProfile = bottomCutoff * topRoundoff;

    // Cloud coverage threshold - creates distinct puffy shapes
    float baseThreshold = 0.1;

    // Make threshold vary with height to create rounded tops
    // Higher threshold at top = clouds taper off into round shapes
    float threshold = baseThreshold + heightNorm * 0.15;

    float density = smoothstep(threshold, threshold + 0.2, noise) * heightProfile;

    // Boost density for more solid-looking clouds
    density = pow(density, 0.8) * 1.2;

    return clamp(density, 0.0, 1.0);
}

vec4 renderSimpleClouds(vec3 rayDir) {
    // Handle ray intersection with cloud layer from any direction
    float tMin, tMax;

    // Check if ray is nearly horizontal
    if (abs(rayDir.y) < 0.001) {
        // Horizontal ray - only hits clouds if we're inside the layer
        if (cameraPos.y < SIMPLE_CLOUD_MIN || cameraPos.y > SIMPLE_CLOUD_MAX) {
            return vec4(0.0);
        }
        tMin = 0.0;
        tMax = 3000.0;
    } else {
        // Calculate intersection with both planes
        float t1 = (SIMPLE_CLOUD_MIN - cameraPos.y) / rayDir.y;
        float t2 = (SIMPLE_CLOUD_MAX - cameraPos.y) / rayDir.y;

        tMin = min(t1, t2);
        tMax = max(t1, t2);

        // If we're inside the cloud layer, start from camera
        if (cameraPos.y >= SIMPLE_CLOUD_MIN && cameraPos.y <= SIMPLE_CLOUD_MAX) {
            tMin = 0.0;
        }

        // Clamp to positive (in front of camera)
        tMin = max(tMin, 0.0);
        tMax = max(tMax, 0.0);
    }

    // No valid intersection
    if (tMax <= tMin) return vec4(0.0);

    // Limit draw distance to render distance (in blocks, convert to world units)
    float maxCloudDist = cloudRenderDistance * 16.0;  // chunks to blocks
    if (tMin > maxCloudDist) return vec4(0.0);
    tMax = min(tMax, min(tMin + 400.0, maxCloudDist));

    // Ray march through cloud layer
    float stepSize = (tMax - tMin) / float(SIMPLE_CLOUD_STEPS);

    // Add jitter to reduce banding
    float jitter = fract(sin(dot(screenPos, vec2(12.9898, 78.233))) * 43758.5453);
    float t = tMin + stepSize * jitter * 0.5;

    float transmittance = 1.0;
    vec3 lightAccum = vec3(0.0);

    // Cloud colors - bright white with subtle blue shadow
    vec3 cloudBright = vec3(1.0, 1.0, 1.0);
    vec3 cloudShadow = vec3(0.75, 0.8, 0.9);

    for (int i = 0; i < SIMPLE_CLOUD_STEPS; i++) {
        vec3 pos = cameraPos + rayDir * t;
        float density = getSimpleCloudDensity3D(pos);

        if (density > 0.01) {
            // Sample density slightly toward sun for self-shadowing
            vec3 lightSamplePos = pos + sunDirection * 8.0;
            float lightDensity = getSimpleCloudDensity3D(lightSamplePos);
            float shadowAmount = exp(-lightDensity * 2.0);

            // Height-based lighting (brighter at top)
            float heightNorm = (pos.y - SIMPLE_CLOUD_MIN) / SIMPLE_CLOUD_THICKNESS;
            float heightLight = 0.5 + 0.5 * heightNorm;

            // Combine shadow and height lighting
            float totalLight = shadowAmount * 0.7 + heightLight * 0.3;

            // Sun contribution based on sun angle
            float sunUp = max(sunDirection.y, 0.0);
            totalLight *= 0.7 + 0.3 * sunUp;

            vec3 cloudColor = mix(cloudShadow, cloudBright, totalLight);

            // Beer's law absorption
            float absorption = exp(-density * stepSize * 3.0);
            float alpha = 1.0 - absorption;

            lightAccum += transmittance * cloudColor * alpha;
            transmittance *= absorption;

            if (transmittance < 0.02) break;
        }

        t += stepSize;
    }

    // Distance fade
    float distFade = 1.0 - smoothstep(1500.0, 2500.0, tMin);

    float finalAlpha = (1.0 - transmittance) * distFade;
    vec3 finalColor = lightAccum / max(1.0 - transmittance, 0.001);

    return vec4(finalColor, finalAlpha);
}

// ============================================================
// Volumetric cloud functions
// ============================================================
float fbmClouds(vec3 p) {
    float value = 0.0;
    float amplitude = 0.55;
    float frequency = 1.0;
    mat3 rot = mat3(0.80, 0.60, 0.00, -0.60, 0.80, 0.00, 0.00, 0.00, 1.00);
    for (int i = 0; i < 6; i++) {
        value += amplitude * snoise3D(p * frequency);
        p = rot * p;
        frequency *= 1.95;
        amplitude *= 0.55;
    }
    return value;
}

float getVolCloudDensity(vec3 p) {
    vec3 windOffset = vec3(time * 1.2, 0.0, time * 0.5);  // Halved speed
    vec3 samplePos = (p + windOffset) * CLOUD_SCALE;
    float density = fbmClouds(samplePos);
    float heightFactor = (p.y - CLOUD_MIN) / CLOUD_THICKNESS;
    float bottomFalloff = smoothstep(0.0, 0.15, heightFactor);
    float topFalloff = smoothstep(1.0, 0.4, heightFactor);
    float cumulusProfile = pow(bottomFalloff * topFalloff, 0.7);
    density = (density - CLOUD_COVERAGE) * cumulusProfile;
    density = max(density, 0.0) * CLOUD_DENSITY;
    return pow(max(density, 0.0), 0.85);
}

vec2 rayBoxIntersect(vec3 ro, vec3 rd, float minY, float maxY) {
    float tMin = (minY - ro.y) / rd.y;
    float tMax = (maxY - ro.y) / rd.y;
    if (tMin > tMax) { float temp = tMin; tMin = tMax; tMax = temp; }
    return vec2(max(tMin, 0.0), max(tMax, 0.0));
}

float henyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159 * pow(1.0 + g2 - 2.0*g*cosTheta, 1.5));
}

float cloudPhase(float cosTheta) {
    return mix(henyeyGreenstein(cosTheta, -SCATTERING_BACK),
               henyeyGreenstein(cosTheta, SCATTERING_FORWARD), 0.7);
}

float lightMarch(vec3 pos) {
    float totalDensity = 0.0;
    float stepSize = CLOUD_THICKNESS / float(LIGHT_STEPS);
    for (int i = 0; i < LIGHT_STEPS; i++) {
        pos += sunDirection * stepSize;
        if (pos.y > CLOUD_MAX || pos.y < CLOUD_MIN) break;
        totalDensity += getVolCloudDensity(pos) * stepSize;
    }
    return exp(-totalDensity * ABSORPTION);
}

vec4 renderVolumetricClouds(vec3 rayDir) {
    // Early out for rays pointing too far down
    if (rayDir.y <= -0.1) return vec4(0.0);

    vec2 tCloud = rayBoxIntersect(cameraPos, rayDir, CLOUD_MIN, CLOUD_MAX);
    if (tCloud.y <= tCloud.x) return vec4(0.0);

    // Limit to render distance
    float maxCloudDist = cloudRenderDistance * 16.0;
    if (tCloud.x > maxCloudDist) return vec4(0.0);

    float tStart = tCloud.x;
    float tEnd = min(tCloud.y, min(tCloud.x + 500.0, maxCloudDist));

    // OPTIMIZATION: Adaptive step count based on distance
    // Closer clouds get more samples for quality, distant clouds fewer
    float distanceFactor = clamp(tStart / 500.0, 0.0, 1.0);
    int adaptiveSteps = int(mix(float(CLOUD_STEPS), float(CLOUD_STEPS / 2), distanceFactor));
    float stepSize = (tEnd - tStart) / float(adaptiveSteps);

    // Blue noise dithering for reduced banding
    float blueNoise = fract(sin(dot(screenPos, vec2(12.9898, 78.233))) * 43758.5453);
    float t = tStart + stepSize * blueNoise;

    float transmittance = 1.0;
    vec3 lightEnergy = vec3(0.0);
    float cosTheta = dot(rayDir, sunDirection);
    float phase = cloudPhase(cosTheta);

    vec3 sunLight = vec3(1.0, 0.98, 0.9);
    vec3 ambientLight = skyColorTop * 0.8;
    vec3 cloudBase = vec3(1.0);
    vec3 cloudShadow = vec3(0.7, 0.75, 0.85);

    for (int i = 0; i < CLOUD_STEPS; i++) {
        // OPTIMIZATION: More aggressive early termination
        if (transmittance < 0.03) break;
        if (i >= adaptiveSteps) break;

        vec3 pos = cameraPos + rayDir * t;
        float density = getVolCloudDensity(pos);

        if (density > 0.001) {
            float lightTransmittance = lightMarch(pos);
            float heightGrad = clamp((pos.y - CLOUD_MIN) / CLOUD_THICKNESS, 0.0, 1.0);
            vec3 directLight = sunLight * lightTransmittance * phase * 2.0;
            vec3 ambient = ambientLight * AMBIENT_STRENGTH * (0.5 + 0.5 * heightGrad);
            vec3 cloudCol = mix(cloudShadow, cloudBase, lightTransmittance);
            cloudCol += vec3(1.0, 0.95, 0.9) * pow(max(cosTheta, 0.0), 2.0) * (1.0 - lightTransmittance) * 0.5;
            vec3 sampleColor = cloudCol * (directLight + ambient);
            float beers = exp(-density * stepSize * ABSORPTION);
            float powder = 1.0 - exp(-density * stepSize * 2.0);
            float sampleTransmit = mix(beers, beers * powder, 0.5);
            lightEnergy += transmittance * sampleColor * density * stepSize;
            transmittance *= sampleTransmit;
        }
        t += stepSize;
    }
    return vec4(lightEnergy, 1.0 - transmittance);
}

// ============================================================
// Star field generation
// ============================================================
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float hash3(vec3 p) {
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
}

vec3 renderStars(vec3 rayDir) {
    // Only show stars when looking up
    if (rayDir.y < 0.0) return vec3(0.0);

    vec3 stars = vec3(0.0);
    vec3 starDir = normalize(rayDir);

    // OPTIMIZATION: Use 2D spherical coordinates instead of 3D grid
    // This reduces from 81 iterations to 18 (2 layers × 9 cells)
    float phi = atan(starDir.z, starDir.x);  // Azimuth
    float theta = acos(starDir.y);            // Polar angle

    // Two star layers for depth
    for (int layer = 0; layer < 2; layer++) {
        float scale = 60.0 + float(layer) * 30.0;
        vec2 starUV = vec2(phi, theta) * scale;
        vec2 cell = floor(starUV);

        // Check 3x3 neighborhood (9 cells instead of 27)
        for (int x = -1; x <= 1; x++) {
            for (int y = -1; y <= 1; y++) {
                vec2 neighbor = cell + vec2(x, y);
                vec2 cellHash = neighbor + float(layer) * 100.0;

                // Random star presence - ~4% of cells have stars
                float h = hash(cellHash);
                if (h > 0.96) {
                    // Star position within cell
                    vec2 starCenter = neighbor + vec2(
                        hash(cellHash + vec2(1.0, 0.0)),
                        hash(cellHash + vec2(0.0, 1.0))
                    );

                    float dist = length(starUV - starCenter);
                    float starSize = 0.12 + hash(cellHash + vec2(5.0)) * 0.18;

                    if (dist < starSize) {
                        // Star brightness with twinkle
                        float twinkle = sin(time * (2.0 + h * 4.0) + h * 6.28) * 0.3 + 0.7;
                        float brightness = (1.0 - dist / starSize) * twinkle;
                        brightness = brightness * brightness;  // Squared falloff

                        // Star color
                        float colorHash = hash(cellHash + vec2(10.0));
                        vec3 starColor = vec3(1.0);
                        if (colorHash > 0.85) starColor = vec3(1.0, 0.8, 0.6);       // Orange
                        else if (colorHash > 0.7) starColor = vec3(0.8, 0.9, 1.0);   // Blue-white

                        stars += starColor * brightness * 0.9;
                    }
                }
            }
        }
    }

    return stars;
}

void main() {
    // Reconstruct ray direction
    vec4 clipPos = vec4(screenPos, 1.0, 1.0);
    vec4 viewPos = invProjection * clipPos;
    viewPos = vec4(viewPos.xy, -1.0, 0.0);
    vec3 rayDir = normalize((invView * viewPos).xyz);

    // Sky gradient
    float skyGradient = clamp(rayDir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 sky = mix(skyColorBottom, skyColorTop, pow(skyGradient, 0.7));

    // Sun
    float sunDot = dot(rayDir, sunDirection);
    float sunDisc = smoothstep(0.9985, 0.9995, sunDot);
    vec3 sunColor = vec3(1.0, 0.95, 0.8) * 2.0;
    sky += vec3(1.0, 0.8, 0.5) * pow(max(sunDot, 0.0), 8.0) * 0.3;

    // Stars (only visible at night)
    float nightFactor = 1.0 - smoothstep(-0.1, 0.2, sunDirection.y);  // Fade as sun rises
    if (nightFactor > 0.01) {
        vec3 stars = renderStars(rayDir);
        sky += stars * nightFactor;
    }

    // Moon (opposite side of sun)
    vec3 moonDir = -sunDirection;
    float moonDot = dot(rayDir, moonDir);
    float moonDisc = smoothstep(0.998, 0.9995, moonDot);
    vec3 moonColor = vec3(0.9, 0.9, 1.0) * 0.8;
    sky += moonDisc * moonColor * nightFactor;

    // Render clouds based on style
    vec4 cloudColor;
    if (cloudStyle == 0) {
        cloudColor = renderSimpleClouds(rayDir);
    } else {
        cloudColor = renderVolumetricClouds(rayDir);
    }

    // Composite
    vec3 finalColor = mix(sky, cloudColor.rgb, cloudColor.a);
    finalColor += sunDisc * sunColor * (1.0 - cloudColor.a * 0.8);

    FragColor = vec4(finalColor, 1.0);
}
)";

// ============================================================
// Precipitation Shader - Rain and Snow particles
// ============================================================
const char* precipVertexShaderSource = R"(
#version 460 core
layout (location = 0) in vec3 aPos;      // Particle position
layout (location = 1) in float aSize;    // Particle size
layout (location = 2) in float aAlpha;   // Particle alpha

out float vAlpha;
out float vSize;
out vec2 vScreenPos;

uniform mat4 view;
uniform mat4 projection;
uniform float time;
uniform int weatherType;  // 1 = rain, 2 = snow, 3 = thunderstorm

void main() {
    vec3 pos = aPos;

    // Animation based on weather type
    if (weatherType == 2) {
        // Snow - gentle swaying motion
        float sway = sin(time * 0.8 + pos.x * 0.5) * 0.3 +
                     cos(time * 0.6 + pos.z * 0.4) * 0.2;
        pos.x += sway;
        pos.z += cos(time * 0.5 + pos.x * 0.3) * 0.15;
    }

    vec4 viewPos = view * vec4(pos, 1.0);
    gl_Position = projection * viewPos;

    // Size attenuation based on distance
    float dist = length(viewPos.xyz);
    float sizeScale = 300.0 / max(dist, 1.0);
    gl_PointSize = aSize * sizeScale;

    vAlpha = aAlpha;
    vSize = aSize;
    vScreenPos = gl_Position.xy / gl_Position.w;
}
)";

const char* precipFragmentShaderSource = R"(
#version 460 core
in float vAlpha;
in float vSize;
in vec2 vScreenPos;

out vec4 FragColor;

uniform int weatherType;  // 1 = rain, 2 = snow, 3 = thunderstorm
uniform float intensity;
uniform vec3 lightColor;

void main() {
    vec2 coord = gl_PointCoord * 2.0 - 1.0;

    if (weatherType == 2) {
        // Snow - soft circular flakes
        float dist = length(coord);
        float alpha = 1.0 - smoothstep(0.3, 1.0, dist);

        // Subtle sparkle
        float sparkle = max(0.0, sin(coord.x * 10.0) * sin(coord.y * 10.0)) * 0.3;

        vec3 snowColor = vec3(0.95, 0.97, 1.0) + sparkle;
        FragColor = vec4(snowColor * lightColor, alpha * vAlpha * intensity * 0.8);
    } else {
        // Rain - elongated streaks
        float rainShape = abs(coord.x) * 4.0 + abs(coord.y - 0.3) * 0.5;
        float alpha = 1.0 - smoothstep(0.0, 1.0, rainShape);

        // Slight blue tint for rain
        vec3 rainColor = vec3(0.7, 0.8, 0.95);
        FragColor = vec4(rainColor * lightColor, alpha * vAlpha * intensity * 0.6);
    }

    if (FragColor.a < 0.01) discard;
}
)";

// Shadow map vertex shader - renders scene from light's perspective
const char* shadowVertexShaderSource = R"(
#version 460 core
layout (location = 0) in vec3 aPackedPos;  // Packed int16 positions
layout (location = 1) in vec2 aPackedTexCoord;  // Not used for shadows
layout (location = 2) in uvec4 aPackedData;  // Not used for shadows

uniform mat4 lightSpaceMatrix;
uniform vec3 chunkOffset;

void main() {
    vec3 worldPos = aPackedPos / 256.0 + chunkOffset;
    gl_Position = lightSpaceMatrix * vec4(worldPos, 1.0);
}
)";

// Shadow map fragment shader - just outputs depth (done automatically)
const char* shadowFragmentShaderSource = R"(
#version 460 core

void main() {
    // Depth is written automatically
}
)";

// ============================================
// Z-PREPASS SHADERS (eliminates overdraw)
// ============================================

// Z-prepass vertex shader - minimal, just outputs depth
const char* zPrepassVertexSource = R"(
#version 460 core
layout (location = 0) in vec3 aPackedPos;
layout (location = 1) in vec2 aPackedTexCoord;  // For alpha testing
layout (location = 2) in uvec4 aPackedData;

out vec2 texCoord;
out vec2 texSlotBase;

uniform mat4 view;
uniform mat4 projection;
uniform vec3 chunkOffset;

const float ATLAS_SIZE = 16.0;
const float SLOT_SIZE = 1.0 / ATLAS_SIZE;

void main() {
    vec3 worldPos = aPackedPos / 256.0 + chunkOffset;
    gl_Position = projection * view * vec4(worldPos, 1.0);

    // Pass texture coords for alpha testing
    texCoord = aPackedTexCoord / 256.0;
    uint texSlot = aPackedData.w;
    float slotX = float(texSlot % 16u);
    float slotY = float(texSlot / 16u);
    texSlotBase = vec2(slotX * SLOT_SIZE, slotY * SLOT_SIZE);
}
)";

// Z-prepass fragment shader - only for alpha testing
const char* zPrepassFragmentSource = R"(
#version 460 core

in vec2 texCoord;
in vec2 texSlotBase;

uniform sampler2D texAtlas;

const float SLOT_SIZE = 1.0 / 16.0;

void main() {
    vec2 tiledUV = texSlotBase + fract(texCoord) * SLOT_SIZE;
    float alpha = texture(texAtlas, tiledUV).a;
    if (alpha < 0.1) discard;
    // Depth is written automatically
}
)";

// Loading screen vertex shader
const char* loadingVertexShaderSource = R"(
#version 460 core

layout (location = 0) in vec2 aPos;

uniform vec2 uOffset;
uniform vec2 uScale;

void main() {
    vec2 pos = aPos * uScale + uOffset;
    gl_Position = vec4(pos, 0.0, 1.0);
}
)";

// Loading screen fragment shader
const char* loadingFragmentShaderSource = R"(
#version 460 core

out vec4 FragColor;
uniform vec3 uColor;

void main() {
    FragColor = vec4(uColor, 1.0);
}
)";

// ============================================
// DEFERRED RENDERING SHADERS
// ============================================

// G-Buffer geometry pass vertex shader
const char* gBufferVertexSource = R"(
#version 460 core
layout (location = 0) in vec3 aPackedPos;
layout (location = 1) in vec2 aPackedTexCoord;
layout (location = 2) in uvec4 aPackedData;

out vec3 fragPos;
out vec3 fragNormal;
out vec2 texCoord;
out vec2 texSlotBase;
out float aoFactor;
out float lightLevel;
out float viewDepth;

uniform mat4 view;
uniform mat4 projection;
uniform vec3 chunkOffset;

const vec3 NORMALS[6] = vec3[6](
    vec3(1, 0, 0), vec3(-1, 0, 0),
    vec3(0, 1, 0), vec3(0, -1, 0),
    vec3(0, 0, 1), vec3(0, 0, -1)
);

const float ATLAS_SIZE = 16.0;
const float SLOT_SIZE = 1.0 / ATLAS_SIZE;

void main() {
    vec3 worldPos = aPackedPos / 256.0 + chunkOffset;
    texCoord = aPackedTexCoord / 256.0;

    uint normalIndex = aPackedData.x;
    uint aoValue = aPackedData.y;
    uint lightValue = aPackedData.z;
    uint texSlot = aPackedData.w;

    fragNormal = NORMALS[normalIndex];
    aoFactor = float(aoValue) / 255.0;
    lightLevel = float(lightValue) / 255.0;

    float slotX = float(texSlot % 16u);
    float slotY = float(texSlot / 16u);
    texSlotBase = vec2(slotX * SLOT_SIZE, slotY * SLOT_SIZE);

    vec4 viewPos = view * vec4(worldPos, 1.0);
    gl_Position = projection * viewPos;
    fragPos = worldPos;
    viewDepth = -viewPos.z;  // Positive depth in view space
}
)";

// G-Buffer geometry pass fragment shader
const char* gBufferFragmentSource = R"(
#version 460 core
layout (location = 0) out vec4 gPosition;  // xyz = world pos, w = AO
layout (location = 1) out vec4 gNormal;    // xyz = normal, w = light level
layout (location = 2) out vec4 gAlbedo;    // rgb = albedo, a = emission

in vec3 fragPos;
in vec3 fragNormal;
in vec2 texCoord;
in vec2 texSlotBase;
in float aoFactor;
in float lightLevel;
in float viewDepth;

uniform sampler2D texAtlas;

const float ATLAS_SIZE = 16.0;
const float SLOT_SIZE = 1.0 / ATLAS_SIZE;

float getEmission(vec2 uv) {
    int col = int(uv.x / SLOT_SIZE);
    int row = int(uv.y / SLOT_SIZE);
    int slot = row * 16 + col;
    if (slot == 22) return 1.0;  // Glowstone
    if (slot == 23) return 0.95; // Lava
    return 0.0;
}

void main() {
    vec2 tiledUV = texSlotBase + fract(texCoord) * SLOT_SIZE;
    vec4 texColor = texture(texAtlas, tiledUV);

    if (texColor.a < 0.1) discard;

    float emission = getEmission(texSlotBase);

    gPosition = vec4(fragPos, aoFactor);
    gNormal = vec4(normalize(fragNormal), lightLevel);
    gAlbedo = vec4(texColor.rgb, emission);
}
)";

// Composite/deferred lighting vertex shader
const char* compositeVertexSource = R"(
#version 460 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoords;

void main() {
    TexCoords = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// Composite/deferred lighting fragment shader
const char* compositeFragmentSource = R"(
#version 460 core
out vec4 FragColor;

in vec2 TexCoords;

// G-Buffer textures
uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedo;
uniform sampler2D gDepth;

// SSAO
uniform sampler2D ssaoTexture;
uniform bool enableSSAO;

// Cascade shadow maps
uniform sampler2DArrayShadow cascadeShadowMaps;
uniform mat4 cascadeMatrices[3];
uniform float cascadeSplits[3];
uniform float shadowStrength;

// Lighting uniforms
uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec3 ambientColor;
uniform vec3 skyColor;
uniform vec3 cameraPos;
uniform float time;

// Fog parameters
uniform float fogDensity;
uniform float isUnderwater;
uniform mat4 invViewProj;
uniform float renderDistanceBlocks;  // Render distance in blocks (chunks * 16)

const float FOG_HEIGHT_FALLOFF = 0.015;
const float FOG_BASE_HEIGHT = 64.0;

float getFogDensity(float y) {
    float heightAboveBase = max(y - FOG_BASE_HEIGHT, 0.0);
    float heightFactor = exp(-heightAboveBase * FOG_HEIGHT_FALLOFF);
    float belowBase = max(FOG_BASE_HEIGHT - y, 0.0);
    float valleyFactor = 1.0 + belowBase * 0.02;
    return heightFactor * valleyFactor;
}

float calculateCascadeShadow(vec3 fragPos, vec3 normal, float viewDepth) {
    // Early out if shadows disabled
    if (shadowStrength < 0.01) return 0.0;

    // Select cascade based on view depth
    int cascade = 2;
    if (viewDepth < cascadeSplits[0]) cascade = 0;
    else if (viewDepth < cascadeSplits[1]) cascade = 1;

    // Transform to light space
    vec4 fragPosLightSpace = cascadeMatrices[cascade] * vec4(fragPos, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0) {
        return 0.0;
    }

    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001);
    float currentDepth = projCoords.z - bias;

    // Optimized PCF - fewer samples for distant cascades
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(cascadeShadowMaps, 0).xy);

    if (cascade == 0) {
        // Near cascade: 3x3 PCF for quality (9 samples)
        for (int x = -1; x <= 1; x++) {
            for (int y = -1; y <= 1; y++) {
                vec2 offset = vec2(x, y) * texelSize;
                shadow += texture(cascadeShadowMaps, vec4(projCoords.xy + offset, float(cascade), currentDepth));
            }
        }
        shadow = 1.0 - (shadow / 9.0);
    } else {
        // Distant cascades: 4-tap PCF for performance
        shadow += texture(cascadeShadowMaps, vec4(projCoords.xy + vec2(-0.5, -0.5) * texelSize, float(cascade), currentDepth));
        shadow += texture(cascadeShadowMaps, vec4(projCoords.xy + vec2(0.5, -0.5) * texelSize, float(cascade), currentDepth));
        shadow += texture(cascadeShadowMaps, vec4(projCoords.xy + vec2(-0.5, 0.5) * texelSize, float(cascade), currentDepth));
        shadow += texture(cascadeShadowMaps, vec4(projCoords.xy + vec2(0.5, 0.5) * texelSize, float(cascade), currentDepth));
        shadow = 1.0 - (shadow / 4.0);
    }

    // Distance fade
    float distFade = smoothstep(150.0, 250.0, viewDepth);
    return shadow * shadowStrength * (1.0 - distFade);

}

uniform int debugMode;  // 0=normal, 1=albedo, 2=normals, 3=position, 4=depth

void main() {
    // Sample G-buffer
    vec4 posAO = texture(gPosition, TexCoords);
    vec4 normalLight = texture(gNormal, TexCoords);
    vec4 albedoEmit = texture(gAlbedo, TexCoords);
    float depth = texture(gDepth, TexCoords).r;

    // Debug visualization modes
    if (debugMode == 1) {
        // Debug mode 1: Show albedo, or cyan for sky (depth=1), or magenta for zero albedo
        if (depth >= 0.999) {
            FragColor = vec4(0.0, 1.0, 1.0, 1.0);  // Cyan = sky pixel
        } else if (length(albedoEmit.rgb) < 0.001) {
            FragColor = vec4(1.0, 0.0, 1.0, 1.0);  // Magenta = geometry with zero albedo
        } else {
            FragColor = vec4(albedoEmit.rgb, 1.0);  // Normal albedo
        }
        return;
    } else if (debugMode == 2) {
        // Debug mode 2: Normals (should show blue-ish for up-facing, etc)
        if (depth >= 0.999) {
            FragColor = vec4(0.0, 1.0, 1.0, 1.0);  // Cyan = sky
        } else {
            FragColor = vec4(normalLight.xyz * 0.5 + 0.5, 1.0);
        }
        return;
    } else if (debugMode == 3) {
        // Debug mode 3: Position (fractional, colored by world coords)
        if (depth >= 0.999) {
            FragColor = vec4(0.0, 1.0, 1.0, 1.0);  // Cyan = sky
        } else {
            // Reconstruct position from depth for debug view
            vec2 ndc = TexCoords * 2.0 - 1.0;
            vec4 clipPos = vec4(ndc, depth * 2.0 - 1.0, 1.0);
            vec4 worldPos4 = invViewProj * clipPos;
            vec3 debugPos = worldPos4.xyz / worldPos4.w;
            FragColor = vec4(fract(debugPos / 16.0), 1.0);
        }
        return;
    } else if (debugMode == 4) {
        // Debug mode 4: Depth visualization
        FragColor = vec4(vec3(1.0 - depth), 1.0);  // Invert so closer = brighter
        return;
    }

    // Early out for sky pixels
    if (depth >= 1.0) {
        FragColor = vec4(skyColor, 1.0);
        return;
    }

    // Reconstruct world position from depth (saves G-buffer bandwidth)
    vec2 ndc = TexCoords * 2.0 - 1.0;
    vec4 clipPos = vec4(ndc, depth * 2.0 - 1.0, 1.0);
    vec4 worldPos4 = invViewProj * clipPos;
    vec3 fragPos = worldPos4.xyz / worldPos4.w;

    float ao = posAO.w;  // Still reading AO from position buffer for now
    vec3 normal = normalize(normalLight.xyz);
    float lightLevel = normalLight.w;
    vec3 albedo = albedoEmit.rgb;
    float emission = albedoEmit.a;

    // Calculate view depth for fog and shadows
    float viewDepth = length(fragPos - cameraPos);

    // SSAO
    float ssao = enableSSAO ? texture(ssaoTexture, TexCoords).r : 1.0;
    ao *= ssao;

    // Shadow
    float shadow = calculateCascadeShadow(fragPos, normal, viewDepth);

    // Lighting calculation
    vec3 lightDirection = normalize(lightDir);
    vec3 ambient = ambientColor * 0.6;
    float diff = max(dot(normal, lightDirection), 0.0);
    vec3 diffuse = diff * lightColor * 0.6 * (1.0 - shadow);
    vec3 pointLight = lightLevel * vec3(1.0, 0.85, 0.6) * 1.2;

    vec3 lighting = (ambient + diffuse) * ao + pointLight;

    vec3 result;
    if (emission > 0.0) {
        vec3 glowColor = albedo * (1.5 + emission * 0.5);
        if (emission < 1.0) {
            float pulse = sin(time * 2.0) * 0.1 + 1.0;
            glowColor *= pulse;
        }
        result = glowColor;
    } else {
        result = albedo * lighting;
    }

    // Distance fog with LOD-hiding enhancement
    // Fog intensifies at 70-100% of render distance to hide LOD transitions
    if (isUnderwater < 0.5) {
        float heightDensity = getFogDensity(fragPos.y);

        // Base fog - gradual exponential
        float baseFog = 1.0 - exp(-fogDensity * heightDensity * viewDepth * 0.01);

        // LOD transition fog - starts at 70% of render distance, full at 100%
        // This is the key to hiding LOD! Distant Horizons style.
        float lodStartDist = renderDistanceBlocks * 0.7;
        float lodEndDist = renderDistanceBlocks;
        float lodFogFactor = smoothstep(lodStartDist, lodEndDist, viewDepth);

        // Combine base fog with LOD-hiding fog
        // LOD fog adds up to 40% extra fog at the far edge
        float fogFactor = baseFog + lodFogFactor * 0.4 * (1.0 - baseFog);
        fogFactor = clamp(fogFactor, 0.0, 1.0);

        // Emissive blocks resist fog
        if (emission > 0.0) {
            fogFactor *= (1.0 - emission * 0.7);
        }

        vec3 fogColor = mix(skyColor, lightColor * 0.3, 0.3);
        result = mix(result, fogColor, fogFactor);
    } else {
        // Underwater fog
        float underwaterFog = 1.0 - exp(-fogDensity * 16.0 * viewDepth * viewDepth / 10000.0);
        underwaterFog = clamp(underwaterFog, 0.0, 1.0);
        vec3 underwaterColor = vec3(0.05, 0.2, 0.35);
        result = mix(result, underwaterColor, underwaterFog);
        result = mix(result, result * vec3(0.4, 0.7, 0.9), 0.4);
    }

    FragColor = vec4(result, 1.0);
}
)";

// ============================================
// FSR 1.0 SHADERS (FidelityFX Super Resolution)
// ============================================

// FSR Vertex shader (shared by EASU and RCAS)
const char* fsrVertexSource = R"(
#version 460 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoords;

void main() {
    TexCoords = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// FSR EASU (Edge Adaptive Spatial Upscaling) - Main upscaling pass
// Simplified implementation based on AMD FidelityFX FSR 1.0
const char* fsrEASUFragmentSource = R"(
#version 460 core
out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D inputTexture;
uniform vec2 inputSize;      // Render resolution (e.g., 640x360)
uniform vec2 outputSize;     // Display resolution (e.g., 1280x720)

// FsrEasuConst equivalent parameters
uniform vec4 con0;  // {inputSize.x/outputSize.x, inputSize.y/outputSize.y, 0.5*inputSize.x/outputSize.x - 0.5, 0.5*inputSize.y/outputSize.y - 0.5}
uniform vec4 con1;  // {1.0/inputSize.x, 1.0/inputSize.y, 1.0/inputSize.x, -1.0/inputSize.y}
uniform vec4 con2;  // {-1.0/inputSize.x, 2.0/inputSize.y, 1.0/inputSize.x, 2.0/inputSize.y}
uniform vec4 con3;  // {0.0/inputSize.x, 4.0/inputSize.y, 0, 0}

// Compute edge-directed weights for a 12-tap filter pattern
void main() {
    // Input pixel position (in output space)
    vec2 pos = TexCoords * outputSize;

    // Position in input texture
    vec2 srcPos = pos * con0.xy + con0.zw;
    vec2 srcUV = srcPos / inputSize;

    // Get texel offsets
    vec2 texelSize = 1.0 / inputSize;

    // 12-tap filter: sample in a cross pattern around the target pixel
    // This is a simplified version of FSR EASU's edge-aware sampling

    // Center and immediate neighbors
    vec3 c = texture(inputTexture, srcUV).rgb;
    vec3 n = texture(inputTexture, srcUV + vec2(0.0, -texelSize.y)).rgb;
    vec3 s = texture(inputTexture, srcUV + vec2(0.0, texelSize.y)).rgb;
    vec3 e = texture(inputTexture, srcUV + vec2(texelSize.x, 0.0)).rgb;
    vec3 w = texture(inputTexture, srcUV + vec2(-texelSize.x, 0.0)).rgb;

    // Corner samples
    vec3 nw = texture(inputTexture, srcUV + vec2(-texelSize.x, -texelSize.y)).rgb;
    vec3 ne = texture(inputTexture, srcUV + vec2(texelSize.x, -texelSize.y)).rgb;
    vec3 sw = texture(inputTexture, srcUV + vec2(-texelSize.x, texelSize.y)).rgb;
    vec3 se = texture(inputTexture, srcUV + vec2(texelSize.x, texelSize.y)).rgb;

    // Extended samples for edge detection
    vec3 n2 = texture(inputTexture, srcUV + vec2(0.0, -2.0 * texelSize.y)).rgb;
    vec3 s2 = texture(inputTexture, srcUV + vec2(0.0, 2.0 * texelSize.y)).rgb;
    vec3 e2 = texture(inputTexture, srcUV + vec2(2.0 * texelSize.x, 0.0)).rgb;
    vec3 w2 = texture(inputTexture, srcUV + vec2(-2.0 * texelSize.x, 0.0)).rgb;

    // Compute luminance for edge detection
    float lc = dot(c, vec3(0.299, 0.587, 0.114));
    float ln = dot(n, vec3(0.299, 0.587, 0.114));
    float ls = dot(s, vec3(0.299, 0.587, 0.114));
    float le = dot(e, vec3(0.299, 0.587, 0.114));
    float lw = dot(w, vec3(0.299, 0.587, 0.114));
    float lnw = dot(nw, vec3(0.299, 0.587, 0.114));
    float lne = dot(ne, vec3(0.299, 0.587, 0.114));
    float lsw = dot(sw, vec3(0.299, 0.587, 0.114));
    float lse = dot(se, vec3(0.299, 0.587, 0.114));

    // Detect edges using Sobel-like gradients
    float gradH = abs((lnw + 2.0*lw + lsw) - (lne + 2.0*le + lse));
    float gradV = abs((lnw + 2.0*ln + lne) - (lsw + 2.0*ls + lse));

    // Subpixel offset within the source texel
    vec2 subpix = fract(srcPos) - 0.5;

    // Edge-aware interpolation weights
    // Prefer interpolation along edges, not across them
    float edgeH = 1.0 / (1.0 + gradH * 4.0);
    float edgeV = 1.0 / (1.0 + gradV * 4.0);

    // Bilinear-like weights with edge awareness
    float wx = abs(subpix.x);
    float wy = abs(subpix.y);

    // Lanczos-inspired weights (simplified)
    float wc = (1.0 - wx) * (1.0 - wy);
    float wn = (1.0 - wx) * wy * (subpix.y < 0.0 ? 1.0 : 0.0) * edgeV;
    float ws = (1.0 - wx) * wy * (subpix.y >= 0.0 ? 1.0 : 0.0) * edgeV;
    float we = wx * (1.0 - wy) * (subpix.x >= 0.0 ? 1.0 : 0.0) * edgeH;
    float ww = wx * (1.0 - wy) * (subpix.x < 0.0 ? 1.0 : 0.0) * edgeH;

    // Normalize weights
    float wsum = wc + wn + ws + we + ww + 0.0001;
    wc /= wsum;
    wn /= wsum;
    ws /= wsum;
    we /= wsum;
    ww /= wsum;

    // Final color blend
    vec3 result = c * wc + n * wn + s * ws + e * we + w * ww;

    FragColor = vec4(result, 1.0);
}
)";

// FSR RCAS (Robust Contrast Adaptive Sharpening)
// Sharpening pass that enhances detail without amplifying noise
const char* fsrRCASFragmentSource = R"(
#version 460 core
out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D inputTexture;
uniform vec2 texelSize;     // 1.0 / resolution
uniform float sharpness;    // 0.0 = no sharpening, 2.0 = max (default 0.5)

void main() {
    // Sample the center and 4 neighbors (plus pattern)
    vec3 c = texture(inputTexture, TexCoords).rgb;
    vec3 n = texture(inputTexture, TexCoords + vec2(0.0, -texelSize.y)).rgb;
    vec3 s = texture(inputTexture, TexCoords + vec2(0.0, texelSize.y)).rgb;
    vec3 e = texture(inputTexture, TexCoords + vec2(texelSize.x, 0.0)).rgb;
    vec3 w = texture(inputTexture, TexCoords + vec2(-texelSize.x, 0.0)).rgb;

    // Compute min and max of neighborhood (for clamping)
    vec3 minC = min(c, min(min(n, s), min(e, w)));
    vec3 maxC = max(c, max(max(n, s), max(e, w)));

    // Average of neighbors
    vec3 avg = (n + s + e + w) * 0.25;

    // Compute local contrast
    vec3 diff = c - avg;

    // Apply sharpening with contrast-adaptive strength
    // The sharpening is reduced in high-contrast areas to prevent halos
    vec3 contrast = maxC - minC + 0.0001;
    vec3 adaptiveSharp = sharpness / (contrast + 0.5);
    adaptiveSharp = min(adaptiveSharp, vec3(1.0));  // Cap sharpening strength

    // Sharpen
    vec3 result = c + diff * adaptiveSharp;

    // Clamp to neighborhood bounds to prevent ringing/halos
    result = clamp(result, minC, maxC);

    FragColor = vec4(result, 1.0);
}
)";

// ============================================
// MESH SHADER (GL_NV_mesh_shader) - NVIDIA Turing+ only
// ============================================
// Mesh shaders replace the traditional vertex/geometry pipeline with a more efficient
// compute-like approach. Benefits: better GPU utilization, per-meshlet culling.

// Task shader - dispatches mesh shader workgroups based on visibility
// Performs per-meshlet frustum culling for efficient rendering
const char* meshTaskShaderSource = R"(
#version 460 core
#extension GL_NV_mesh_shader : require
#extension GL_KHR_shader_subgroup_ballot : require

layout(local_size_x = 32) in;

// Meshlet descriptor (32 bytes) - matches MeshletDescriptor in ChunkMesh.h
struct Meshlet {
    uint vertexOffset;      // Offset into vertex SSBO
    uint vertexCount;       // Number of vertices
    uint triangleOffset;    // Triangle index (for reference)
    uint triangleCount;     // Number of triangles
    float centerX, centerY, centerZ;  // Bounding sphere center
    float radius;           // Bounding sphere radius
};

layout(std430, binding = 2) readonly buffer MeshletBuffer {
    Meshlet meshlets[];
};

// Uniforms
layout(std140, binding = 3) uniform MeshShaderData {
    mat4 viewProj;
    vec3 chunkOffset;
    uint meshletCount;      // Total meshlets for this draw
};

// Additional frustum data for culling
layout(std140, binding = 4) uniform FrustumPlanes {
    vec4 planes[6];
} frustum;

// Task output - which meshlets to draw
taskNV out Task {
    uint meshletIndices[32];
} OUT;

// Frustum culling for bounding sphere
bool isVisible(vec3 center, float radius) {
    for (int i = 0; i < 6; i++) {
        if (dot(frustum.planes[i].xyz, center) + frustum.planes[i].w < -radius) {
            return false;
        }
    }
    return true;
}

void main() {
    uint meshletIndex = gl_GlobalInvocationID.x;

    bool visible = false;
    if (meshletIndex < meshletCount) {
        Meshlet m = meshlets[meshletIndex];
        vec3 localCenter = vec3(m.centerX, m.centerY, m.centerZ);
        vec3 worldCenter = localCenter + chunkOffset;
        visible = isVisible(worldCenter, m.radius);
    }

    // Count visible meshlets using subgroup operations
    uvec4 ballot = subgroupBallot(visible);
    uint visibleCount = subgroupBallotBitCount(ballot);

    // First thread writes the task count
    if (gl_LocalInvocationID.x == 0) {
        gl_TaskCountNV = visibleCount;
    }

    // Compact visible meshlet indices
    if (visible) {
        uint localIndex = subgroupBallotExclusiveBitCount(ballot);
        OUT.meshletIndices[localIndex] = meshletIndex;
    }
}
)";

// Mesh shader - generates vertices and primitives from meshlet data
// Uses non-indexed triangle lists for simplicity with greedy meshing output
const char* meshShaderSource = R"(
#version 460 core
#extension GL_NV_mesh_shader : require
#extension GL_NV_shader_subgroup_partitioned : enable

// Workgroup size: 32 threads (optimal for Turing/Ampere)
layout(local_size_x = 32) in;

// Output: triangles, max 64 vertices, max 21 primitives (64 vertices / 3 = 21 triangles)
layout(triangles, max_vertices = 64, max_primitives = 21) out;

// Meshlet descriptor (32 bytes) - matches MeshletDescriptor in ChunkMesh.h
struct Meshlet {
    uint vertexOffset;      // Offset into vertex SSBO
    uint vertexCount;       // Number of vertices
    uint triangleOffset;    // Triangle index (for reference)
    uint triangleCount;     // Number of triangles
    float centerX, centerY, centerZ;  // Bounding sphere center
    float radius;           // Bounding sphere radius
};

// Vertex data SSBO - packed as uvec4 (16 bytes each = 4 uints)
// PackedChunkVertex layout: [x,y,z] [u,v] [normalIndex,ao,light,texSlot] [padding]
layout(std430, binding = 0) readonly buffer VertexBuffer {
    uvec4 vertexData[];
};

// Meshlet data SSBO
layout(std430, binding = 2) readonly buffer MeshletBuffer {
    Meshlet meshlets[];
};

// Uniforms
layout(std140, binding = 3) uniform MeshShaderData {
    mat4 viewProj;
    vec3 chunkOffset;
    uint meshletCount;
};

// Task input
taskNV in Task {
    uint meshletIndices[32];
} IN;

// Per-vertex outputs (match G-buffer shader)
layout(location = 0) out PerVertexData {
    vec3 worldPos;
    vec3 normal;
    vec2 texCoord;
    vec2 texSlotBase;
    float aoFactor;
    float lightLevel;
} v_out[];

// Normal lookup table
const vec3 NORMALS[6] = vec3[6](
    vec3(1, 0, 0), vec3(-1, 0, 0),
    vec3(0, 1, 0), vec3(0, -1, 0),
    vec3(0, 0, 1), vec3(0, 0, -1)
);

// Texture atlas constants
const float ATLAS_SIZE = 256.0;
const float TILE_SIZE = 16.0;
const float TILES_PER_ROW = ATLAS_SIZE / TILE_SIZE;

void main() {
    uint meshletIndex = IN.meshletIndices[gl_WorkGroupID.x];
    Meshlet m = meshlets[meshletIndex];

    uint threadId = gl_LocalInvocationID.x;

    // Set output counts - use gl_PrimitiveCountNV for primitive count
    // For NV mesh shaders: vertices are set implicitly by writing to gl_MeshVerticesNV[]
    gl_PrimitiveCountNV = m.triangleCount;

    // Process vertices (each thread handles multiple if needed)
    for (uint i = threadId; i < m.vertexCount; i += 32u) {
        uvec4 data = vertexData[m.vertexOffset + i];

        // PackedChunkVertex layout (16 bytes):
        // bytes 0-1: x (int16), bytes 2-3: y (int16)
        // bytes 4-5: z (int16), bytes 6-7: u (uint16)
        // bytes 8-9: v (uint16), bytes 10: normalIndex, bytes 11: ao
        // bytes 12: light, bytes 13: texSlot, bytes 14-15: padding

        // data.x = [x_low, x_high, y_low, y_high] = x(16) | y(16)<<16
        // data.y = [z_low, z_high, u_low, u_high] = z(16) | u(16)<<16
        // data.z = [v_low, v_high, normalIdx, ao] = v(16) | (normalIdx | ao<<8)<<16
        // data.w = [light, texSlot, pad, pad] = light | texSlot<<8 | pad<<16

        // Extract x (signed 16-bit from low bits of data.x)
        int xInt = int(data.x & 0xFFFFu);
        if (xInt >= 32768) xInt -= 65536;
        float x = float(xInt);

        // Extract y (signed 16-bit from high bits of data.x)
        int yInt = int(data.x >> 16u);
        if (yInt >= 32768) yInt -= 65536;
        float y = float(yInt);

        // Extract z (signed 16-bit from low bits of data.y)
        int zInt = int(data.y & 0xFFFFu);
        if (zInt >= 32768) zInt -= 65536;
        float z = float(zInt);

        vec3 localPos = vec3(x, y, z) / 256.0;
        vec3 worldPos = localPos + chunkOffset;

        // Extract u (unsigned 16-bit from high bits of data.y)
        float u = float(data.y >> 16u);

        // Extract v (unsigned 16-bit from low bits of data.z)
        float v = float(data.z & 0xFFFFu);

        vec2 texCoord = vec2(u, v) / 256.0;

        // Extract normalIndex from byte 2 of high 16 bits of data.z
        uint normalIdx = (data.z >> 16u) & 0xFFu;

        // Extract ao from byte 3 of data.z
        uint ao = (data.z >> 24u) & 0xFFu;

        // Extract light from byte 0 of data.w
        uint light = data.w & 0xFFu;

        // Extract texSlot from byte 1 of data.w
        uint texSlot = (data.w >> 8u) & 0xFFu;

        vec3 normal = NORMALS[min(normalIdx, 5u)];

        // Calculate texture slot base UV
        float slotX = mod(float(texSlot), TILES_PER_ROW);
        float slotY = floor(float(texSlot) / TILES_PER_ROW);
        vec2 texSlotBase = vec2(slotX, slotY) * (TILE_SIZE / ATLAS_SIZE);

        // Write outputs
        gl_MeshVerticesNV[i].gl_Position = viewProj * vec4(worldPos, 1.0);
        v_out[i].worldPos = worldPos;
        v_out[i].normal = normal;
        v_out[i].texCoord = texCoord;
        v_out[i].texSlotBase = texSlotBase;
        v_out[i].aoFactor = float(ao) / 255.0;
        v_out[i].lightLevel = float(light) / 255.0;
    }

    // For non-indexed triangles, set sequential indices (0,1,2, 3,4,5, ...)
    for (uint i = threadId; i < m.triangleCount; i += 32u) {
        uint triIdx = i * 3u;
        gl_PrimitiveIndicesNV[triIdx + 0u] = triIdx + 0u;
        gl_PrimitiveIndicesNV[triIdx + 1u] = triIdx + 1u;
        gl_PrimitiveIndicesNV[triIdx + 2u] = triIdx + 2u;
    }
}
)";

// Fragment shader for mesh shader path (same as G-buffer fragment)
const char* meshFragmentShaderSource = R"(
#version 460 core

layout(location = 0) out vec4 gPosition;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedo;

uniform sampler2D texAtlas;

// Per-vertex input
layout(location = 0) in PerVertexData {
    vec3 worldPos;
    vec3 normal;
    vec2 texCoord;
    vec2 texSlotBase;
    float aoFactor;
    float lightLevel;
} v_in;

const float ATLAS_SIZE = 256.0;
const float TILE_SIZE = 16.0;

void main() {
    // Calculate final texture coordinates with tiling
    vec2 tileUV = fract(v_in.texCoord) * (TILE_SIZE / ATLAS_SIZE);
    vec2 finalUV = v_in.texSlotBase + tileUV;

    // Sample texture
    vec4 texColor = texture(texAtlas, finalUV);

    // Alpha test
    if (texColor.a < 0.5) {
        discard;
    }

    // Output to G-buffer
    gPosition = vec4(v_in.worldPos, v_in.aoFactor);
    gNormal = vec4(normalize(v_in.normal), v_in.lightLevel);
    gAlbedo = vec4(texColor.rgb, 0.0);  // Alpha = emission flag
}
)";

// SSAO shader
const char* ssaoVertexSource = R"(
#version 460 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoords;

void main() {
    TexCoords = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* ssaoFragmentSource = R"(
#version 460 core
out float FragColor;

in vec2 TexCoords;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gDepth;
uniform sampler2D noiseTexture;

// OPTIMIZATION: Use UBO for kernel samples (uploaded once, not per-frame)
layout(std140, binding = 0) uniform SSAOKernel {
    vec4 samples[32];  // vec4 for std140 alignment (only xyz used)
};

uniform mat4 projection;
uniform mat4 view;
uniform vec2 noiseScale;
uniform float radius;
uniform float bias;

void main() {
    vec3 fragPos = texture(gPosition, TexCoords).xyz;
    vec3 normal = normalize(texture(gNormal, TexCoords).xyz);
    float depth = texture(gDepth, TexCoords).r;

    // Skip sky pixels
    if (depth >= 1.0) {
        FragColor = 1.0;
        return;
    }

    // Transform to view space
    vec3 fragPosView = (view * vec4(fragPos, 1.0)).xyz;
    vec3 normalView = normalize((view * vec4(normal, 0.0)).xyz);

    // Random rotation from noise texture
    vec3 randomVec = normalize(texture(noiseTexture, TexCoords * noiseScale).xyz);

    // Gram-Schmidt to create TBN matrix
    vec3 tangent = normalize(randomVec - normalView * dot(randomVec, normalView));
    vec3 bitangent = cross(normalView, tangent);
    mat3 TBN = mat3(tangent, bitangent, normalView);

    float occlusion = 0.0;
    for (int i = 0; i < 32; i++) {
        // Get sample position (samples stored as vec4 for std140 alignment)
        vec3 sampleDir = TBN * samples[i].xyz;
        vec3 samplePos = fragPosView + sampleDir * radius;

        // Project sample to screen space
        vec4 offset = projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;

        // Sample depth at this position
        float sampleDepth = texture(gDepth, offset.xy).r;

        // Reconstruct view-space position of sampled point
        vec3 sampledPos = texture(gPosition, offset.xy).xyz;
        float sampledDepth = (view * vec4(sampledPos, 1.0)).z;

        // Range check and compare
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPosView.z - sampledDepth));
        occlusion += (sampledDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    occlusion = 1.0 - (occlusion / 32.0);
    FragColor = pow(occlusion, 2.0);  // Increase contrast
}
)";

// SSAO blur shader
const char* ssaoBlurFragmentSource = R"(
#version 460 core
out float FragColor;

in vec2 TexCoords;

uniform sampler2D ssaoInput;

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(ssaoInput, 0));
    float result = 0.0;
    for (int x = -2; x <= 2; x++) {
        for (int y = -2; y <= 2; y++) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            result += texture(ssaoInput, TexCoords + offset).r;
        }
    }
    FragColor = result / 25.0;
}
)";

// Hi-Z downsample compute shader
const char* hiZDownsampleSource = R"(
#version 460 core
layout (local_size_x = 8, local_size_y = 8) in;

uniform sampler2D srcDepth;
uniform int srcLevel;
layout (r32f, binding = 0) uniform writeonly image2D dstDepth;

void main() {
    ivec2 dstCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 srcCoord = dstCoord * 2;

    // Sample 4 texels from source level and take maximum (conservative)
    float d0 = texelFetch(srcDepth, srcCoord + ivec2(0, 0), srcLevel).r;
    float d1 = texelFetch(srcDepth, srcCoord + ivec2(1, 0), srcLevel).r;
    float d2 = texelFetch(srcDepth, srcCoord + ivec2(0, 1), srcLevel).r;
    float d3 = texelFetch(srcDepth, srcCoord + ivec2(1, 1), srcLevel).r;

    float maxDepth = max(max(d0, d1), max(d2, d3));
    imageStore(dstDepth, dstCoord, vec4(maxDepth));
}
)";

// Occlusion culling compute shader
const char* occlusionCullSource = R"(
#version 460 core
layout (local_size_x = 64) in;

struct ChunkBounds {
    vec4 minBound;  // xyz = min corner, w = padding
    vec4 maxBound;  // xyz = max corner, w = padding
};

layout (std430, binding = 0) buffer ChunkBoundsBuffer {
    ChunkBounds bounds[];
};

layout (std430, binding = 1) buffer VisibilityBuffer {
    uint visible[];
};

uniform sampler2D hiZBuffer;
uniform mat4 viewProj;
uniform int numMipLevels;
uniform vec2 screenSize;
uniform int chunkCount;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= chunkCount) return;

    vec3 minB = bounds[idx].minBound.xyz;
    vec3 maxB = bounds[idx].maxBound.xyz;

    // Project all 8 corners to screen space
    vec4 corners[8];
    corners[0] = viewProj * vec4(minB.x, minB.y, minB.z, 1.0);
    corners[1] = viewProj * vec4(maxB.x, minB.y, minB.z, 1.0);
    corners[2] = viewProj * vec4(minB.x, maxB.y, minB.z, 1.0);
    corners[3] = viewProj * vec4(maxB.x, maxB.y, minB.z, 1.0);
    corners[4] = viewProj * vec4(minB.x, minB.y, maxB.z, 1.0);
    corners[5] = viewProj * vec4(maxB.x, minB.y, maxB.z, 1.0);
    corners[6] = viewProj * vec4(minB.x, maxB.y, maxB.z, 1.0);
    corners[7] = viewProj * vec4(maxB.x, maxB.y, maxB.z, 1.0);

    // Find screen-space bounding box and closest depth
    vec2 minScreen = vec2(1.0);
    vec2 maxScreen = vec2(-1.0);
    float minDepth = 1.0;
    bool anyInFront = false;

    for (int i = 0; i < 8; i++) {
        if (corners[i].w <= 0.0) {
            // Behind camera - assume visible
            visible[idx] = 1u;
            return;
        }

        vec3 ndc = corners[i].xyz / corners[i].w;
        minScreen = min(minScreen, ndc.xy);
        maxScreen = max(maxScreen, ndc.xy);
        minDepth = min(minDepth, ndc.z * 0.5 + 0.5);
        anyInFront = true;
    }

    // Clamp to screen bounds
    minScreen = clamp(minScreen * 0.5 + 0.5, vec2(0.0), vec2(1.0));
    maxScreen = clamp(maxScreen * 0.5 + 0.5, vec2(0.0), vec2(1.0));

    // Calculate appropriate mip level based on screen size
    vec2 size = (maxScreen - minScreen) * screenSize;
    float maxDim = max(size.x, size.y);
    int mipLevel = int(ceil(log2(maxDim)));
    mipLevel = clamp(mipLevel, 0, numMipLevels - 1);

    // Sample Hi-Z at center of bounding box
    vec2 center = (minScreen + maxScreen) * 0.5;
    float hiZDepth = textureLod(hiZBuffer, center, float(mipLevel)).r;

    // Occluded if chunk's closest point is behind Hi-Z depth
    visible[idx] = (minDepth <= hiZDepth) ? 1u : 0u;
}
)";

// Check shader compilation
bool checkShaderCompilation(GLuint shader, const std::string& type) {
    GLint success;
    char infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "ERROR::SHADER::" << type << "::COMPILATION_FAILED\n" << infoLog << std::endl;
        return false;
    }
    return true;
}

// Check program linking
bool checkProgramLinking(GLuint program) {
    GLint success;
    char infoLog[512];
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        std::cerr << "ERROR::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
        return false;
    }
    return true;
}

// ============================================
// CASCADE SHADOW MAP CALCULATIONS
// ============================================

// Calculate cascade split distances using practical split scheme
void calculateCascadeSplits(float nearPlane, float farPlane, int numCascades,
                            float lambda, float* splits) {
    // Practical split scheme: blend between logarithmic and uniform splits
    // lambda = 0: uniform, lambda = 1: logarithmic
    float ratio = farPlane / nearPlane;
    for (int i = 0; i < numCascades; i++) {
        float p = (float)(i + 1) / (float)numCascades;
        float log_split = nearPlane * pow(ratio, p);
        float uni_split = nearPlane + (farPlane - nearPlane) * p;
        splits[i] = lambda * log_split + (1.0f - lambda) * uni_split;
    }
}

// Get frustum corners in world space for a given near/far range
std::vector<glm::vec4> getFrustumCornersWorldSpace(const glm::mat4& view,
                                                    float fov, float aspect,
                                                    float nearPlane, float farPlane) {
    glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
    glm::mat4 invViewProj = glm::inverse(proj * view);

    std::vector<glm::vec4> corners;
    for (int x = 0; x < 2; x++) {
        for (int y = 0; y < 2; y++) {
            for (int z = 0; z < 2; z++) {
                glm::vec4 pt = invViewProj * glm::vec4(
                    2.0f * x - 1.0f,
                    2.0f * y - 1.0f,
                    2.0f * z - 1.0f,
                    1.0f
                );
                corners.push_back(pt / pt.w);
            }
        }
    }
    return corners;
}

// Calculate light space matrix for a cascade
glm::mat4 calculateCascadeLightSpaceMatrix(const glm::vec3& lightDir,
                                            const std::vector<glm::vec4>& frustumCorners) {
    // Calculate frustum center
    glm::vec3 center(0.0f);
    for (const auto& corner : frustumCorners) {
        center += glm::vec3(corner);
    }
    center /= static_cast<float>(frustumCorners.size());

    // Look from light direction toward center
    glm::mat4 lightView = glm::lookAt(
        center - lightDir * 100.0f,  // Light position (far away in light direction)
        center,
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    // Find bounding box in light space
    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();

    for (const auto& corner : frustumCorners) {
        glm::vec4 lightSpaceCorner = lightView * corner;
        minX = std::min(minX, lightSpaceCorner.x);
        maxX = std::max(maxX, lightSpaceCorner.x);
        minY = std::min(minY, lightSpaceCorner.y);
        maxY = std::max(maxY, lightSpaceCorner.y);
        minZ = std::min(minZ, lightSpaceCorner.z);
        maxZ = std::max(maxZ, lightSpaceCorner.z);
    }

    // Expand Z range to include shadow casters behind the frustum
    float zMult = 10.0f;
    if (minZ < 0) minZ *= zMult;
    else minZ /= zMult;
    if (maxZ < 0) maxZ /= zMult;
    else maxZ *= zMult;

    // Create orthographic projection for this cascade
    glm::mat4 lightProj = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);

    return lightProj * lightView;
}

int main() {
    // Load configuration
    g_config.load("settings.cfg");

    // Apply config to globals
    WINDOW_WIDTH = g_config.windowWidth;
    WINDOW_HEIGHT = g_config.windowHeight;

    // Apply camera settings from config
    camera.fov = static_cast<float>(g_config.fov);
    camera.mouseSensitivity = g_config.mouseSensitivity;

    // Apply quality settings from config
    g_useDeferredRendering = g_config.enableDeferredRendering;
    g_enableSSAO = g_config.enableSSAO;
    g_enableHiZCulling = g_config.enableHiZCulling;
    g_showPerfStats = g_config.showPerformanceStats;

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    // Configure GLFW
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    // Create window (fullscreen if configured)
    GLFWwindow* window = nullptr;
    if (g_config.fullscreen) {
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        window = glfwCreateWindow(mode->width, mode->height, WINDOW_TITLE, monitor, nullptr);
    } else {
        window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE, nullptr, nullptr);
    }

    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);

    // Capture mouse
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // VSync from config
    glfwSwapInterval(g_config.vsync ? 1 : 0);

    // Load OpenGL functions using GLAD
    int version = gladLoadGL(glfwGetProcAddress);
    if (version == 0) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << std::endl;

    // ============================================
    // HARDWARE DETECTION & AUTO-TUNING
    // ============================================
    g_hardware.gpuName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    g_hardware.gpuVendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));

    // Try to get VRAM info (NVIDIA extension)
    GLint vramKB = 0;
    glGetIntegerv(0x9048, &vramKB);  // GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX
    if (vramKB > 0) {
        g_hardware.vramMB = vramKB / 1024;
    } else {
        // Try AMD extension
        glGetIntegerv(0x87FB, &vramKB);  // GL_TEXTURE_FREE_MEMORY_ATI
        if (vramKB > 0) {
            g_hardware.vramMB = vramKB / 1024;
        }
    }

    // Classify GPU tier and calculate recommendations
    g_hardware.classifyGPU();
    g_hardware.calculateRecommendations();
    g_hardware.print();

    // Check for mesh shader extension (GL_NV_mesh_shader)
    GLint numExtensions = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
    for (GLint i = 0; i < numExtensions; i++) {
        const char* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
        if (ext && strcmp(ext, "GL_NV_mesh_shader") == 0) {
            g_meshShadersAvailable = true;
            break;
        }
    }

    if (g_meshShadersAvailable) {
        std::cout << "Mesh shaders available (GL_NV_mesh_shader)" << std::endl;
        // Load glDrawMeshTasksNV function pointer (if not already provided by GLAD)
#ifndef GLAD_GL_NV_mesh_shader
        pfn_glDrawMeshTasksNV = (PFNGLDRAWMESHTASKSNVPROC_LOCAL)glfwGetProcAddress("glDrawMeshTasksNV");
        if (pfn_glDrawMeshTasksNV) {
            std::cout << "  glDrawMeshTasksNV loaded successfully" << std::endl;
        } else {
            std::cout << "  Failed to load glDrawMeshTasksNV" << std::endl;
            g_meshShadersAvailable = false;
        }
#else
        // GLAD provides mesh shader support
        std::cout << "  Using GLAD mesh shader functions" << std::endl;
#endif
        if (g_meshShadersAvailable) {
            // Enable by default on supported hardware
            g_enableMeshShaders = g_hardware.supportsMeshShaders;
            // Enable meshlet generation for mesh shader rendering
            g_generateMeshlets = g_enableMeshShaders;
        } else {
            g_enableMeshShaders = false;
            g_generateMeshlets = false;
        }
    } else {
        std::cout << "Mesh shaders not available" << std::endl;
        g_enableMeshShaders = false;
        g_generateMeshlets = false;
    }

    // Auto-tune if enabled
    if (g_config.autoTuneOnStartup) {
        g_config.autoTune();
        g_config.autoTuneOnStartup = false;  // Only auto-tune once
        g_config.save();  // Save the tuned settings

        // Re-apply settings that were already used before auto-tune
        camera.fov = static_cast<float>(g_config.fov);
        g_useDeferredRendering = g_config.enableDeferredRendering;
        g_enableSSAO = g_config.enableSSAO;
        g_enableHiZCulling = g_config.enableHiZCulling;
    }

    // Compile vertex shader
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
    glCompileShader(vertexShader);
    if (!checkShaderCompilation(vertexShader, "VERTEX")) return -1;

    // Compile fragment shader
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fragmentShader);
    if (!checkShaderCompilation(fragmentShader, "FRAGMENT")) return -1;

    // Link shaders into program
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    if (!checkProgramLinking(shaderProgram)) return -1;

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Compile water vertex shader
    GLuint waterVertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(waterVertexShader, 1, &waterVertexShaderSource, nullptr);
    glCompileShader(waterVertexShader);
    if (!checkShaderCompilation(waterVertexShader, "WATER VERTEX")) return -1;

    // Compile water fragment shader
    GLuint waterFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(waterFragmentShader, 1, &waterFragmentShaderSource, nullptr);
    glCompileShader(waterFragmentShader);
    if (!checkShaderCompilation(waterFragmentShader, "WATER FRAGMENT")) return -1;

    // Link water shaders into program
    GLuint waterShaderProgram = glCreateProgram();
    glAttachShader(waterShaderProgram, waterVertexShader);
    glAttachShader(waterShaderProgram, waterFragmentShader);
    glLinkProgram(waterShaderProgram);
    if (!checkProgramLinking(waterShaderProgram)) return -1;

    glDeleteShader(waterVertexShader);
    glDeleteShader(waterFragmentShader);

    // Compile sky vertex shader
    GLuint skyVertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(skyVertexShader, 1, &skyVertexShaderSource, nullptr);
    glCompileShader(skyVertexShader);
    if (!checkShaderCompilation(skyVertexShader, "SKY VERTEX")) return -1;

    // Compile sky fragment shader
    GLuint skyFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(skyFragmentShader, 1, &skyFragmentShaderSource, nullptr);
    glCompileShader(skyFragmentShader);
    if (!checkShaderCompilation(skyFragmentShader, "SKY FRAGMENT")) return -1;

    // Link sky shaders into program
    GLuint skyShaderProgram = glCreateProgram();
    glAttachShader(skyShaderProgram, skyVertexShader);
    glAttachShader(skyShaderProgram, skyFragmentShader);
    glLinkProgram(skyShaderProgram);
    if (!checkProgramLinking(skyShaderProgram)) return -1;

    glDeleteShader(skyVertexShader);
    glDeleteShader(skyFragmentShader);

    // Compile precipitation vertex shader
    GLuint precipVertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(precipVertexShader, 1, &precipVertexShaderSource, nullptr);
    glCompileShader(precipVertexShader);
    if (!checkShaderCompilation(precipVertexShader, "PRECIP VERTEX")) return -1;

    // Compile precipitation fragment shader
    GLuint precipFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(precipFragmentShader, 1, &precipFragmentShaderSource, nullptr);
    glCompileShader(precipFragmentShader);
    if (!checkShaderCompilation(precipFragmentShader, "PRECIP FRAGMENT")) return -1;

    // Link precipitation shaders into program
    GLuint precipShaderProgram = glCreateProgram();
    glAttachShader(precipShaderProgram, precipVertexShader);
    glAttachShader(precipShaderProgram, precipFragmentShader);
    glLinkProgram(precipShaderProgram);
    if (!checkProgramLinking(precipShaderProgram)) return -1;

    glDeleteShader(precipVertexShader);
    glDeleteShader(precipFragmentShader);

    // Compile shadow vertex shader
    GLuint shadowVertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(shadowVertexShader, 1, &shadowVertexShaderSource, nullptr);
    glCompileShader(shadowVertexShader);
    if (!checkShaderCompilation(shadowVertexShader, "SHADOW VERTEX")) return -1;

    // Compile shadow fragment shader
    GLuint shadowFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(shadowFragmentShader, 1, &shadowFragmentShaderSource, nullptr);
    glCompileShader(shadowFragmentShader);
    if (!checkShaderCompilation(shadowFragmentShader, "SHADOW FRAGMENT")) return -1;

    // Link shadow shaders into program
    GLuint shadowShaderProgram = glCreateProgram();
    glAttachShader(shadowShaderProgram, shadowVertexShader);
    glAttachShader(shadowShaderProgram, shadowFragmentShader);
    glLinkProgram(shadowShaderProgram);
    if (!checkProgramLinking(shadowShaderProgram)) return -1;

    glDeleteShader(shadowVertexShader);
    glDeleteShader(shadowFragmentShader);

    // Compile Z-prepass shaders (eliminates overdraw)
    GLuint zPrepassVertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(zPrepassVertexShader, 1, &zPrepassVertexSource, nullptr);
    glCompileShader(zPrepassVertexShader);
    if (!checkShaderCompilation(zPrepassVertexShader, "ZPREPASS_VERTEX")) return -1;

    GLuint zPrepassFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(zPrepassFragmentShader, 1, &zPrepassFragmentSource, nullptr);
    glCompileShader(zPrepassFragmentShader);
    if (!checkShaderCompilation(zPrepassFragmentShader, "ZPREPASS_FRAGMENT")) return -1;

    GLuint zPrepassProgram = glCreateProgram();
    glAttachShader(zPrepassProgram, zPrepassVertexShader);
    glAttachShader(zPrepassProgram, zPrepassFragmentShader);
    glLinkProgram(zPrepassProgram);
    if (!checkProgramLinking(zPrepassProgram)) return -1;

    glDeleteShader(zPrepassVertexShader);
    glDeleteShader(zPrepassFragmentShader);

    // Compile loading screen shaders
    GLuint loadingVertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(loadingVertexShader, 1, &loadingVertexShaderSource, nullptr);
    glCompileShader(loadingVertexShader);
    if (!checkShaderCompilation(loadingVertexShader, "LOADING_VERTEX")) return -1;

    GLuint loadingFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(loadingFragmentShader, 1, &loadingFragmentShaderSource, nullptr);
    glCompileShader(loadingFragmentShader);
    if (!checkShaderCompilation(loadingFragmentShader, "LOADING_FRAGMENT")) return -1;

    GLuint loadingShaderProgram = glCreateProgram();
    glAttachShader(loadingShaderProgram, loadingVertexShader);
    glAttachShader(loadingShaderProgram, loadingFragmentShader);
    glLinkProgram(loadingShaderProgram);
    if (!checkProgramLinking(loadingShaderProgram)) return -1;

    glDeleteShader(loadingVertexShader);
    glDeleteShader(loadingFragmentShader);

    // ============================================
    // COMPILE DEFERRED RENDERING SHADERS
    // ============================================

    // G-Buffer shader program
    GLuint gBufferVertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(gBufferVertexShader, 1, &gBufferVertexSource, nullptr);
    glCompileShader(gBufferVertexShader);
    if (!checkShaderCompilation(gBufferVertexShader, "GBUFFER_VERTEX")) return -1;

    GLuint gBufferFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(gBufferFragmentShader, 1, &gBufferFragmentSource, nullptr);
    glCompileShader(gBufferFragmentShader);
    if (!checkShaderCompilation(gBufferFragmentShader, "GBUFFER_FRAGMENT")) return -1;

    GLuint gBufferProgram = glCreateProgram();
    glAttachShader(gBufferProgram, gBufferVertexShader);
    glAttachShader(gBufferProgram, gBufferFragmentShader);
    glLinkProgram(gBufferProgram);
    if (!checkProgramLinking(gBufferProgram)) return -1;

    glDeleteShader(gBufferVertexShader);
    glDeleteShader(gBufferFragmentShader);

    // Composite shader program
    GLuint compositeVertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(compositeVertexShader, 1, &compositeVertexSource, nullptr);
    glCompileShader(compositeVertexShader);
    if (!checkShaderCompilation(compositeVertexShader, "COMPOSITE_VERTEX")) return -1;

    GLuint compositeFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(compositeFragmentShader, 1, &compositeFragmentSource, nullptr);
    glCompileShader(compositeFragmentShader);
    if (!checkShaderCompilation(compositeFragmentShader, "COMPOSITE_FRAGMENT")) return -1;

    GLuint compositeProgram = glCreateProgram();
    glAttachShader(compositeProgram, compositeVertexShader);
    glAttachShader(compositeProgram, compositeFragmentShader);
    glLinkProgram(compositeProgram);
    if (!checkProgramLinking(compositeProgram)) return -1;

    glDeleteShader(compositeVertexShader);
    glDeleteShader(compositeFragmentShader);

    // SSAO shader program
    GLuint ssaoVertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(ssaoVertexShader, 1, &ssaoVertexSource, nullptr);
    glCompileShader(ssaoVertexShader);
    if (!checkShaderCompilation(ssaoVertexShader, "SSAO_VERTEX")) return -1;

    GLuint ssaoFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(ssaoFragmentShader, 1, &ssaoFragmentSource, nullptr);
    glCompileShader(ssaoFragmentShader);
    if (!checkShaderCompilation(ssaoFragmentShader, "SSAO_FRAGMENT")) return -1;

    GLuint ssaoProgram = glCreateProgram();
    glAttachShader(ssaoProgram, ssaoVertexShader);
    glAttachShader(ssaoProgram, ssaoFragmentShader);
    glLinkProgram(ssaoProgram);
    if (!checkProgramLinking(ssaoProgram)) return -1;

    glDeleteShader(ssaoVertexShader);
    glDeleteShader(ssaoFragmentShader);

    // SSAO Blur shader program (reuses ssao vertex shader source)
    GLuint ssaoBlurVertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(ssaoBlurVertexShader, 1, &ssaoVertexSource, nullptr);
    glCompileShader(ssaoBlurVertexShader);
    if (!checkShaderCompilation(ssaoBlurVertexShader, "SSAO_BLUR_VERTEX")) return -1;

    GLuint ssaoBlurFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(ssaoBlurFragmentShader, 1, &ssaoBlurFragmentSource, nullptr);
    glCompileShader(ssaoBlurFragmentShader);
    if (!checkShaderCompilation(ssaoBlurFragmentShader, "SSAO_BLUR_FRAGMENT")) return -1;

    GLuint ssaoBlurProgram = glCreateProgram();
    glAttachShader(ssaoBlurProgram, ssaoBlurVertexShader);
    glAttachShader(ssaoBlurProgram, ssaoBlurFragmentShader);
    glLinkProgram(ssaoBlurProgram);
    if (!checkProgramLinking(ssaoBlurProgram)) return -1;

    glDeleteShader(ssaoBlurVertexShader);
    glDeleteShader(ssaoBlurFragmentShader);

    // FSR EASU (upscaling) shader program
    GLuint fsrEASUVertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(fsrEASUVertexShader, 1, &fsrVertexSource, nullptr);
    glCompileShader(fsrEASUVertexShader);
    if (!checkShaderCompilation(fsrEASUVertexShader, "FSR_EASU_VERTEX")) return -1;

    GLuint fsrEASUFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fsrEASUFragmentShader, 1, &fsrEASUFragmentSource, nullptr);
    glCompileShader(fsrEASUFragmentShader);
    if (!checkShaderCompilation(fsrEASUFragmentShader, "FSR_EASU_FRAGMENT")) return -1;

    GLuint fsrEASUProgram = glCreateProgram();
    glAttachShader(fsrEASUProgram, fsrEASUVertexShader);
    glAttachShader(fsrEASUProgram, fsrEASUFragmentShader);
    glLinkProgram(fsrEASUProgram);
    if (!checkProgramLinking(fsrEASUProgram)) return -1;

    glDeleteShader(fsrEASUVertexShader);
    glDeleteShader(fsrEASUFragmentShader);

    // FSR RCAS (sharpening) shader program
    GLuint fsrRCASVertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(fsrRCASVertexShader, 1, &fsrVertexSource, nullptr);
    glCompileShader(fsrRCASVertexShader);
    if (!checkShaderCompilation(fsrRCASVertexShader, "FSR_RCAS_VERTEX")) return -1;

    GLuint fsrRCASFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fsrRCASFragmentShader, 1, &fsrRCASFragmentSource, nullptr);
    glCompileShader(fsrRCASFragmentShader);
    if (!checkShaderCompilation(fsrRCASFragmentShader, "FSR_RCAS_FRAGMENT")) return -1;

    GLuint fsrRCASProgram = glCreateProgram();
    glAttachShader(fsrRCASProgram, fsrRCASVertexShader);
    glAttachShader(fsrRCASProgram, fsrRCASFragmentShader);
    glLinkProgram(fsrRCASProgram);
    if (!checkProgramLinking(fsrRCASProgram)) return -1;

    glDeleteShader(fsrRCASVertexShader);
    glDeleteShader(fsrRCASFragmentShader);

    std::cout << "FSR shaders compiled successfully." << std::endl;

    // Hi-Z downsample compute shader program
    GLuint hiZDownsampleShader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(hiZDownsampleShader, 1, &hiZDownsampleSource, nullptr);
    glCompileShader(hiZDownsampleShader);
    if (!checkShaderCompilation(hiZDownsampleShader, "HIZ_DOWNSAMPLE_COMPUTE")) return -1;

    GLuint hiZDownsampleProgram = glCreateProgram();
    glAttachShader(hiZDownsampleProgram, hiZDownsampleShader);
    glLinkProgram(hiZDownsampleProgram);
    if (!checkProgramLinking(hiZDownsampleProgram)) return -1;

    glDeleteShader(hiZDownsampleShader);

    // Occlusion culling compute shader program
    GLuint occlusionCullShader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(occlusionCullShader, 1, &occlusionCullSource, nullptr);
    glCompileShader(occlusionCullShader);
    if (!checkShaderCompilation(occlusionCullShader, "OCCLUSION_CULL_COMPUTE")) return -1;

    GLuint occlusionCullProgram = glCreateProgram();
    glAttachShader(occlusionCullProgram, occlusionCullShader);
    glLinkProgram(occlusionCullProgram);
    if (!checkProgramLinking(occlusionCullProgram)) return -1;

    glDeleteShader(occlusionCullShader);

    std::cout << "Deferred rendering shaders compiled successfully" << std::endl;

    // ================================================================
    // MESH SHADER COMPILATION (GL_NV_mesh_shader) - Optional path
    // ================================================================
    if (g_meshShadersAvailable) {
        std::cout << "Compiling mesh shader program..." << std::endl;

        // Compile task shader
        GLuint taskShader = glCreateShader(GL_TASK_SHADER_NV);
        glShaderSource(taskShader, 1, &meshTaskShaderSource, nullptr);
        glCompileShader(taskShader);
        if (!checkShaderCompilation(taskShader, "MESH_TASK")) {
            std::cerr << "Task shader compilation failed, disabling mesh shaders" << std::endl;
            g_meshShadersAvailable = false;
            g_enableMeshShaders = false;
            g_generateMeshlets = false;
        }

        if (g_meshShadersAvailable) {
            // Compile mesh shader
            GLuint meshShader = glCreateShader(GL_MESH_SHADER_NV);
            glShaderSource(meshShader, 1, &meshShaderSource, nullptr);
            glCompileShader(meshShader);
            if (!checkShaderCompilation(meshShader, "MESH_SHADER")) {
                std::cerr << "Mesh shader compilation failed, disabling mesh shaders" << std::endl;
                g_meshShadersAvailable = false;
                g_enableMeshShaders = false;
                g_generateMeshlets = false;
                glDeleteShader(taskShader);
            } else {
                // Compile fragment shader
                GLuint meshFragShader = glCreateShader(GL_FRAGMENT_SHADER);
                glShaderSource(meshFragShader, 1, &meshFragmentShaderSource, nullptr);
                glCompileShader(meshFragShader);
                if (!checkShaderCompilation(meshFragShader, "MESH_FRAGMENT")) {
                    std::cerr << "Mesh fragment shader compilation failed, disabling mesh shaders" << std::endl;
                    g_meshShadersAvailable = false;
                    g_enableMeshShaders = false;
                    g_generateMeshlets = false;
                    glDeleteShader(taskShader);
                    glDeleteShader(meshShader);
                } else {
                    // Link program
                    meshShaderProgram = glCreateProgram();
                    glAttachShader(meshShaderProgram, taskShader);
                    glAttachShader(meshShaderProgram, meshShader);
                    glAttachShader(meshShaderProgram, meshFragShader);
                    glLinkProgram(meshShaderProgram);

                    if (!checkProgramLinking(meshShaderProgram)) {
                        std::cerr << "Mesh shader program linking failed, disabling mesh shaders" << std::endl;
                        g_meshShadersAvailable = false;
                        g_enableMeshShaders = false;
                        g_generateMeshlets = false;
                        glDeleteProgram(meshShaderProgram);
                        meshShaderProgram = 0;
                    } else {
                        std::cout << "Mesh shader program compiled successfully!" << std::endl;
                    }

                    glDeleteShader(taskShader);
                    glDeleteShader(meshShader);
                    glDeleteShader(meshFragShader);
                }
            }
        }

        // Create UBOs for mesh shader
        if (g_meshShadersAvailable && meshShaderProgram != 0) {
            // Mesh shader data UBO (binding = 3): mat4 viewProj, vec3 chunkOffset, uint meshletCount
            glGenBuffers(1, &meshShaderDataUBO);
            glBindBuffer(GL_UNIFORM_BUFFER, meshShaderDataUBO);
            glBufferData(GL_UNIFORM_BUFFER, sizeof(glm::mat4) + 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
            glBindBufferBase(GL_UNIFORM_BUFFER, 3, meshShaderDataUBO);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);

            // Frustum planes UBO (binding = 4): 6 x vec4
            glGenBuffers(1, &frustumPlanesUBO);
            glBindBuffer(GL_UNIFORM_BUFFER, frustumPlanesUBO);
            glBufferData(GL_UNIFORM_BUFFER, 6 * sizeof(glm::vec4), nullptr, GL_DYNAMIC_DRAW);
            glBindBufferBase(GL_UNIFORM_BUFFER, 4, frustumPlanesUBO);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);

            std::cout << "Mesh shader UBOs created" << std::endl;
        }
    }

    // Get uniform locations for loading shader
    GLint loadingOffsetLoc = glGetUniformLocation(loadingShaderProgram, "uOffset");
    GLint loadingScaleLoc = glGetUniformLocation(loadingShaderProgram, "uScale");
    GLint loadingColorLoc = glGetUniformLocation(loadingShaderProgram, "uColor");

    // Create quad VAO for loading screen
    GLuint loadingVAO, loadingVBO;
    float loadingQuad[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f
    };
    glGenVertexArrays(1, &loadingVAO);
    glGenBuffers(1, &loadingVBO);
    glBindVertexArray(loadingVAO);
    glBindBuffer(GL_ARRAY_BUFFER, loadingVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(loadingQuad), loadingQuad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Get uniform locations for shadow shader
    GLint shadowLightSpaceMatrixLoc = glGetUniformLocation(shadowShaderProgram, "lightSpaceMatrix");
    GLint shadowChunkOffsetLoc = glGetUniformLocation(shadowShaderProgram, "chunkOffset");

    // Get uniform locations for main shader
    GLint viewLoc = glGetUniformLocation(shaderProgram, "view");
    GLint projectionLoc = glGetUniformLocation(shaderProgram, "projection");
    GLint lightSpaceMatrixLoc = glGetUniformLocation(shaderProgram, "lightSpaceMatrix");
    GLint lightDirLoc = glGetUniformLocation(shaderProgram, "lightDir");
    GLint lightColorLoc = glGetUniformLocation(shaderProgram, "lightColor");
    GLint shadowMapLoc = glGetUniformLocation(shaderProgram, "shadowMap");
    GLint shadowStrengthLoc = glGetUniformLocation(shaderProgram, "shadowStrength");
    GLint ambientColorLoc = glGetUniformLocation(shaderProgram, "ambientColor");
    GLint skyColorLoc = glGetUniformLocation(shaderProgram, "skyColor");
    GLint fogDensityLoc = glGetUniformLocation(shaderProgram, "fogDensity");
    GLint texAtlasLoc = glGetUniformLocation(shaderProgram, "texAtlas");
    GLint underwaterLoc = glGetUniformLocation(shaderProgram, "isUnderwater");
    GLint timeLoc = glGetUniformLocation(shaderProgram, "time");
    GLint cameraPosLoc = glGetUniformLocation(shaderProgram, "cameraPos");
    GLint chunkOffsetLoc = glGetUniformLocation(shaderProgram, "chunkOffset");  // Packed vertex format
    GLint renderDistLoc = glGetUniformLocation(shaderProgram, "renderDistanceBlocks");

    // Get uniform locations for water shader
    GLint waterViewLoc = glGetUniformLocation(waterShaderProgram, "view");
    GLint waterProjectionLoc = glGetUniformLocation(waterShaderProgram, "projection");
    GLint waterTimeLoc = glGetUniformLocation(waterShaderProgram, "time");
    GLint waterLightDirLoc = glGetUniformLocation(waterShaderProgram, "lightDir");
    GLint waterLightColorLoc = glGetUniformLocation(waterShaderProgram, "lightColor");
    GLint waterAmbientColorLoc = glGetUniformLocation(waterShaderProgram, "ambientColor");
    GLint waterSkyColorLoc = glGetUniformLocation(waterShaderProgram, "skyColor");
    GLint waterFogDensityLoc = glGetUniformLocation(waterShaderProgram, "fogDensity");
    GLint waterTexAtlasLoc = glGetUniformLocation(waterShaderProgram, "texAtlas");
    GLint waterUnderwaterLoc = glGetUniformLocation(waterShaderProgram, "isUnderwater");
    GLint waterTexBoundsLoc = glGetUniformLocation(waterShaderProgram, "waterTexBounds");
    GLint waterCameraPosLoc = glGetUniformLocation(waterShaderProgram, "cameraPos");
    GLint waterLodDistanceLoc = glGetUniformLocation(waterShaderProgram, "waterLodDistance");

    // Get uniform locations for sky shader
    GLint skyInvViewLoc = glGetUniformLocation(skyShaderProgram, "invView");
    GLint skyInvProjectionLoc = glGetUniformLocation(skyShaderProgram, "invProjection");
    GLint skyCameraPosLoc = glGetUniformLocation(skyShaderProgram, "cameraPos");
    GLint skySunDirLoc = glGetUniformLocation(skyShaderProgram, "sunDirection");
    GLint skySkyTopLoc = glGetUniformLocation(skyShaderProgram, "skyColorTop");
    GLint skySkyBottomLoc = glGetUniformLocation(skyShaderProgram, "skyColorBottom");
    GLint skyTimeLoc = glGetUniformLocation(skyShaderProgram, "time");
    GLint skyCloudStyleLoc = glGetUniformLocation(skyShaderProgram, "cloudStyle");
    GLint skyCloudRenderDistLoc = glGetUniformLocation(skyShaderProgram, "cloudRenderDistance");

    // Get uniform locations for precipitation shader
    GLint precipViewLoc = glGetUniformLocation(precipShaderProgram, "view");
    GLint precipProjectionLoc = glGetUniformLocation(precipShaderProgram, "projection");
    GLint precipTimeLoc = glGetUniformLocation(precipShaderProgram, "time");
    GLint precipWeatherTypeLoc = glGetUniformLocation(precipShaderProgram, "weatherType");
    GLint precipIntensityLoc = glGetUniformLocation(precipShaderProgram, "intensity");
    GLint precipLightColorLoc = glGetUniformLocation(precipShaderProgram, "lightColor");

    // ============================================
    // DEFERRED RENDERING UNIFORM LOCATIONS
    // ============================================

    // Z-prepass shader uniforms
    GLint zPrepassViewLoc = glGetUniformLocation(zPrepassProgram, "view");
    GLint zPrepassProjectionLoc = glGetUniformLocation(zPrepassProgram, "projection");
    GLint zPrepassChunkOffsetLoc = glGetUniformLocation(zPrepassProgram, "chunkOffset");
    GLint zPrepassTexAtlasLoc = glGetUniformLocation(zPrepassProgram, "texAtlas");

    // G-Buffer shader uniforms
    GLint gBufferViewLoc = glGetUniformLocation(gBufferProgram, "view");
    GLint gBufferProjectionLoc = glGetUniformLocation(gBufferProgram, "projection");
    GLint gBufferChunkOffsetLoc = glGetUniformLocation(gBufferProgram, "chunkOffset");
    GLint gBufferTexAtlasLoc = glGetUniformLocation(gBufferProgram, "texAtlas");

    // Debug: verify G-buffer uniform locations
    std::cout << "G-buffer uniform locations:" << std::endl;
    std::cout << "  view: " << gBufferViewLoc << std::endl;
    std::cout << "  projection: " << gBufferProjectionLoc << std::endl;
    std::cout << "  chunkOffset: " << gBufferChunkOffsetLoc << std::endl;
    std::cout << "  texAtlas: " << gBufferTexAtlasLoc << std::endl;
    if (gBufferViewLoc == -1 || gBufferProjectionLoc == -1 || gBufferChunkOffsetLoc == -1) {
        std::cerr << "WARNING: G-buffer shader missing required uniforms!" << std::endl;
    }

    // Composite shader uniforms
    GLint compositeGPositionLoc = glGetUniformLocation(compositeProgram, "gPosition");
    GLint compositeGNormalLoc = glGetUniformLocation(compositeProgram, "gNormal");
    GLint compositeGAlbedoLoc = glGetUniformLocation(compositeProgram, "gAlbedo");
    GLint compositeGDepthLoc = glGetUniformLocation(compositeProgram, "gDepth");
    GLint compositeSsaoTexLoc = glGetUniformLocation(compositeProgram, "ssaoTexture");
    GLint compositeEnableSsaoLoc = glGetUniformLocation(compositeProgram, "enableSSAO");
    GLint compositeCascadeMapsLoc = glGetUniformLocation(compositeProgram, "cascadeShadowMaps");
    GLint compositeCascadeMatricesLoc = glGetUniformLocation(compositeProgram, "cascadeMatrices");
    GLint compositeCascadeSplitsLoc = glGetUniformLocation(compositeProgram, "cascadeSplits");
    GLint compositeShadowStrengthLoc = glGetUniformLocation(compositeProgram, "shadowStrength");
    GLint compositeLightDirLoc = glGetUniformLocation(compositeProgram, "lightDir");
    GLint compositeLightColorLoc = glGetUniformLocation(compositeProgram, "lightColor");
    GLint compositeAmbientColorLoc = glGetUniformLocation(compositeProgram, "ambientColor");
    GLint compositeSkyColorLoc = glGetUniformLocation(compositeProgram, "skyColor");
    GLint compositeCameraPosLoc = glGetUniformLocation(compositeProgram, "cameraPos");
    GLint compositeTimeLoc = glGetUniformLocation(compositeProgram, "time");
    GLint compositeFogDensityLoc = glGetUniformLocation(compositeProgram, "fogDensity");
    GLint compositeUnderwaterLoc = glGetUniformLocation(compositeProgram, "isUnderwater");
    GLint compositeDebugModeLoc = glGetUniformLocation(compositeProgram, "debugMode");
    GLint compositeRenderDistLoc = glGetUniformLocation(compositeProgram, "renderDistanceBlocks");
    GLint compositeInvViewProjLoc = glGetUniformLocation(compositeProgram, "invViewProj");

    // Debug: verify composite shader uniform locations
    std::cout << "Composite shader uniform locations:" << std::endl;
    std::cout << "  gPosition: " << compositeGPositionLoc << std::endl;
    std::cout << "  gNormal: " << compositeGNormalLoc << std::endl;
    std::cout << "  gAlbedo: " << compositeGAlbedoLoc << std::endl;
    std::cout << "  debugMode: " << compositeDebugModeLoc << std::endl;

    // SSAO shader uniforms (kernel samples now in UBO, no uniform location needed)
    GLint ssaoGPositionLoc = glGetUniformLocation(ssaoProgram, "gPosition");
    GLint ssaoGNormalLoc = glGetUniformLocation(ssaoProgram, "gNormal");
    GLint ssaoGDepthLoc = glGetUniformLocation(ssaoProgram, "gDepth");
    GLint ssaoNoiseLoc = glGetUniformLocation(ssaoProgram, "noiseTexture");
    GLint ssaoProjectionLoc = glGetUniformLocation(ssaoProgram, "projection");
    GLint ssaoViewLoc = glGetUniformLocation(ssaoProgram, "view");
    GLint ssaoNoiseScaleLoc = glGetUniformLocation(ssaoProgram, "noiseScale");
    GLint ssaoRadiusLoc = glGetUniformLocation(ssaoProgram, "radius");
    GLint ssaoBiasLoc = glGetUniformLocation(ssaoProgram, "bias");

    // SSAO Blur shader uniforms
    GLint ssaoBlurInputLoc = glGetUniformLocation(ssaoBlurProgram, "ssaoInput");

    // Hi-Z downsample shader uniforms
    GLint hiZSrcDepthLoc = glGetUniformLocation(hiZDownsampleProgram, "srcDepth");
    GLint hiZSrcLevelLoc = glGetUniformLocation(hiZDownsampleProgram, "srcLevel");

    // Occlusion culling shader uniforms
    GLint occlusionHiZLoc = glGetUniformLocation(occlusionCullProgram, "hiZBuffer");
    GLint occlusionViewProjLoc = glGetUniformLocation(occlusionCullProgram, "viewProj");
    GLint occlusionNumMipsLoc = glGetUniformLocation(occlusionCullProgram, "numMipLevels");
    GLint occlusionScreenSizeLoc = glGetUniformLocation(occlusionCullProgram, "screenSize");
    GLint occlusionChunkCountLoc = glGetUniformLocation(occlusionCullProgram, "chunkCount");

    // FSR EASU (upscaling) shader uniforms
    GLint fsrEASUInputLoc = glGetUniformLocation(fsrEASUProgram, "inputTexture");
    GLint fsrEASUInputSizeLoc = glGetUniformLocation(fsrEASUProgram, "inputSize");
    GLint fsrEASUOutputSizeLoc = glGetUniformLocation(fsrEASUProgram, "outputSize");
    GLint fsrEASUCon0Loc = glGetUniformLocation(fsrEASUProgram, "con0");
    GLint fsrEASUCon1Loc = glGetUniformLocation(fsrEASUProgram, "con1");
    GLint fsrEASUCon2Loc = glGetUniformLocation(fsrEASUProgram, "con2");
    GLint fsrEASUCon3Loc = glGetUniformLocation(fsrEASUProgram, "con3");

    // FSR RCAS (sharpening) shader uniforms
    GLint fsrRCASInputLoc = glGetUniformLocation(fsrRCASProgram, "inputTexture");
    GLint fsrRCASTexelSizeLoc = glGetUniformLocation(fsrRCASProgram, "texelSize");
    GLint fsrRCASSharpnessLoc = glGetUniformLocation(fsrRCASProgram, "sharpness");
    (void)fsrRCASInputLoc; (void)fsrRCASTexelSizeLoc; (void)fsrRCASSharpnessLoc; // Reserved for RCAS sharpening pass

    std::cout << "FSR uniform locations retrieved." << std::endl;

    // SSAO parameters
    float ssaoRadius = g_config.ssaoRadius;   // From config
    float ssaoBias = g_config.ssaoBias;       // From config
    // Note: g_enableSSAO and g_useDeferredRendering are global for keyboard toggle

    // Create precipitation particle system
    const int MAX_PARTICLES = 10000;
    struct PrecipParticle {
        float x, y, z;      // Position
        float size;         // Size
        float alpha;        // Alpha
        float speed;        // Fall speed
        float offset;       // Random offset for animation
    };
    std::vector<PrecipParticle> particles(MAX_PARTICLES);

    // Initialize particles with random positions (will be set properly on first frame)
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> posDist(-80.0f, 80.0f);
    std::uniform_real_distribution<float> heightDist(0.0f, 60.0f);
    std::uniform_real_distribution<float> sizeDist(1.0f, 3.0f);
    std::uniform_real_distribution<float> alphaDist(0.3f, 1.0f);
    std::uniform_real_distribution<float> speedDist(15.0f, 25.0f);
    std::uniform_real_distribution<float> offsetDist(0.0f, 100.0f);
    bool particlesInitialized = false;

    for (int i = 0; i < MAX_PARTICLES; i++) {
        // These are world space positions - will be offset from camera on first update
        particles[i].x = posDist(rng);
        particles[i].y = heightDist(rng);
        particles[i].z = posDist(rng);
        particles[i].size = sizeDist(rng);
        particles[i].alpha = alphaDist(rng);
        particles[i].speed = speedDist(rng);
        particles[i].offset = offsetDist(rng);
    }

    // Create precipitation VAO/VBO
    GLuint precipVAO, precipVBO;
    glGenVertexArrays(1, &precipVAO);
    glGenBuffers(1, &precipVBO);
    glBindVertexArray(precipVAO);
    glBindBuffer(GL_ARRAY_BUFFER, precipVBO);
    // Will update buffer each frame, so use GL_DYNAMIC_DRAW
    glBufferData(GL_ARRAY_BUFFER, MAX_PARTICLES * 5 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    // Position (vec3)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Size (float)
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // Alpha (float)
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Create fullscreen quad for sky rendering
    GLuint skyVAO, skyVBO;
    float quadVertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f
    };
    glGenVertexArrays(1, &skyVAO);
    glGenBuffers(1, &skyVBO);
    glBindVertexArray(skyVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Calculate water texture bounds in atlas (slot 11)
    // Using TextureAtlas::getUV formula: slot 11 -> x=11, y=0, u=11/16, v=0, size=1/16
    glm::vec4 waterTexBounds = TextureAtlas::getUV(11);  // Returns (u0, v0, u1, v1)

    // Enable depth testing
    glEnable(GL_DEPTH_TEST);

    // Enable alpha blending for water transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Enable face culling for performance
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // Create shadow map framebuffer and texture
    const unsigned int SHADOW_WIDTH = 4096, SHADOW_HEIGHT = 4096;
    GLuint shadowMapFBO;
    glGenFramebuffers(1, &shadowMapFBO);

    GLuint shadowMapTexture;
    glGenTextures(1, &shadowMapTexture);
    glBindTexture(GL_TEXTURE_2D, shadowMapTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    // Attach depth texture to framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, shadowMapFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMapTexture, 0);
    glDrawBuffer(GL_NONE);  // No color buffer
    glReadBuffer(GL_NONE);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Shadow map framebuffer is not complete!" << std::endl;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    std::cout << "Shadow map created (" << SHADOW_WIDTH << "x" << SHADOW_HEIGHT << ")" << std::endl;

    // ============================================
    // CALCULATE RENDER RESOLUTION (for FSR upscaling)
    // ============================================
    g_enableFSR = g_config.enableFSR;
    g_renderScale = HardwareInfo::getRenderScale(g_config.upscaleMode);
    RENDER_WIDTH = static_cast<int>(WINDOW_WIDTH * g_renderScale);
    RENDER_HEIGHT = static_cast<int>(WINDOW_HEIGHT * g_renderScale);

    // Ensure minimum render resolution (avoid divide by zero issues)
    RENDER_WIDTH = std::max(RENDER_WIDTH, 320);
    RENDER_HEIGHT = std::max(RENDER_HEIGHT, 180);

    // Round to multiples of 8 for better GPU efficiency
    RENDER_WIDTH = (RENDER_WIDTH + 7) & ~7;
    RENDER_HEIGHT = (RENDER_HEIGHT + 7) & ~7;

    std::cout << "Render resolution: " << RENDER_WIDTH << "x" << RENDER_HEIGHT;
    if (g_enableFSR && g_renderScale < 1.0f) {
        std::cout << " (FSR upscaling to " << WINDOW_WIDTH << "x" << WINDOW_HEIGHT << ", scale=" << g_renderScale << ")";
    }
    std::cout << std::endl;

    // ============================================
    // CREATE G-BUFFER FOR DEFERRED RENDERING
    // ============================================
    std::cout << "Creating G-buffer..." << std::endl;

    glGenFramebuffers(1, &gBufferFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, gBufferFBO);

    // Use render resolution for G-buffer (supports FSR upscaling)
    int gBufferWidth = g_enableFSR ? RENDER_WIDTH : WINDOW_WIDTH;
    int gBufferHeight = g_enableFSR ? RENDER_HEIGHT : WINDOW_HEIGHT;

    // Position buffer (RGB16F) - world position + vertex AO in alpha
    glGenTextures(1, &gPosition);
    glBindTexture(GL_TEXTURE_2D, gPosition);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gBufferWidth, gBufferHeight, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gPosition, 0);

    // Normal buffer (RGB16F) - world normal + light level in alpha
    glGenTextures(1, &gNormal);
    glBindTexture(GL_TEXTURE_2D, gNormal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gBufferWidth, gBufferHeight, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gNormal, 0);

    // Albedo buffer (RGBA8) - base color + emission flag
    glGenTextures(1, &gAlbedo);
    glBindTexture(GL_TEXTURE_2D, gAlbedo);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, gBufferWidth, gBufferHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gAlbedo, 0);

    // Depth buffer (DEPTH32F) - for Hi-Z and SSAO
    glGenTextures(1, &gDepth);
    glBindTexture(GL_TEXTURE_2D, gDepth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, gBufferWidth, gBufferHeight, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gDepth, 0);

    // Tell OpenGL which color attachments we'll use
    GLenum gBufferAttachments[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3, gBufferAttachments);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "G-buffer framebuffer is not complete!" << std::endl;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    std::cout << "G-buffer created (" << gBufferWidth << "x" << gBufferHeight << ")" << std::endl;

    // ============================================
    // CREATE SCENE FBO (for FSR - composite output at render resolution)
    // ============================================
    if (g_enableFSR && g_renderScale < 1.0f) {
        std::cout << "Creating scene FBO for FSR..." << std::endl;

        glGenFramebuffers(1, &sceneFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);

        // Scene color texture at render resolution (output of composite pass)
        glGenTextures(1, &sceneColorTexture);
        glBindTexture(GL_TEXTURE_2D, sceneColorTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, RENDER_WIDTH, RENDER_HEIGHT, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);  // Linear for FSR sampling
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColorTexture, 0);

        // Depth renderbuffer (for sky rendering in scene FBO)
        glGenRenderbuffers(1, &sceneDepthRBO);
        glBindRenderbuffer(GL_RENDERBUFFER, sceneDepthRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, RENDER_WIDTH, RENDER_HEIGHT);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sceneDepthRBO);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "Scene FBO is not complete!" << std::endl;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        std::cout << "Scene FBO created (" << RENDER_WIDTH << "x" << RENDER_HEIGHT << ")" << std::endl;
    }

    // ============================================
    // CREATE CASCADE SHADOW MAPS (3 cascades)
    // ============================================
    std::cout << "Creating cascade shadow maps..." << std::endl;

    glGenTextures(1, &cascadeShadowMaps);
    glBindTexture(GL_TEXTURE_2D_ARRAY, cascadeShadowMaps);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F,
                 CASCADE_RESOLUTION, CASCADE_RESOLUTION, NUM_CASCADES,
                 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    float cascadeBorderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, cascadeBorderColor);

    glGenFramebuffers(1, &cascadeShadowFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, cascadeShadowFBO);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, cascadeShadowMaps, 0, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Cascade shadow framebuffer is not complete!" << std::endl;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    std::cout << "Cascade shadow maps created (3x " << CASCADE_RESOLUTION << "x" << CASCADE_RESOLUTION << ")" << std::endl;

    // ============================================
    // CREATE SSAO RESOURCES
    // ============================================
    std::cout << "Creating SSAO resources..." << std::endl;

    // Generate SSAO kernel (random samples in hemisphere)
    std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
    std::default_random_engine generator;

    for (int i = 0; i < SSAO_KERNEL_SIZE; i++) {
        glm::vec3 sample(
            randomFloats(generator) * 2.0f - 1.0f,
            randomFloats(generator) * 2.0f - 1.0f,
            randomFloats(generator)  // Only positive Z (hemisphere)
        );
        sample = glm::normalize(sample);
        sample *= randomFloats(generator);

        // Scale samples to be more concentrated near origin
        float scale = static_cast<float>(i) / static_cast<float>(SSAO_KERNEL_SIZE);
        scale = 0.1f + scale * scale * 0.9f;  // lerp(0.1, 1.0, scale^2)
        sample *= scale;

        ssaoKernel.push_back(sample);
    }

    // OPTIMIZATION: Create UBO for SSAO kernel (upload once, not per-frame)
    // Convert vec3 to vec4 for std140 alignment
    std::vector<glm::vec4> kernelData;
    kernelData.reserve(SSAO_KERNEL_SIZE);
    for (const auto& sample : ssaoKernel) {
        kernelData.push_back(glm::vec4(sample, 0.0f));
    }

    glGenBuffers(1, &ssaoKernelUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, ssaoKernelUBO);
    glBufferData(GL_UNIFORM_BUFFER, kernelData.size() * sizeof(glm::vec4), kernelData.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ssaoKernelUBO);  // Binding point 0 matches shader
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // Generate noise texture for random rotation
    std::vector<glm::vec3> ssaoNoise;
    for (int i = 0; i < SSAO_NOISE_SIZE * SSAO_NOISE_SIZE; i++) {
        glm::vec3 noise(
            randomFloats(generator) * 2.0f - 1.0f,
            randomFloats(generator) * 2.0f - 1.0f,
            0.0f
        );
        ssaoNoise.push_back(noise);
    }

    glGenTextures(1, &ssaoNoiseTexture);
    glBindTexture(GL_TEXTURE_2D, ssaoNoiseTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, SSAO_NOISE_SIZE, SSAO_NOISE_SIZE,
                 0, GL_RGB, GL_FLOAT, ssaoNoise.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // Create SSAO FBO (use render resolution if FSR enabled)
    int ssaoWidth = g_enableFSR ? RENDER_WIDTH : WINDOW_WIDTH;
    int ssaoHeight = g_enableFSR ? RENDER_HEIGHT : WINDOW_HEIGHT;

    glGenFramebuffers(1, &ssaoFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);

    glGenTextures(1, &ssaoColorBuffer);
    glBindTexture(GL_TEXTURE_2D, ssaoColorBuffer);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ssaoWidth, ssaoHeight, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssaoColorBuffer, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "SSAO framebuffer is not complete!" << std::endl;
    }

    // Create SSAO blur FBO
    glGenFramebuffers(1, &ssaoBlurFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFBO);

    glGenTextures(1, &ssaoBlurBuffer);
    glBindTexture(GL_TEXTURE_2D, ssaoBlurBuffer);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ssaoWidth, ssaoHeight, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssaoBlurBuffer, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "SSAO blur framebuffer is not complete!" << std::endl;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    std::cout << "SSAO resources created (" << ssaoWidth << "x" << ssaoHeight << ", kernel size: " << SSAO_KERNEL_SIZE << ")" << std::endl;

    // ============================================
    // CREATE HI-Z BUFFER FOR OCCLUSION CULLING
    // ============================================
    std::cout << "Creating Hi-Z buffer..." << std::endl;

    hiZLevels = 1 + static_cast<int>(floor(log2(std::max(WINDOW_WIDTH, WINDOW_HEIGHT))));

    glGenTextures(1, &hiZTexture);
    glBindTexture(GL_TEXTURE_2D, hiZTexture);
    glTexStorage2D(GL_TEXTURE_2D, hiZLevels, GL_R32F, WINDOW_WIDTH, WINDOW_HEIGHT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &hiZFBO);
    std::cout << "Hi-Z buffer created (" << hiZLevels << " mip levels)" << std::endl;

    // Create SSBOs for occlusion culling
    // With sub-chunk culling: (renderDistance * 2 + 1)^2 * 16 sub-chunks per column
    // For render distance 10: 441 * 16 = 7,056 sub-chunks max
    const int MAX_CULLING_SUBCHUNKS = 16384;

    // Sub-chunk bounds SSBO: each sub-chunk has min (vec4) and max (vec4) = 32 bytes
    glGenBuffers(1, &chunkBoundsSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunkBoundsSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, MAX_CULLING_SUBCHUNKS * 32, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // OPTIMIZATION: Double-buffered visibility SSBOs to avoid GPU stalls
    // Frame N writes to buffer[writeIndex], reads cached results from previous frame
    glGenBuffers(2, visibilitySSBO);
    for (int i = 0; i < 2; i++) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, visibilitySSBO[i]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, MAX_CULLING_SUBCHUNKS * sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    std::cout << "Occlusion culling SSBOs created (max " << MAX_CULLING_SUBCHUNKS << " sub-chunks, double-buffered)" << std::endl;

    // ============================================
    // CREATE FULL-SCREEN QUAD FOR DEFERRED PASSES
    // ============================================
    float screenQuadVertices[] = {
        // positions   // texCoords
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };

    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(screenQuadVertices), screenQuadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);
    std::cout << "Full-screen quad created" << std::endl;

    // ============================================
    // CREATE GPU TIMER QUERIES
    // ============================================
    for (int frame = 0; frame < 2; frame++) {
        glGenQueries(NUM_GPU_TIMERS, gpuTimerQueries[frame]);
    }
    // Initialize queries with dummy values to avoid errors on first read
    for (int i = 0; i < NUM_GPU_TIMERS; i++) {
        glBeginQuery(GL_TIME_ELAPSED, gpuTimerQueries[0][i]);
        glEndQuery(GL_TIME_ELAPSED);
        glBeginQuery(GL_TIME_ELAPSED, gpuTimerQueries[1][i]);
        glEndQuery(GL_TIME_ELAPSED);
    }
    std::cout << "GPU timer queries created" << std::endl;

    // Initialize render timing log file
    initRenderTimingLog();
    std::cout << "Render timing log initialized (RenderTime.txt)" << std::endl;

    // Generate texture atlas
    std::cout << "\nGenerating texture atlas..." << std::endl;
    TextureAtlas textureAtlas;
    textureAtlas.generate();
    std::cout << "Texture atlas generated (256x256)" << std::endl;

    // Initialize vertex pool for reduced allocation overhead
    if (g_useVertexPool) {
        if (VertexPool::getInstance().initialize()) {
            std::cout << "Vertex pool initialized (" << VERTEX_POOL_SIZE_MB << "MB)" << std::endl;
        } else {
            std::cout << "Vertex pool failed to initialize - using per-chunk allocation" << std::endl;
            g_useVertexPool = false;
        }
    }

    // Initialize UI elements
    Crosshair crosshair;
    crosshair.init();

    BlockHighlight blockHighlight;
    blockHighlight.init();

    // Initialize thread pool with config settings
    world.initThreadPool(g_config.chunkThreads, g_config.meshThreads);

    // Initialize indirect rendering buffers for batched rendering
    world.initIndirectRendering();

    // Configure world settings from config
    world.renderDistance = g_config.renderDistance;
    world.unloadDistance = g_config.renderDistance + 4;  // Unload a bit beyond render
    world.maxChunksPerFrame = g_config.maxChunksPerFrame;
    world.maxMeshesPerFrame = g_config.maxMeshesPerFrame;

    // Create player placeholder (will set proper spawn after chunks load)
    glm::vec3 spawnPos(8.0f, 100.0f, 8.0f);
    Player localPlayer(spawnPos);
    player = &localPlayer;
    player->attachCamera(&camera);
    player->isFlying = true;  // Start in fly mode for convenience

    // Calculate total chunks to load for loading screen
    int loadRadius = world.renderDistance;
    totalChunksToLoad = 0;

    // Queue all chunks within render distance for async generation
    std::cout << "\nQueuing chunks for generation (render distance: " << loadRadius << ")..." << std::endl;
    for (int dx = -loadRadius; dx <= loadRadius; dx++) {
        for (int dz = -loadRadius; dz <= loadRadius; dz++) {
            glm::ivec2 chunkPos(dx, dz);
            world.chunkThreadPool->queueChunk(chunkPos);
            totalChunksToLoad++;
        }
    }
    std::cout << "Queued " << totalChunksToLoad << " chunks for generation" << std::endl;

    // Set loading state
    gameState = GameState::LOADING;
    chunksLoaded = 0;
    meshesBuilt = 0;
    loadingMessage = "Generating terrain...";
    world.burstMode = true;  // Enable burst mode for maximum loading speed
    glfwSwapInterval(0);     // Disable VSync during loading for max speed

    std::cout << "\n=== Voxel Engine Started ===" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  WASD - Move" << std::endl;
    std::cout << "  Mouse - Look around" << std::endl;
    std::cout << "  Space - Jump (or fly up)" << std::endl;
    std::cout << "  Ctrl - Descend (fly mode)" << std::endl;
    std::cout << "  Shift - Sprint" << std::endl;
    std::cout << "  Left Click - Break block" << std::endl;
    std::cout << "  Right Click - Place block" << std::endl;
    std::cout << "  Scroll/1-9 - Select block" << std::endl;
    std::cout << "  F1 - Toggle wireframe" << std::endl;
    std::cout << "  F2 - Toggle fly mode" << std::endl;
    std::cout << "  F3 - Toggle daylight cycle" << std::endl;
    std::cout << "  F4 - Toggle cloud style (Simple/Volumetric)" << std::endl;
    std::cout << "  F5 - Toggle weather (Clear/Rain/Snow/Thunderstorm)" << std::endl;
    std::cout << "  F6 - Toggle noclip (fly through blocks)" << std::endl;
    std::cout << "  ESC - Exit" << std::endl;

    // FPS counter
    double lastFPSTime = glfwGetTime();
    int frameCount = 0;

    // Day/night cycle settings (from config)
    float dayLength = g_config.dayLength;
    float timeOfDay = 0.25f;   // Start at sunrise (0=midnight, 0.25=sunrise, 0.5=noon, 0.75=sunset)
    float fogDensity = g_config.fogDensity;

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        // Calculate delta time
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Poll events first
        glfwPollEvents();

        // ============================================================
        // LOADING STATE - Preload chunks before gameplay
        // ============================================================
        if (gameState == GameState::LOADING) {
            // Process completed chunks from thread pool - no limit during loading
            auto completed = world.chunkThreadPool->getCompletedChunks(1000);
            for (auto& result : completed) {
                if (world.getChunk(result.position) == nullptr) {
                    world.chunks[result.position] = std::move(result.chunk);
                    world.chunks[result.position]->isDirty = true;
                    chunksLoaded++;
                }
            }

            // Calculate progress
            float chunkProgress = static_cast<float>(chunksLoaded) / static_cast<float>(totalChunksToLoad);

            // Once all chunks are loaded, build meshes using thread pool
            if (chunksLoaded >= totalChunksToLoad) {
                loadingMessage = "Building meshes...";

                // Use world.update() with burst mode - this properly uses the thread pool
                // for mesh generation AND uploads to GPU incrementally (bounded RAM usage)
                world.burstMode = true;
                glm::ivec2 centerChunk = Chunk::worldToChunkPos(spawnPos);
                world.updateMeshes(centerChunk);

                // Count how many meshes are done
                meshesBuilt = static_cast<int>(world.meshes.size());

                // Check if all meshes are built (no dirty chunks with all neighbors)
                bool allMeshesBuilt = true;
                int pendingMeshes = 0;
                for (auto& [pos, chunk] : world.chunks) {
                    if (chunk->isDirty) {
                        bool allNeighborsExist =
                            world.getChunk(glm::ivec2(pos.x - 1, pos.y)) != nullptr &&
                            world.getChunk(glm::ivec2(pos.x + 1, pos.y)) != nullptr &&
                            world.getChunk(glm::ivec2(pos.x, pos.y - 1)) != nullptr &&
                            world.getChunk(glm::ivec2(pos.x, pos.y + 1)) != nullptr;
                        if (allNeighborsExist) {
                            allMeshesBuilt = false;
                            pendingMeshes++;
                        }
                    }
                }

                // Also check if any meshes are still being generated in thread pool
                if (world.chunkThreadPool && world.chunkThreadPool->hasPendingMeshes()) {
                    allMeshesBuilt = false;
                }

                if (allMeshesBuilt) {
                    // Find spawn point
                    for (int y = 200; y > 0; y--) {
                        if (isBlockSolid(world.getBlock(8, y, 8))) {
                            spawnPos.y = static_cast<float>(y + 1);
                            break;
                        }
                    }
                    player->position = spawnPos;
                    camera.position = spawnPos + glm::vec3(0, Player::EYE_HEIGHT, 0);

                    std::cout << "Loading complete! " << chunksLoaded << " chunks, " << meshesBuilt << " meshes" << std::endl;
                    std::cout << "Player spawned at: " << spawnPos.x << ", " << spawnPos.y << ", " << spawnPos.z << std::endl;

                    world.burstMode = false;  // Disable burst mode for smoother gameplay
                    glfwSwapInterval(g_config.vsync ? 1 : 0);  // Restore VSync setting
                    gameState = GameState::PLAYING;
                }
            }

            // Render loading screen
            glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glUseProgram(loadingShaderProgram);
            glBindVertexArray(loadingVAO);

            // Draw background bar (dark gray)
            float barWidth = 0.6f;
            float barHeight = 0.05f;
            float barY = -0.1f;
            glUniform2f(loadingOffsetLoc, 0.0f, barY);
            glUniform2f(loadingScaleLoc, barWidth, barHeight);
            glUniform3f(loadingColorLoc, 0.2f, 0.2f, 0.25f);
            glDrawArrays(GL_TRIANGLES, 0, 6);

            // Draw progress bar (green)
            float progress = chunkProgress;
            if (chunksLoaded >= totalChunksToLoad) {
                // During mesh building, estimate progress
                int totalMeshes = static_cast<int>(world.chunks.size());
                progress = 0.5f + 0.5f * (static_cast<float>(meshesBuilt) / static_cast<float>(std::max(1, totalMeshes)));
            } else {
                progress = 0.5f * chunkProgress;  // Chunk loading is 50% of total
            }
            float progressWidth = barWidth * progress;
            glUniform2f(loadingOffsetLoc, -barWidth + progressWidth, barY);
            glUniform2f(loadingScaleLoc, progressWidth, barHeight * 0.8f);
            glUniform3f(loadingColorLoc, 0.2f, 0.7f, 0.3f);
            glDrawArrays(GL_TRIANGLES, 0, 6);

            // Draw title bar (light)
            glUniform2f(loadingOffsetLoc, 0.0f, 0.15f);
            glUniform2f(loadingScaleLoc, 0.4f, 0.08f);
            glUniform3f(loadingColorLoc, 0.3f, 0.4f, 0.5f);
            glDrawArrays(GL_TRIANGLES, 0, 6);

            glBindVertexArray(0);

            // Update window title with progress
            int progressPercent = static_cast<int>(progress * 100);
            std::string title = "Voxel Engine - Loading " + std::to_string(progressPercent) + "%";
            glfwSetWindowTitle(window, title.c_str());

            glfwSwapBuffers(window);
            continue;  // Skip the rest of the game loop during loading
        }

        // Reset window title when playing
        static bool titleReset = false;
        if (!titleReset) {
            glfwSetWindowTitle(window, "Voxel Engine");
            std::cout << "Entering PLAYING state - deferred rendering: " << (g_useDeferredRendering ? "ON" : "OFF") << std::endl;
            std::cout << std::flush;
            titleReset = true;
        }

        // ============================================
        // PERFORMANCE STATS - Read GPU timers from previous frame
        // ============================================
        g_perfStats.frameTimeMs = deltaTime * 1000.0;
        g_perfStats.fps = 1.0 / deltaTime;

        // Read GPU timer results from previous frame (to avoid stalls)
        int prevFrame = 1 - currentTimerFrame;
        if (gpuTimersReady) {
            GLuint64 timeNs;

            if (g_useDeferredRendering) {
                glGetQueryObjectui64v(gpuTimerQueries[prevFrame][TIMER_SHADOW], GL_QUERY_RESULT, &timeNs);
                g_perfStats.shadowPassMs = timeNs / 1000000.0;

                glGetQueryObjectui64v(gpuTimerQueries[prevFrame][TIMER_GBUFFER], GL_QUERY_RESULT, &timeNs);
                g_perfStats.gBufferPassMs = timeNs / 1000000.0;

                glGetQueryObjectui64v(gpuTimerQueries[prevFrame][TIMER_HIZ], GL_QUERY_RESULT, &timeNs);
                g_perfStats.hiZPassMs = timeNs / 1000000.0;

                glGetQueryObjectui64v(gpuTimerQueries[prevFrame][TIMER_SSAO], GL_QUERY_RESULT, &timeNs);
                g_perfStats.ssaoPassMs = timeNs / 1000000.0;

                glGetQueryObjectui64v(gpuTimerQueries[prevFrame][TIMER_COMPOSITE], GL_QUERY_RESULT, &timeNs);
                g_perfStats.compositePassMs = timeNs / 1000000.0;
            }

            // Read timers for water, precipitation, sky, and UI (always active)
            glGetQueryObjectui64v(gpuTimerQueries[prevFrame][TIMER_WATER], GL_QUERY_RESULT, &timeNs);
            g_perfStats.waterPassMs = timeNs / 1000000.0;

            glGetQueryObjectui64v(gpuTimerQueries[prevFrame][TIMER_PRECIP], GL_QUERY_RESULT, &timeNs);
            g_perfStats.precipPassMs = timeNs / 1000000.0;

            glGetQueryObjectui64v(gpuTimerQueries[prevFrame][TIMER_SKY], GL_QUERY_RESULT, &timeNs);
            g_perfStats.skyPassMs = timeNs / 1000000.0;

            glGetQueryObjectui64v(gpuTimerQueries[prevFrame][TIMER_UI], GL_QUERY_RESULT, &timeNs);
            g_perfStats.uiPassMs = timeNs / 1000000.0;

            g_perfStats.totalGpuMs = g_perfStats.shadowPassMs + g_perfStats.gBufferPassMs +
                                      g_perfStats.hiZPassMs + g_perfStats.ssaoPassMs +
                                      g_perfStats.compositePassMs + g_perfStats.waterPassMs +
                                      g_perfStats.precipPassMs + g_perfStats.skyPassMs +
                                      g_perfStats.uiPassMs;
        }

        // Increment frame counter and log timing
        g_frameNumber++;
        logRenderTiming();

        // Update chunk stats
        g_perfStats.chunksRendered = world.lastRenderedChunks;
        g_perfStats.chunksFrustumCulled = world.lastCulledChunks;
        g_perfStats.chunksHiZCulled = world.lastHiZCulledChunks;
        g_perfStats.subChunksRendered = world.lastRenderedSubChunks;
        g_perfStats.subChunksFrustumCulled = world.lastCulledSubChunks;
        g_perfStats.waterSubChunksRendered = world.lastRenderedWaterSubChunks;
        g_perfStats.waterSubChunksCulled = world.lastCulledWaterSubChunks;
        g_perfStats.chunksLoaded = world.getChunkCount();
        g_perfStats.meshesLoaded = world.getMeshCount();

        // Update time of day (only if daylight cycle is enabled)
        if (doDaylightCycle) {
            timeOfDay += deltaTime / dayLength;
            if (timeOfDay >= 1.0f) timeOfDay -= 1.0f;
        }

        // Calculate sun position (rotates around X axis)
        float sunAngle = timeOfDay * 2.0f * 3.14159f;
        glm::vec3 lightDir = glm::normalize(glm::vec3(0.2f, sin(sunAngle), cos(sunAngle)));

        // Calculate sky colors based on time of day
        // 0.0 = midnight, 0.25 = sunrise, 0.5 = noon, 0.75 = sunset
        glm::vec3 skyColor, lightColor, ambientColor;

        float dayFactor = sin(sunAngle);  // -1 to 1, positive during day
        dayFactor = glm::clamp((dayFactor + 0.2f) / 1.2f, 0.0f, 1.0f);  // Smooth transition

        // Night colors
        glm::vec3 nightSky(0.05f, 0.05f, 0.15f);
        glm::vec3 nightLight(0.2f, 0.2f, 0.4f);
        glm::vec3 nightAmbient(0.1f, 0.1f, 0.2f);

        // Day colors
        glm::vec3 daySky(0.5f, 0.7f, 0.95f);
        glm::vec3 dayLight(1.0f, 0.95f, 0.85f);
        glm::vec3 dayAmbient(0.6f, 0.65f, 0.8f);

        // Sunrise/sunset colors (when sun is near horizon)
        glm::vec3 sunsetSky(0.9f, 0.5f, 0.3f);
        glm::vec3 sunsetLight(1.0f, 0.6f, 0.3f);

        // Blend between night and day
        skyColor = glm::mix(nightSky, daySky, dayFactor);
        lightColor = glm::mix(nightLight, dayLight, dayFactor);
        ambientColor = glm::mix(nightAmbient, dayAmbient, dayFactor);

        // Add sunset/sunrise tint when sun is near horizon
        float horizonFactor = 1.0f - abs(dayFactor - 0.5f) * 2.0f;  // 1 at horizon, 0 at noon/midnight
        horizonFactor = pow(horizonFactor, 2.0f) * 0.8f;
        if (dayFactor > 0.1f && dayFactor < 0.9f) {
            skyColor = glm::mix(skyColor, sunsetSky, horizonFactor);
            lightColor = glm::mix(lightColor, sunsetLight, horizonFactor);
        }

        // ============================================================
        // Weather System Update
        // ============================================================
        // Smooth weather intensity transition
        float transitionSpeed = 0.5f * deltaTime;
        if (weatherIntensity < targetWeatherIntensity) {
            weatherIntensity = std::min(weatherIntensity + transitionSpeed, targetWeatherIntensity);
        } else if (weatherIntensity > targetWeatherIntensity) {
            weatherIntensity = std::max(weatherIntensity - transitionSpeed, targetWeatherIntensity);
        }

        // Modify colors based on weather
        if (currentWeather != WeatherType::CLEAR && weatherIntensity > 0.0f) {
            // Darken and desaturate during weather
            float weatherDarken = 1.0f - weatherIntensity * 0.4f;  // Up to 40% darker
            glm::vec3 stormTint(0.5f, 0.55f, 0.6f);  // Gray-blue storm color

            if (currentWeather == WeatherType::THUNDERSTORM) {
                weatherDarken *= 0.7f;  // Extra dark for storms
            }

            skyColor = glm::mix(skyColor, skyColor * stormTint, weatherIntensity) * weatherDarken;
            lightColor *= weatherDarken;
            ambientColor = glm::mix(ambientColor, ambientColor * stormTint, weatherIntensity * 0.5f);
        }

        // Lightning flash effect for thunderstorms
        if (currentWeather == WeatherType::THUNDERSTORM && weatherIntensity > 0.5f) {
            if (currentFrame >= nextLightningTime) {
                // Trigger lightning flash
                lightningFlash = 1.0f;
                // Random delay until next lightning (3-15 seconds)
                std::uniform_real_distribution<float> lightningDist(3.0f, 15.0f);
                nextLightningTime = currentFrame + lightningDist(rng);
            }

            // Apply lightning flash to scene
            if (lightningFlash > 0.0f) {
                float flashBoost = lightningFlash * 2.0f;
                skyColor += glm::vec3(flashBoost);
                lightColor += glm::vec3(flashBoost);
                ambientColor += glm::vec3(flashBoost * 0.5f);
                // Decay flash quickly
                lightningFlash -= deltaTime * 8.0f;
                if (lightningFlash < 0.0f) lightningFlash = 0.0f;
            }
        }

        // Update precipitation particles (world space)
        if (currentWeather != WeatherType::CLEAR && weatherIntensity > 0.01f) {
            // Initialize particles around camera on first use
            if (!particlesInitialized) {
                for (int i = 0; i < MAX_PARTICLES; i++) {
                    particles[i].x = camera.position.x + posDist(rng);
                    particles[i].y = camera.position.y + heightDist(rng);
                    particles[i].z = camera.position.z + posDist(rng);
                }
                particlesInitialized = true;
            }

            float fallSpeed = (currentWeather == WeatherType::SNOW) ? 3.0f : 20.0f;
            float spawnRadius = 80.0f;

            for (int i = 0; i < MAX_PARTICLES; i++) {
                // Fall down in world space
                particles[i].y -= particles[i].speed * fallSpeed * deltaTime / 20.0f;

                // Check if particle is too far from camera or below ground
                float dx = particles[i].x - camera.position.x;
                float dz = particles[i].z - camera.position.z;
                float distSq = dx * dx + dz * dz;

                // Respawn if too far horizontally, too low, or fell below camera view
                if (distSq > spawnRadius * spawnRadius ||
                    particles[i].y < camera.position.y - 30.0f ||
                    particles[i].y < 0.0f) {
                    // Respawn at top within range of camera
                    particles[i].x = camera.position.x + posDist(rng);
                    particles[i].y = camera.position.y + 30.0f + heightDist(rng) * 0.5f;
                    particles[i].z = camera.position.z + posDist(rng);
                }
            }
        }

        // FPS counter with chunk stats and time
        frameCount++;
        if (currentFrame - lastFPSTime >= 1.0) {
            std::string mode = player->isFlying ? "Flying" :
                               player->isInWater ? "Swimming" : "Survival";
            int hour = static_cast<int>(timeOfDay * 24.0f) % 24;
            int minute = static_cast<int>(timeOfDay * 24.0f * 60.0f) % 60;
            char timeStr[8];
            snprintf(timeStr, sizeof(timeStr), "%02d:%02d", hour, minute);
            std::string title = std::string(WINDOW_TITLE) +
                " - FPS: " + std::to_string(frameCount) +
                " | Chunks: " + std::to_string(world.getChunkCount()) +
                " | " + timeStr +
                " | " + mode;
            glfwSetWindowTitle(window, title.c_str());
            frameCount = 0;
            lastFPSTime = currentFrame;
        }

        // Process input and update player physics (timed)
        auto inputStart = std::chrono::high_resolution_clock::now();
        InputState input = processInput(window);
        player->update(deltaTime, world,
                       input.forward, input.backward, input.left, input.right,
                       input.jump, input.descend, input.sprint);

        // Raycast to find target block
        currentTarget = Raycast::cast(
            camera.position,
            camera.front,
            REACH_DISTANCE,
            [](int x, int y, int z) {
                BlockType block = world.getBlock(x, y, z);
                return isBlockSolid(block);
            }
        );
        auto inputEnd = std::chrono::high_resolution_clock::now();
        g_perfStats.inputProcessMs = std::chrono::duration<double, std::milli>(inputEnd - inputStart).count();

        // Update world - loads/unloads chunks and updates meshes (timed)
        auto worldUpdateStart = std::chrono::high_resolution_clock::now();
        world.update(camera.position, deltaTime);
        auto worldUpdateEnd = std::chrono::high_resolution_clock::now();
        g_perfStats.worldUpdateMs = std::chrono::duration<double, std::milli>(worldUpdateEnd - worldUpdateStart).count();

        // ============================================================
        // Shadow Pass - Render scene from light's perspective
        // ============================================================

        // Only render shadows when sun is above horizon
        bool doShadowPass = lightDir.y > 0.05f;
        glm::mat4 lightSpaceMatrix = glm::mat4(1.0f);

        if (doShadowPass) {
            // Calculate light space matrix for shadow mapping
            // Use orthographic projection centered on the player
            float shadowDistance = 60.0f;  // Shadow distance
            glm::mat4 lightProjection = glm::ortho(-shadowDistance, shadowDistance,
                                                    -shadowDistance, shadowDistance,
                                                    1.0f, 250.0f);

            // Light view matrix - look from above in light direction
            glm::vec3 lightPos = camera.position + lightDir * 120.0f;

            // Calculate a safe up vector (avoid parallel to light direction)
            glm::vec3 upVec = glm::vec3(0.0f, 1.0f, 0.0f);
            if (std::abs(lightDir.y) > 0.99f) {
                upVec = glm::vec3(0.0f, 0.0f, 1.0f);
            }

            glm::mat4 lightView = glm::lookAt(lightPos, camera.position, upVec);
            lightSpaceMatrix = lightProjection * lightView;

            // Render to shadow map
            glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
            glBindFramebuffer(GL_FRAMEBUFFER, shadowMapFBO);
            glClear(GL_DEPTH_BUFFER_BIT);

            // Use shadow shader
            glUseProgram(shadowShaderProgram);
            glUniformMatrix4fv(shadowLightSpaceMatrixLoc, 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));

            // Use front face culling to reduce peter panning
            glCullFace(GL_FRONT);

            // Render world geometry to shadow map
            world.render(camera.position, shadowChunkOffsetLoc);

            // Restore face culling
            glCullFace(GL_BACK);

            // Unbind shadow framebuffer
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        // ============================================================
        // Main Render Pass
        // ============================================================

        // Get window size for aspect ratio
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        float aspectRatio = static_cast<float>(width) / static_cast<float>(height);

        // Clear screen
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Set matrices
        glm::mat4 view = camera.getViewMatrix();
        glm::mat4 projection = camera.getProjectionMatrix(aspectRatio);

        // Update frustum for culling (use view-projection matrix)
        world.updateFrustum(projection * view);

        // Calculate inverse matrices for ray direction reconstruction (used by sky shader)
        glm::mat4 invView = glm::inverse(view);
        glm::mat4 invProjection = glm::inverse(projection);

        // Lambda to render sky (called in appropriate place for each path)
        auto renderSky = [&]() {
            glBeginQuery(GL_TIME_ELAPSED, gpuTimerQueries[currentTimerFrame][TIMER_SKY]);

            glDepthMask(GL_FALSE);  // Don't write to depth buffer
            glDepthFunc(GL_LEQUAL); // Pass depth test at far plane

            glUseProgram(skyShaderProgram);
            glUniformMatrix4fv(skyInvViewLoc, 1, GL_FALSE, glm::value_ptr(invView));
            glUniformMatrix4fv(skyInvProjectionLoc, 1, GL_FALSE, glm::value_ptr(invProjection));
            glUniform3fv(skyCameraPosLoc, 1, glm::value_ptr(camera.position));
            glUniform3fv(skySunDirLoc, 1, glm::value_ptr(lightDir));
            glUniform3fv(skySkyTopLoc, 1, glm::value_ptr(skyColor));
            glUniform3fv(skySkyBottomLoc, 1, glm::value_ptr(glm::mix(skyColor, glm::vec3(0.9f, 0.85f, 0.8f), 0.3f)));
            glUniform1f(skyTimeLoc, static_cast<float>(glfwGetTime()));
            glUniform1i(skyCloudStyleLoc, cloudStyle);
            glUniform1f(skyCloudRenderDistLoc, static_cast<float>(world.renderDistance));

            glBindVertexArray(skyVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);

            glDepthMask(GL_TRUE);   // Re-enable depth writing
            glDepthFunc(GL_LESS);   // Restore normal depth function

            glEndQuery(GL_TIME_ELAPSED);
        };

        // ============================================================
        // DEFERRED RENDERING PATH
        // ============================================================
        if (g_useDeferredRendering) {
            // Sync culling flags with World
            world.useHiZCulling = g_enableHiZCulling;
            world.useSubChunkCulling = g_enableSubChunkCulling;

            // Calculate cascade shadow map matrices
            float nearPlane = 0.1f;
            float farPlane = 500.0f;
            calculateCascadeSplits(nearPlane, farPlane, NUM_CASCADES, 0.5f, cascadeSplitDepths);

            // OPTIMIZATION: Determine which cascades need updating this frame
            g_shadowFrameCounter++;
            for (int c = 0; c < NUM_CASCADES; c++) {
                g_cascadeNeedsUpdate[c] = (g_shadowFrameCounter % g_cascadeUpdateIntervals[c]) == 0;
            }

            // Render cascade shadow maps (only cascades that need updating)
            glBeginQuery(GL_TIME_ELAPSED, gpuTimerQueries[currentTimerFrame][TIMER_SHADOW]);

            glViewport(0, 0, CASCADE_RESOLUTION, CASCADE_RESOLUTION);
            glBindFramebuffer(GL_FRAMEBUFFER, cascadeShadowFBO);
            glCullFace(GL_FRONT);

            for (int cascade = 0; cascade < NUM_CASCADES; cascade++) {
                // Skip cascades that don't need updating this frame
                if (!g_cascadeNeedsUpdate[cascade]) continue;

                // Get frustum corners for this cascade
                float cascadeNear = (cascade == 0) ? nearPlane : cascadeSplitDepths[cascade - 1];
                float cascadeFar = cascadeSplitDepths[cascade];

                auto corners = getFrustumCornersWorldSpace(view, camera.fov, aspectRatio,
                                                            cascadeNear, cascadeFar);
                cascadeLightSpaceMatrices[cascade] = calculateCascadeLightSpaceMatrix(lightDir, corners);

                // Render to this cascade layer
                glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                          cascadeShadowMaps, 0, cascade);
                glClear(GL_DEPTH_BUFFER_BIT);

                glUseProgram(shadowShaderProgram);
                glUniformMatrix4fv(shadowLightSpaceMatrixLoc, 1, GL_FALSE,
                                   glm::value_ptr(cascadeLightSpaceMatrices[cascade]));

                // OPTIMIZATION: Use shadow-specific render with limited distance per cascade
                world.renderForShadow(camera.position, shadowChunkOffsetLoc, g_cascadeShadowDistances[cascade]);
            }

            glCullFace(GL_BACK);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            glEndQuery(GL_TIME_ELAPSED);  // End shadow timer

            // ============================================
            // Z-PREPASS (eliminates overdraw in G-buffer pass)
            // ============================================
            // Use render resolution if FSR is enabled
            int renderW = g_enableFSR ? RENDER_WIDTH : width;
            int renderH = g_enableFSR ? RENDER_HEIGHT : height;
            glViewport(0, 0, renderW, renderH);
            glBindFramebuffer(GL_FRAMEBUFFER, gBufferFBO);

            // Disable color writes - depth only
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);

            // Clear depth buffer for Z-prepass
            glClear(GL_DEPTH_BUFFER_BIT);

            glUseProgram(zPrepassProgram);
            glUniformMatrix4fv(zPrepassViewLoc, 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(zPrepassProjectionLoc, 1, GL_FALSE, glm::value_ptr(projection));

            textureAtlas.bind(0);
            glUniform1i(zPrepassTexAtlasLoc, 0);

            // Render world depth-only
            world.render(camera.position, zPrepassChunkOffsetLoc);

            // ============================================
            // G-Buffer pass (no overdraw due to Z-prepass)
            // ============================================
            glBeginQuery(GL_TIME_ELAPSED, gpuTimerQueries[currentTimerFrame][TIMER_GBUFFER]);

            // Re-enable color writes
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

            // Ensure correct draw buffers are set
            GLenum gBufferDrawBuffers[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
            glDrawBuffers(3, gBufferDrawBuffers);

            // One-time debug: verify G-buffer FBO is complete
            static bool gBufferFBOChecked = false;
            if (!gBufferFBOChecked) {
                GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                if (fboStatus != GL_FRAMEBUFFER_COMPLETE) {
                    std::cerr << "G-buffer FBO incomplete during render! Status: " << fboStatus << std::endl;
                } else {
                    std::cout << "G-buffer FBO verified complete during render." << std::endl;
                }
                std::cout << "gBufferFBO ID: " << gBufferFBO << std::endl;
                std::cout << "gBufferProgram ID: " << gBufferProgram << std::endl;
                std::cout << std::flush;  // Force output
                gBufferFBOChecked = true;
            }

            // Use GL_LEQUAL - draw pixels at same or closer depth as Z-prepass
            // Note: GL_EQUAL can fail due to floating point precision differences between shaders
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);  // Don't write depth again

            // Only clear color, NOT depth (keep Z-prepass depth)
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            // Bind texture atlas (needed for both rendering paths)
            textureAtlas.bind(0);

            // Choose rendering path: Mesh Shaders > Batched > Traditional
            if (g_enableMeshShaders && g_meshShadersAvailable && meshShaderProgram != 0) {
                // ============================================
                // MESH SHADER RENDERING PATH (NVIDIA Turing+)
                // ============================================
                glm::mat4 viewProj = projection * view;
                world.renderSubChunksMeshShader(camera.position, viewProj);
            } else {
                // Set up traditional shader for both batched and non-batched paths
                glUseProgram(gBufferProgram);
                glUniformMatrix4fv(gBufferViewLoc, 1, GL_FALSE, glm::value_ptr(view));
                glUniformMatrix4fv(gBufferProjectionLoc, 1, GL_FALSE, glm::value_ptr(projection));
                glUniform1i(gBufferTexAtlasLoc, 0);

                if (g_enableBatchedRendering) {
                    // ============================================
                    // SODIUM-STYLE BATCHED RENDERING PATH
                    // ============================================
                    // Groups sub-chunks by column, reducing uniform updates
                    world.renderSubChunksBatched(camera.position, gBufferChunkOffsetLoc);
                } else {
                    // ============================================
                    // TRADITIONAL VAO/VBO RENDERING PATH
                    // ============================================
                    // Render world to G-buffer (no overdraw!)
                    world.render(camera.position, gBufferChunkOffsetLoc);
                }
            }

            // One-time debug: report how many chunks were rendered
            static bool renderCountReported = false;
            if (!renderCountReported) {
                std::cout << "G-buffer pass rendered " << world.lastRenderedChunks
                          << " chunks (culled: " << world.lastCulledChunks << ")" << std::endl;
                if (g_enableMeshShaders) {
                    std::cout << "  Using MESH SHADER rendering path" << std::endl;
                } else if (g_enableBatchedRendering) {
                    std::cout << "  Using SODIUM-STYLE BATCHED rendering path" << std::endl;
                } else {
                    std::cout << "  Using TRADITIONAL VAO/VBO rendering path" << std::endl;
                }
                std::cout << std::flush;
                renderCountReported = true;
            }

            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            // Restore depth state for subsequent passes
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);

            glEndQuery(GL_TIME_ELAPSED);  // End G-buffer timer

            // ============================================
            // HI-Z GENERATION (for next frame's occlusion culling)
            // ============================================
            glBeginQuery(GL_TIME_ELAPSED, gpuTimerQueries[currentTimerFrame][TIMER_HIZ]);

            // OPTIMIZATION: Only update Hi-Z every N frames (saves 1-2ms on skip frames)
            bool doHiZUpdate = (g_hiZFrameCounter % g_hiZUpdateInterval == 0);
            g_hiZFrameCounter++;

            if (g_enableHiZCulling) {
                // Always apply cached visibility results (cheap operation)
                // This happens below in the cached results section

                if (doHiZUpdate) {
                    // Copy G-buffer depth to Hi-Z level 0
                    glCopyImageSubData(
                        gDepth, GL_TEXTURE_2D, 0, 0, 0, 0,
                        hiZTexture, GL_TEXTURE_2D, 0, 0, 0, 0,
                        width, height, 1
                    );

                    // Generate Hi-Z mipmap pyramid using compute shader
                    glUseProgram(hiZDownsampleProgram);

                    int currentWidth = width;
                    int currentHeight = height;

                    for (int level = 1; level < hiZLevels; level++) {
                        int srcLevel = level - 1;
                        currentWidth = std::max(1, currentWidth / 2);
                        currentHeight = std::max(1, currentHeight / 2);

                        // Bind source (previous mip level)
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, hiZTexture);
                        glUniform1i(hiZSrcDepthLoc, 0);
                        glUniform1i(hiZSrcLevelLoc, srcLevel);

                        // Bind destination (current mip level as image)
                        glBindImageTexture(0, hiZTexture, level, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);

                        // Dispatch compute shader
                        int groupsX = (currentWidth + 7) / 8;
                        int groupsY = (currentHeight + 7) / 8;
                        glDispatchCompute(groupsX, groupsY, 1);

                        // Memory barrier before next iteration
                        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
                    }
                } // end doHiZUpdate for mipmap generation

                // ============================================
                // OCCLUSION CULLING (results used next frame)
                // ============================================
                // Collect sub-chunk bounds from world (16x16x16 sections)
                struct SubChunkBoundsData {
                    glm::vec4 minBound;
                    glm::vec4 maxBound;
                };
                std::vector<SubChunkBoundsData> subChunkBounds;
                std::vector<glm::ivec3> subChunkPositions;  // Track which sub-chunk each index corresponds to

                glm::ivec2 playerChunk = Chunk::worldToChunkPos(camera.position);

                for (auto& [pos, mesh] : world.meshes) {
                    int dx = pos.x - playerChunk.x;
                    int dz = pos.y - playerChunk.y;

                    if (abs(dx) <= world.renderDistance && abs(dz) <= world.renderDistance) {
                        // Iterate through sub-chunks in this column
                        for (int subY = 0; subY < SUB_CHUNKS_PER_COLUMN; subY++) {
                            const auto& subChunk = mesh->subChunks[subY];

                            // Only add non-empty sub-chunks
                            if (subChunk.isEmpty) continue;

                            SubChunkBoundsData bounds;
                            bounds.minBound = glm::vec4(
                                pos.x * CHUNK_SIZE_X,
                                static_cast<float>(subY * SUB_CHUNK_HEIGHT),
                                pos.y * CHUNK_SIZE_Z,
                                0.0f
                            );
                            bounds.maxBound = glm::vec4(
                                (pos.x + 1) * CHUNK_SIZE_X,
                                static_cast<float>((subY + 1) * SUB_CHUNK_HEIGHT),
                                (pos.y + 1) * CHUNK_SIZE_Z,
                                0.0f
                            );
                            subChunkBounds.push_back(bounds);
                            subChunkPositions.push_back(glm::ivec3(pos.x, subY, pos.y));
                        }
                    }
                }

                int subChunkCount = static_cast<int>(subChunkBounds.size());

                // OPTIMIZATION: Use cached results from previous frame (avoids GPU stall)
                // First, apply cached visibility results from last frame
                if (!cachedVisibilityResults.empty() && !cachedSubChunkPositions.empty()) {
                    world.hiZSubChunkVisibility.clear();
                    lastOccludedChunks = 0;
                    int cachedCount = static_cast<int>(std::min(cachedVisibilityResults.size(), cachedSubChunkPositions.size()));
                    for (int i = 0; i < cachedCount; i++) {
                        world.hiZSubChunkVisibility[cachedSubChunkPositions[i]] = (cachedVisibilityResults[i] != 0);
                        if (cachedVisibilityResults[i] == 0) {
                            lastOccludedChunks++;
                        }
                    }
                }

                if (subChunkCount > 0) {
                    // Check if previous frame's fence is signaled (non-blocking)
                    // This is cheap and should happen every frame to keep pipeline moving
                    int readIndex = 1 - visibilityWriteIndex;
                    if (visibilityFence[readIndex] != nullptr) {
                        GLenum waitResult = glClientWaitSync(visibilityFence[readIndex], 0, 0);
                        if (waitResult == GL_ALREADY_SIGNALED || waitResult == GL_CONDITION_SATISFIED) {
                            // Previous results are ready - read them asynchronously
                            glDeleteSync(visibilityFence[readIndex]);
                            visibilityFence[readIndex] = nullptr;

                            // Read back results (fast because GPU is done)
                            glBindBuffer(GL_SHADER_STORAGE_BUFFER, visibilitySSBO[readIndex]);
                            cachedVisibilityResults.resize(subChunkCount);
                            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                                               subChunkCount * sizeof(GLuint), cachedVisibilityResults.data());
                            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

                            // Cache positions for applying next frame
                            cachedSubChunkPositions = subChunkPositions;
                        }
                    }

                    // Only run expensive occlusion culling on update frames
                    if (doHiZUpdate) {
                        // Upload sub-chunk bounds to SSBO
                        glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunkBoundsSSBO);
                        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                                        subChunkCount * sizeof(SubChunkBoundsData), subChunkBounds.data());

                        // Initialize visibility to 1 (visible) in current write buffer
                        std::vector<GLuint> initialVisibility(subChunkCount, 1u);
                        glBindBuffer(GL_SHADER_STORAGE_BUFFER, visibilitySSBO[visibilityWriteIndex]);
                        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                                        subChunkCount * sizeof(GLuint), initialVisibility.data());
                        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

                        // Run occlusion culling compute shader
                        glUseProgram(occlusionCullProgram);

                        // Bind SSBOs (write to current buffer)
                        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, chunkBoundsSSBO);
                        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, visibilitySSBO[visibilityWriteIndex]);

                        // Bind Hi-Z texture
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, hiZTexture);
                        glUniform1i(occlusionHiZLoc, 0);

                        // Set uniforms
                        glm::mat4 viewProj = projection * view;
                        glUniformMatrix4fv(occlusionViewProjLoc, 1, GL_FALSE, glm::value_ptr(viewProj));
                        glUniform1i(occlusionNumMipsLoc, hiZLevels);
                        glUniform2f(occlusionScreenSizeLoc, static_cast<float>(width), static_cast<float>(height));
                        glUniform1i(occlusionChunkCountLoc, subChunkCount);

                        // Dispatch
                        int groups = (subChunkCount + 63) / 64;
                        glDispatchCompute(groups, 1, 1);

                        // Memory barrier
                        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

                        // Create fence for this frame's write (async - no stall!)
                        if (visibilityFence[visibilityWriteIndex] != nullptr) {
                            glDeleteSync(visibilityFence[visibilityWriteIndex]);
                        }
                        visibilityFence[visibilityWriteIndex] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

                        // Swap write index for next frame
                        visibilityWriteIndex = 1 - visibilityWriteIndex;
                    } // end doHiZUpdate for occlusion culling
                } // end subChunkCount > 0
            } // end g_enableHiZCulling

            glEndQuery(GL_TIME_ELAPSED);  // End Hi-Z timer

            // SSAO pass
            glBeginQuery(GL_TIME_ELAPSED, gpuTimerQueries[currentTimerFrame][TIMER_SSAO]);

            if (g_enableSSAO) {
                glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);
                glClear(GL_COLOR_BUFFER_BIT);

                glUseProgram(ssaoProgram);

                // Bind G-buffer textures
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, gPosition);
                glUniform1i(ssaoGPositionLoc, 0);

                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, gNormal);
                glUniform1i(ssaoGNormalLoc, 1);

                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, gDepth);
                glUniform1i(ssaoGDepthLoc, 2);

                glActiveTexture(GL_TEXTURE3);
                glBindTexture(GL_TEXTURE_2D, ssaoNoiseTexture);
                glUniform1i(ssaoNoiseLoc, 3);

                // Set SSAO uniforms (use render resolution if FSR enabled)
                glUniformMatrix4fv(ssaoProjectionLoc, 1, GL_FALSE, glm::value_ptr(projection));
                glUniformMatrix4fv(ssaoViewLoc, 1, GL_FALSE, glm::value_ptr(view));
                glUniform2f(ssaoNoiseScaleLoc, renderW / 4.0f, renderH / 4.0f);
                glUniform1f(ssaoRadiusLoc, ssaoRadius);
                glUniform1f(ssaoBiasLoc, ssaoBias);

                // OPTIMIZATION: Kernel samples are in UBO (binding 0), uploaded once at init
                // No per-frame upload needed!

                // Render fullscreen quad
                glBindVertexArray(quadVAO);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glBindVertexArray(0);

                glBindFramebuffer(GL_FRAMEBUFFER, 0);

                // SSAO blur pass
                glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFBO);
                glClear(GL_COLOR_BUFFER_BIT);

                glUseProgram(ssaoBlurProgram);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, ssaoColorBuffer);
                glUniform1i(ssaoBlurInputLoc, 0);

                glBindVertexArray(quadVAO);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glBindVertexArray(0);

                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }

            glEndQuery(GL_TIME_ELAPSED);  // End SSAO timer

            // Composite pass - render to scene FBO (for FSR) or directly to screen
            glBeginQuery(GL_TIME_ELAPSED, gpuTimerQueries[currentTimerFrame][TIMER_COMPOSITE]);

            // Determine render target and viewport
            int compositeWidth = width;
            int compositeHeight = height;
            if (g_enableFSR && sceneFBO != 0) {
                glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
                compositeWidth = RENDER_WIDTH;
                compositeHeight = RENDER_HEIGHT;
            } else {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }

            glViewport(0, 0, compositeWidth, compositeHeight);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDisable(GL_DEPTH_TEST);  // Disable depth test for fullscreen quad

            glUseProgram(compositeProgram);

            // Bind G-buffer textures
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, gPosition);
            glUniform1i(compositeGPositionLoc, 0);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, gNormal);
            glUniform1i(compositeGNormalLoc, 1);

            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, gAlbedo);
            glUniform1i(compositeGAlbedoLoc, 2);

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, gDepth);
            glUniform1i(compositeGDepthLoc, 3);

            // Bind SSAO texture
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, g_enableSSAO ? ssaoBlurBuffer : 0);
            glUniform1i(compositeSsaoTexLoc, 4);
            glUniform1i(compositeEnableSsaoLoc, g_enableSSAO ? 1 : 0);

            // Bind cascade shadow maps
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D_ARRAY, cascadeShadowMaps);
            glUniform1i(compositeCascadeMapsLoc, 5);

            // Set cascade matrices and splits
            glUniformMatrix4fv(compositeCascadeMatricesLoc, NUM_CASCADES, GL_FALSE,
                               glm::value_ptr(cascadeLightSpaceMatrices[0]));
            glUniform1fv(compositeCascadeSplitsLoc, NUM_CASCADES, cascadeSplitDepths);

            // Shadow strength
            float sunUp = glm::max(0.0f, lightDir.y);
            float shadowStr = sunUp > 0.1f ? 0.6f : 0.0f;
            glUniform1f(compositeShadowStrengthLoc, shadowStr);

            // Lighting uniforms
            glUniform3fv(compositeLightDirLoc, 1, glm::value_ptr(lightDir));
            glUniform3fv(compositeLightColorLoc, 1, glm::value_ptr(lightColor));
            glUniform3fv(compositeAmbientColorLoc, 1, glm::value_ptr(ambientColor));
            glUniform3fv(compositeSkyColorLoc, 1, glm::value_ptr(skyColor));
            glUniform3fv(compositeCameraPosLoc, 1, glm::value_ptr(camera.position));
            glUniform1f(compositeTimeLoc, static_cast<float>(glfwGetTime()));
            glUniform1f(compositeFogDensityLoc, fogDensity);
            glUniform1f(compositeUnderwaterLoc, player->isUnderwater ? 1.0f : 0.0f);
            glUniform1i(compositeDebugModeLoc, g_deferredDebugMode);
            glUniform1f(compositeRenderDistLoc, static_cast<float>(world.renderDistance * 16));  // Convert chunks to blocks

            // Set inverse view-projection matrix for position reconstruction from depth
            glm::mat4 viewProj = projection * view;
            glm::mat4 invViewProj = glm::inverse(viewProj);
            glUniformMatrix4fv(compositeInvViewProjLoc, 1, GL_FALSE, glm::value_ptr(invViewProj));

            // Render fullscreen quad
            glBindVertexArray(quadVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);

            // Copy depth from G-buffer to current framebuffer BEFORE sky rendering
            GLuint targetFBO = (g_enableFSR && sceneFBO != 0) ? sceneFBO : 0;
            glBindFramebuffer(GL_READ_FRAMEBUFFER, gBufferFBO);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, targetFBO);
            glBlitFramebuffer(0, 0, compositeWidth, compositeHeight, 0, 0, compositeWidth, compositeHeight,
                              GL_DEPTH_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);

            // Re-enable depth test for sky and forward passes
            glEnable(GL_DEPTH_TEST);

            // Render sky with clouds after composite (only in normal mode, skip in debug modes)
            if (g_deferredDebugMode == 0) {
                renderSky();
            }

            // ============================================
            // FSR UPSCALING PASS (if enabled)
            // ============================================
            if (g_enableFSR && sceneFBO != 0) {
                // Switch to screen framebuffer at display resolution
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0, 0, width, height);
                glClear(GL_COLOR_BUFFER_BIT);
                glDisable(GL_DEPTH_TEST);

                // FSR EASU (Edge Adaptive Spatial Upscaling)
                glUseProgram(fsrEASUProgram);

                // Bind scene texture
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, sceneColorTexture);
                glUniform1i(fsrEASUInputLoc, 0);

                // Set resolution uniforms
                glUniform2f(fsrEASUInputSizeLoc, static_cast<float>(RENDER_WIDTH), static_cast<float>(RENDER_HEIGHT));
                glUniform2f(fsrEASUOutputSizeLoc, static_cast<float>(width), static_cast<float>(height));

                // FSR constants (con0-con3)
                float scaleX = static_cast<float>(RENDER_WIDTH) / static_cast<float>(width);
                float scaleY = static_cast<float>(RENDER_HEIGHT) / static_cast<float>(height);
                glUniform4f(fsrEASUCon0Loc, scaleX, scaleY, 0.5f * scaleX - 0.5f, 0.5f * scaleY - 0.5f);
                glUniform4f(fsrEASUCon1Loc, 1.0f / RENDER_WIDTH, 1.0f / RENDER_HEIGHT, 1.0f / RENDER_WIDTH, -1.0f / RENDER_HEIGHT);
                glUniform4f(fsrEASUCon2Loc, -1.0f / RENDER_WIDTH, 2.0f / RENDER_HEIGHT, 1.0f / RENDER_WIDTH, 2.0f / RENDER_HEIGHT);
                glUniform4f(fsrEASUCon3Loc, 0.0f, 4.0f / RENDER_HEIGHT, 0.0f, 0.0f);

                // Render fullscreen quad
                glBindVertexArray(quadVAO);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glBindVertexArray(0);

                // Note: RCAS sharpening pass could be added here if needed
                // For now, EASU alone provides good quality upscaling
            }

            glActiveTexture(GL_TEXTURE0);

            glEndQuery(GL_TIME_ELAPSED);  // End composite timer
        } else {
            // ============================================================
            // FORWARD RENDERING PATH (original code)
            // ============================================================

            // Note: Hi-Z culling is only available in deferred path
            // Disable it for forward rendering
            world.useHiZCulling = false;

            // Render sky first (at far plane)
            renderSky();

            // Render world
            glUseProgram(shaderProgram);
            glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));
            glUniformMatrix4fv(lightSpaceMatrixLoc, 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));
            glUniform3fv(lightDirLoc, 1, glm::value_ptr(lightDir));
            glUniform3fv(lightColorLoc, 1, glm::value_ptr(lightColor));
            glUniform3fv(ambientColorLoc, 1, glm::value_ptr(ambientColor));
            glUniform3fv(skyColorLoc, 1, glm::value_ptr(skyColor));
            glUniform1f(fogDensityLoc, fogDensity);
            glUniform1f(renderDistLoc, static_cast<float>(world.renderDistance * 16));  // Chunks to blocks
            glUniform1f(underwaterLoc, player->isUnderwater ? 1.0f : 0.0f);
            glUniform1f(timeLoc, static_cast<float>(glfwGetTime()));
            glUniform3fv(cameraPosLoc, 1, glm::value_ptr(camera.position));

            // Shadow strength - reduce at night (sun below horizon)
            float sunUp = glm::max(0.0f, lightDir.y);
            float shadowStr = sunUp > 0.1f ? 0.6f : 0.0f;  // Only cast shadows when sun is up
            glUniform1f(shadowStrengthLoc, shadowStr);

            // Bind texture atlas to slot 0
            textureAtlas.bind(0);
            glUniform1i(texAtlasLoc, 0);

            // Bind shadow map to slot 1
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, shadowMapTexture);
            glUniform1i(shadowMapLoc, 1);
            glActiveTexture(GL_TEXTURE0);

            // Render solid geometry first (pass chunkOffsetLoc for packed vertices)
            world.render(camera.position, chunkOffsetLoc);
        } // End of deferred/forward rendering path selection

        // ============================================================
        // FORWARD PASSES (water, precipitation, etc.) - shared by both paths
        // ============================================================

        // Time water rendering
        glBeginQuery(GL_TIME_ELAPSED, gpuTimerQueries[currentTimerFrame][TIMER_WATER]);

        // Switch to water shader for animated water rendering
        glUseProgram(waterShaderProgram);
        glUniformMatrix4fv(waterViewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(waterProjectionLoc, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform1f(waterTimeLoc, static_cast<float>(glfwGetTime()));
        glUniform3fv(waterLightDirLoc, 1, glm::value_ptr(lightDir));
        glUniform3fv(waterLightColorLoc, 1, glm::value_ptr(lightColor));
        glUniform3fv(waterAmbientColorLoc, 1, glm::value_ptr(ambientColor));
        glUniform3fv(waterSkyColorLoc, 1, glm::value_ptr(skyColor));
        glUniform1f(waterFogDensityLoc, fogDensity);
        glUniform1f(waterUnderwaterLoc, player->isUnderwater ? 1.0f : 0.0f);
        glUniform1i(waterTexAtlasLoc, 0);
        glUniform4fv(waterTexBoundsLoc, 1, glm::value_ptr(waterTexBounds));
        glUniform3fv(waterCameraPosLoc, 1, glm::value_ptr(camera.position));

        // Water LOD distance - water beyond this starts using simpler shader
        // Based on render distance: LOD kicks in at 40% of render distance in blocks
        float waterLodDist = static_cast<float>(world.renderDistance * CHUNK_SIZE_X) * 0.4f;
        glUniform1f(waterLodDistanceLoc, waterLodDist);

        // Render water with depth writing disabled (prevents water from occluding objects behind it)
        world.renderWater(camera.position);

        glEndQuery(GL_TIME_ELAPSED);

        // ============================================================
        // Render Precipitation (rain/snow)
        // ============================================================
        glBeginQuery(GL_TIME_ELAPSED, gpuTimerQueries[currentTimerFrame][TIMER_PRECIP]);

        if (currentWeather != WeatherType::CLEAR && weatherIntensity > 0.01f) {
            glUseProgram(precipShaderProgram);
            glUniformMatrix4fv(precipViewLoc, 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(precipProjectionLoc, 1, GL_FALSE, glm::value_ptr(projection));
            glUniform1f(precipTimeLoc, static_cast<float>(glfwGetTime()));
            glUniform1i(precipWeatherTypeLoc, static_cast<int>(currentWeather));
            glUniform1f(precipIntensityLoc, weatherIntensity);
            glUniform3fv(precipLightColorLoc, 1, glm::value_ptr(lightColor));

            // Build particle buffer data (particles are already in world space)
            std::vector<float> particleData(MAX_PARTICLES * 5);
            for (int i = 0; i < MAX_PARTICLES; i++) {
                particleData[i * 5 + 0] = particles[i].x;
                particleData[i * 5 + 1] = particles[i].y;
                particleData[i * 5 + 2] = particles[i].z;
                particleData[i * 5 + 3] = particles[i].size * (currentWeather == WeatherType::SNOW ? 2.0f : 1.0f);
                particleData[i * 5 + 4] = particles[i].alpha;
            }

            // Update VBO
            glBindBuffer(GL_ARRAY_BUFFER, precipVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, particleData.size() * sizeof(float), particleData.data());
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            // Enable blending and point sprites
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_PROGRAM_POINT_SIZE);
            glDepthMask(GL_FALSE);  // Don't write to depth buffer

            // Draw particles
            glBindVertexArray(precipVAO);
            glDrawArrays(GL_POINTS, 0, MAX_PARTICLES);
            glBindVertexArray(0);

            // Restore state
            glDepthMask(GL_TRUE);
            glDisable(GL_PROGRAM_POINT_SIZE);
        }

        glEndQuery(GL_TIME_ELAPSED);

        // ============================================================
        // UI Rendering (block highlight and crosshair)
        // ============================================================
        glBeginQuery(GL_TIME_ELAPSED, gpuTimerQueries[currentTimerFrame][TIMER_UI]);

        // Switch back to main shader for other rendering
        glUseProgram(shaderProgram);

        // Render block highlight
        if (currentTarget.has_value() && !wireframeMode) {
            blockHighlight.render(currentTarget->blockPos, view, projection);
        }

        // Render crosshair
        crosshair.render();

        glEndQuery(GL_TIME_ELAPSED);

        // Switch to next timer frame and mark timers as ready (moved here for both paths)
        currentTimerFrame = 1 - currentTimerFrame;
        gpuTimersReady = true;

        // ============================================
        // PERFORMANCE STATS DISPLAY
        // ============================================
        if (g_showPerfStats) {
            // Update window title with FPS
            static double lastTitleUpdate = 0.0;
            static double lastDetailedPrint = 0.0;
            double currentTime = glfwGetTime();

            // Update title every 250ms
            if (currentTime - lastTitleUpdate >= 0.25) {
                char title[256];
                if (g_enableSubChunkCulling) {
                    snprintf(title, sizeof(title), "Voxel Engine | FPS: %.0f | GPU: %.1fms | Solid: %d/%d | Water: %d/%d",
                             g_perfStats.fps, g_perfStats.totalGpuMs,
                             g_perfStats.subChunksRendered, g_perfStats.subChunksRendered + g_perfStats.subChunksFrustumCulled,
                             g_perfStats.waterSubChunksRendered, g_perfStats.waterSubChunksRendered + g_perfStats.waterSubChunksCulled);
                } else {
                    snprintf(title, sizeof(title), "Voxel Engine | FPS: %.0f | GPU: %.1fms | Chunks: %d/%zu",
                             g_perfStats.fps, g_perfStats.totalGpuMs,
                             g_perfStats.chunksRendered, g_perfStats.meshesLoaded);
                }
                glfwSetWindowTitle(window, title);
                lastTitleUpdate = currentTime;
            }

            // Print detailed stats to console every 2 seconds
            if (currentTime - lastDetailedPrint >= 2.0) {
                std::cout << "\n=== Performance Stats ===" << std::endl;
                std::cout << "Frame: " << std::fixed << std::setprecision(2) << g_perfStats.frameTimeMs << "ms (" << (int)g_perfStats.fps << " FPS)" << std::endl;

                std::cout << "GPU Timing:" << std::endl;
                if (g_useDeferredRendering) {
                    std::cout << "  Shadow:    " << std::setw(6) << g_perfStats.shadowPassMs << "ms" << std::endl;
                    std::cout << "  G-Buffer:  " << std::setw(6) << g_perfStats.gBufferPassMs << "ms" << std::endl;
                    std::cout << "  Hi-Z:      " << std::setw(6) << g_perfStats.hiZPassMs << "ms" << std::endl;
                    std::cout << "  SSAO:      " << std::setw(6) << g_perfStats.ssaoPassMs << "ms" << std::endl;
                    std::cout << "  Composite: " << std::setw(6) << g_perfStats.compositePassMs << "ms" << std::endl;
                }
                std::cout << "  Water:     " << std::setw(6) << g_perfStats.waterPassMs << "ms" << std::endl;
                std::cout << "  Precip:    " << std::setw(6) << g_perfStats.precipPassMs << "ms" << std::endl;
                std::cout << "  Sky:       " << std::setw(6) << g_perfStats.skyPassMs << "ms" << std::endl;
                std::cout << "  UI:        " << std::setw(6) << g_perfStats.uiPassMs << "ms" << std::endl;
                std::cout << "  Total GPU: " << std::setw(6) << g_perfStats.totalGpuMs << "ms" << std::endl;

                std::cout << "CPU Timing:" << std::endl;
                std::cout << "  Input:     " << std::setw(6) << g_perfStats.inputProcessMs << "ms" << std::endl;
                std::cout << "  World:     " << std::setw(6) << g_perfStats.worldUpdateMs << "ms" << std::endl;

                std::cout << "Chunks:" << std::endl;
                if (g_enableSubChunkCulling) {
                    std::cout << "  Solid sub-chunks rendered: " << g_perfStats.subChunksRendered << std::endl;
                    std::cout << "  Solid sub-chunks culled: " << g_perfStats.subChunksFrustumCulled << std::endl;
                    std::cout << "  Water sub-chunks rendered: " << g_perfStats.waterSubChunksRendered << std::endl;
                    std::cout << "  Water sub-chunks culled: " << g_perfStats.waterSubChunksCulled << std::endl;
                } else {
                    std::cout << "  Rendered: " << g_perfStats.chunksRendered << std::endl;
                    std::cout << "  Frustum culled: " << g_perfStats.chunksFrustumCulled << std::endl;
                }
                std::cout << "  Hi-Z culled: " << g_perfStats.chunksHiZCulled << " (GPU marked occluded: " << lastOccludedChunks << ")" << std::endl;
                std::cout << "  Loaded: " << g_perfStats.chunksLoaded << " chunks, " << g_perfStats.meshesLoaded << " meshes" << std::endl;
                lastDetailedPrint = currentTime;
            }
        }

        // Swap buffers (poll events is at the start of the loop)
        glfwSwapBuffers(window);
    }

    // Cleanup
    closeRenderTimingLog();
    std::cout << "Render timing log closed" << std::endl;

    crosshair.destroy();
    blockHighlight.destroy();
    textureAtlas.destroy();
    glDeleteProgram(shaderProgram);
    glDeleteProgram(waterShaderProgram);
    glDeleteProgram(skyShaderProgram);
    glDeleteProgram(precipShaderProgram);
    glDeleteProgram(shadowShaderProgram);
    glDeleteProgram(zPrepassProgram);
    glDeleteProgram(loadingShaderProgram);
    glDeleteVertexArrays(1, &loadingVAO);
    glDeleteBuffers(1, &loadingVBO);
    glDeleteVertexArrays(1, &skyVAO);
    glDeleteBuffers(1, &skyVBO);
    glDeleteVertexArrays(1, &precipVAO);
    glDeleteBuffers(1, &precipVBO);
    glDeleteFramebuffers(1, &shadowMapFBO);
    glDeleteTextures(1, &shadowMapTexture);

    // Cleanup Hi-Z culling resources
    glDeleteBuffers(2, visibilitySSBO);
    glDeleteBuffers(1, &chunkBoundsSSBO);
    for (int i = 0; i < 2; i++) {
        if (visibilityFence[i] != nullptr) {
            glDeleteSync(visibilityFence[i]);
        }
    }

    // Cleanup SSAO resources
    glDeleteBuffers(1, &ssaoKernelUBO);

    // Shutdown vertex pool
    if (g_useVertexPool) {
        VertexPool::getInstance().shutdown();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "Engine shut down successfully." << std::endl;
    return 0;
}
