// Vulkan Backend Entry Point
// Uses DeferredRendererRHI with Vulkan backend for rendering

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "core/Camera.h"
#include "core/Player.h"
#include "core/Raycast.h"
#include "core/Config.h"
#include "core/CrashHandler.h"
#include "world/World.h"
#include "world/WorldPresets.h"
#include "world/WorldSaveLoad.h"
#include "render/DeferredRendererRHI.h"
#include "render/Renderer.h"
#include "render/TextureAtlas.h"
#include "ui/MenuUI.h"
#include "ui/MainMenu.h"
#include "ui/WorldSelectScreen.h"
#include "ui/WorldCreateScreen.h"
#include "ui/PauseMenu.h"
#include "ui/SettingsMenu.h"
#include "ui/DebugOverlay.h"
#include "input/KeybindManager.h"

#include <iostream>
#include <memory>
#include <chrono>

// Force high-performance GPU on laptops
#ifdef _WIN32
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

// OpenGL mesh shader stubs (referenced by World.h but not used in Vulkan)
bool g_meshShadersAvailable = false;
bool g_enableMeshShaders = false;
unsigned int meshShaderProgram = 0;
unsigned int meshShaderDataUBO = 0;
unsigned int frustumPlanesUBO = 0;
typedef void (*PFNGLDRAWMESHTASKSNVPROC_LOCAL)(unsigned int, unsigned int);
PFNGLDRAWMESHTASKSNVPROC_LOCAL pfn_glDrawMeshTasksNV = nullptr;

// Window settings
int WINDOW_WIDTH = 1280;
int WINDOW_HEIGHT = 720;
const char* WINDOW_TITLE = "Voxel Engine (Vulkan)";

// Global state
Camera camera(glm::vec3(8.0f, 100.0f, 8.0f));
Player* player = nullptr;
float lastX = WINDOW_WIDTH / 2.0f;
float lastY = WINDOW_HEIGHT / 2.0f;
bool firstMouse = true;
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// World
World world;

// Game state
enum class GameState { MAIN_MENU, WORLD_SELECT, WORLD_CREATE, LOADING, PLAYING, PAUSED };
GameState gameState = GameState::MAIN_MENU;

// Input state
bool cursorEnabled = true;

// Block interaction
std::optional<RaycastHit> currentTarget;
constexpr float REACH_DISTANCE = 5.0f;
BlockType selectedBlock = BlockType::STONE;
int selectedSlot = 0;
BlockType hotbar[] = {
    BlockType::STONE, BlockType::DIRT, BlockType::GRASS,
    BlockType::COBBLESTONE, BlockType::WOOD_PLANKS, BlockType::WOOD_LOG,
    BlockType::WATER, BlockType::GLASS, BlockType::SAND
};
constexpr int HOTBAR_SIZE = sizeof(hotbar) / sizeof(hotbar[0]);

// Time of day
float timeOfDay = 0.25f; // Start at sunrise
bool doDaylightCycle = true;

// Debug
bool g_showDebugOverlay = false;

// Renderer
std::unique_ptr<Render::DeferredRendererRHI> g_renderer;

// Loading state
int totalChunksToLoad = 0;
int chunksLoaded = 0;
std::string loadingMessage = "Loading...";

// World save/load
WorldSaveLoad worldSaveLoad;
WorldSettings worldSettings;

// Mouse callbacks
void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    if (cursorEnabled) return;

    float xposF = static_cast<float>(xpos);
    float yposF = static_cast<float>(ypos);

    if (firstMouse) {
        lastX = xposF;
        lastY = yposF;
        firstMouse = false;
    }

    float xoffset = xposF - lastX;
    float yoffset = lastY - yposF;
    lastX = xposF;
    lastY = yposF;

    camera.processMouseMovement(xoffset, yoffset);
}

void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    if (gameState == GameState::PLAYING) {
        selectedSlot -= static_cast<int>(yoffset);
        if (selectedSlot < 0) selectedSlot = HOTBAR_SIZE - 1;
        if (selectedSlot >= HOTBAR_SIZE) selectedSlot = 0;
        selectedBlock = hotbar[selectedSlot];
    }
}

void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    WINDOW_WIDTH = width;
    WINDOW_HEIGHT = height;
    if (g_renderer) {
        g_renderer->resize(width, height);
    }
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_ESCAPE) {
            if (gameState == GameState::PLAYING) {
                gameState = GameState::PAUSED;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                cursorEnabled = true;
            } else if (gameState == GameState::PAUSED) {
                gameState = GameState::PLAYING;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                cursorEnabled = false;
                firstMouse = true;
            }
        }

        if (key == GLFW_KEY_F3) {
            g_showDebugOverlay = !g_showDebugOverlay;
        }

        if (key == GLFW_KEY_F11) {
            // Toggle fullscreen
            static bool isFullscreen = false;
            isFullscreen = !isFullscreen;
            if (isFullscreen) {
                GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
            } else {
                glfwSetWindowMonitor(window, nullptr, 100, 100, 1280, 720, 0);
            }
        }

        // Hotbar selection with number keys
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) {
            int slot = key - GLFW_KEY_1;
            if (slot < HOTBAR_SIZE) {
                selectedSlot = slot;
                selectedBlock = hotbar[selectedSlot];
            }
        }
    }
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (gameState != GameState::PLAYING || cursorEnabled) return;

    if (action == GLFW_PRESS) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && currentTarget.has_value()) {
            // Break block
            world.setBlock(currentTarget->blockPos.x, currentTarget->blockPos.y, currentTarget->blockPos.z, BlockType::AIR);
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT && currentTarget.has_value()) {
            // Place block
            glm::ivec3 placePos = currentTarget->blockPos + currentTarget->normal;
            world.setBlock(placePos.x, placePos.y, placePos.z, selectedBlock);
        }
    }
}

void processInput(GLFWwindow* window) {
    if (gameState != GameState::PLAYING || cursorEnabled) return;

    // Movement
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.processKeyboard(CameraMovement::FORWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera.processKeyboard(CameraMovement::BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.processKeyboard(CameraMovement::LEFT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.processKeyboard(CameraMovement::RIGHT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        camera.processKeyboard(CameraMovement::UP, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
        camera.processKeyboard(CameraMovement::DOWN, deltaTime);

    // Sprint
    camera.setSprinting(glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS);
}

int main() {
    std::cout << "=== Voxel Engine (Vulkan Backend) ===" << std::endl;

    // Initialize crash handler
    Core::CrashHandler::instance().initialize("VoxelEngineVK");

    // Load config
    g_config.load();
    g_config.renderer = RendererType::VULKAN;  // Force Vulkan backend

    // Disable OpenGL mesh operations for Vulkan backend
    world.useOpenGLMeshes = false;

    WINDOW_WIDTH = g_config.windowWidth;
    WINDOW_HEIGHT = g_config.windowHeight;
    camera.fov = static_cast<float>(g_config.fov);

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    // Check Vulkan support
    if (!glfwVulkanSupported()) {
        std::cerr << "Vulkan is not supported on this system" << std::endl;
        glfwTerminate();
        return -1;
    }

    // Configure for Vulkan (no OpenGL context)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    // Create window
    GLFWwindow* window = nullptr;
    if (g_config.fullscreen) {
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        window = glfwCreateWindow(mode->width, mode->height, WINDOW_TITLE, monitor, nullptr);
        WINDOW_WIDTH = mode->width;
        WINDOW_HEIGHT = mode->height;
    } else {
        window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE, nullptr, nullptr);
    }

    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    // Set callbacks
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);

    std::cout << "Window created (Vulkan mode)" << std::endl;

    // Initialize RHI Renderer
    std::cout << "\n=== Initializing Vulkan Renderer ===" << std::endl;
    g_renderer = std::make_unique<Render::DeferredRendererRHI>();

    Render::RenderConfig renderConfig;
    renderConfig.enableShadows = g_config.enableShadows;
    renderConfig.enableSSAO = g_config.enableSSAO;
    renderConfig.enableGPUCulling = true;
    renderConfig.enableHiZCulling = g_config.enableHiZCulling;
    renderConfig.shadowResolution = g_config.shadowResolution;
    renderConfig.ssaoSamples = g_config.ssaoSamples;

    if (!g_renderer->initialize(window, renderConfig)) {
        std::cerr << "Failed to initialize Vulkan renderer" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // Get device info
    if (g_renderer->getDevice()) {
        const auto& info = g_renderer->getDevice()->getInfo();
        std::cout << "GPU: " << info.deviceName << std::endl;
        std::cout << "API: " << info.apiVersion << std::endl;
        Core::CrashHandler::instance().setGPUInfo("Vulkan: " + info.deviceName + " (" + info.apiVersion + ")");
    }

    std::cout << "\n=== Voxel Engine (Vulkan) Started ===" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  WASD - Move, Mouse - Look" << std::endl;
    std::cout << "  Space/Ctrl - Up/Down, Shift - Sprint" << std::endl;
    std::cout << "  Left Click - Break, Right Click - Place" << std::endl;
    std::cout << "  1-9 - Select block, Scroll - Cycle blocks" << std::endl;
    std::cout << "  ESC - Pause, F3 - Debug, F11 - Fullscreen" << std::endl;

    // Initialize world settings
    world.renderDistance = g_config.renderDistance;
    world.gpuCullingEnabled = true;

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        // Delta time
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Input
        glfwPollEvents();
        processInput(window);

        // Update time of day
        if (doDaylightCycle && gameState == GameState::PLAYING) {
            timeOfDay += deltaTime * 0.001f; // ~16 minute day cycle
            if (timeOfDay > 1.0f) timeOfDay -= 1.0f;
        }

        // Game state logic
        switch (gameState) {
            case GameState::MAIN_MENU: {
                // For now, auto-start a new world
                // TODO: Implement proper menu UI with Vulkan
                static bool started = false;
                if (!started) {
                    started = true;
                    std::cout << "\nGenerating world..." << std::endl;

                    // Generate a simple world
                    worldSettings.seed = "VulkanTest";
                    worldSettings.computeSeed();
                    world.setSeed(worldSettings.seedValue);

                    // Calculate expected chunk count for loading progress
                    int loadRadius = std::min(g_config.renderDistance, 8);
                    int diameter = loadRadius * 2 + 1;
                    totalChunksToLoad = diameter * diameter;

                    // Trigger initial chunk loading via update
                    std::cout << "[main] Calling world.update()..." << std::endl;
                    std::cout.flush();
                    world.update(camera.position);
                    std::cout << "[main] world.update() completed" << std::endl;
                    std::cout.flush();

                    gameState = GameState::LOADING;
                }
                break;
            }

            case GameState::LOADING: {
                // Process chunk generation
                std::cout << "[main] LOADING state - calling world.update()..." << std::endl;
                std::cout.flush();
                world.update(camera.position);
                std::cout << "[main] LOADING state - world.update() done" << std::endl;
                std::cout.flush();

                chunksLoaded = static_cast<int>(world.getChunkCount());

                if (chunksLoaded >= totalChunksToLoad * 0.8f) {
                    // Find spawn position
                    glm::vec3 spawnPos(8.0f, 100.0f, 8.0f);
                    for (int y = 255; y > 0; y--) {
                        if (world.getBlock(8, y, 8) != BlockType::AIR) {
                            spawnPos.y = static_cast<float>(y + 2);
                            break;
                        }
                    }

                    camera.position = spawnPos;
                    gameState = GameState::PLAYING;
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    cursorEnabled = false;
                    std::cout << "World loaded! Spawning at " << spawnPos.x << ", " << spawnPos.y << ", " << spawnPos.z << std::endl;
                }
                break;
            }

            case GameState::PLAYING: {
                // Update world
                world.update(camera.position);

                // Raycast for block selection
                currentTarget = Raycast::cast(
                    camera.position,
                    camera.front,
                    REACH_DISTANCE,
                    [](int x, int y, int z) -> bool {
                        BlockType block = world.getBlock(x, y, z);
                        return block != BlockType::AIR && block != BlockType::WATER;
                    }
                );
                break;
            }

            case GameState::PAUSED: {
                // Paused - just wait for ESC
                break;
            }

            default:
                break;
        }

        // Render
        std::cout << "[main] About to render..." << std::endl;
        std::cout.flush();
        if (g_renderer) {
            std::cout << "[main] g_renderer exists, building camera data..." << std::endl;
            std::cout.flush();
            // Build camera data
            float aspectRatio = static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT);
            glm::mat4 view = camera.getViewMatrix();
            glm::mat4 projection = glm::perspective(glm::radians(camera.fov), aspectRatio, 0.1f, 500.0f);

            Render::CameraData cameraData;
            cameraData.view = view;
            cameraData.projection = projection;
            cameraData.viewProjection = projection * view;
            cameraData.invView = glm::inverse(view);
            cameraData.invProjection = glm::inverse(projection);
            cameraData.invViewProjection = glm::inverse(cameraData.viewProjection);
            cameraData.position = camera.position;
            cameraData.forward = camera.front;
            cameraData.nearPlane = 0.1f;
            cameraData.farPlane = 500.0f;
            cameraData.fov = camera.fov;
            cameraData.aspectRatio = aspectRatio;

            // Lighting based on time of day
            float sunAngle = timeOfDay * 2.0f * 3.14159f;
            glm::vec3 lightDir = glm::normalize(glm::vec3(
                cos(sunAngle),
                sin(sunAngle) * 0.8f + 0.2f,
                0.3f
            ));

            float daylight = glm::clamp(sin(sunAngle) + 0.2f, 0.0f, 1.0f);
            glm::vec3 lightColor = glm::vec3(1.0f, 0.95f, 0.9f) * daylight;
            glm::vec3 ambientColor = glm::vec3(0.1f, 0.12f, 0.15f) + glm::vec3(0.1f) * daylight;

            Render::LightingParams lighting;
            lighting.lightDir = lightDir;
            lighting.lightColor = lightColor;
            lighting.ambientColor = ambientColor;
            lighting.skyColor = glm::vec3(0.5f, 0.7f, 1.0f) * daylight;
            lighting.shadowStrength = 0.6f;
            lighting.time = timeOfDay;

            Render::FogParams fog;
            fog.density = g_config.fogDensity;
            fog.heightFalloff = 0.015f;
            fog.baseHeight = 64.0f;
            fog.renderDistance = static_cast<float>(world.renderDistance * 16);
            fog.isUnderwater = false;

            g_renderer->setLighting(lighting);
            g_renderer->setFog(fog);

            // Render frame
            std::cout << "[main] Calling beginFrame()..." << std::endl;
            std::cout.flush();
            g_renderer->beginFrame();
            std::cout << "[main] Calling render()..." << std::endl;
            std::cout.flush();
            g_renderer->render(world, cameraData);
            std::cout << "[main] Calling endFrame()..." << std::endl;
            std::cout.flush();
            g_renderer->endFrame();
            std::cout << "[main] Frame complete" << std::endl;
            std::cout.flush();
        }
    }

    // Cleanup
    std::cout << "\nShutting down..." << std::endl;
    g_renderer.reset();
    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "Goodbye!" << std::endl;
    return 0;
}
