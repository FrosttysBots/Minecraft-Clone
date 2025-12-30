// ============================================================================
// WIP: VULKAN BACKEND ENTRY POINT - Work In Progress
// This executable (VoxelEngineVK) uses the Vulkan rendering backend.
// Currently disabled while development focuses on OpenGL and gameplay mechanics.
// To re-enable: uncomment VoxelEngineVK target in CMakeLists.txt
// ============================================================================

// Vulkan Backend Entry Point
// Uses DeferredRendererRHI with Vulkan backend for rendering
// Full-featured version with menus, world creation, texture packs, etc.

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
#include "ui/VulkanMenuUI.h"
#include "ui/VulkanMainMenu.h"
#include "ui/VulkanScreens.h"
#include "ui/VulkanSettingsMenu.h"
#include "ui/VulkanTexturePackScreen.h"
#include "ui/TitleScreenWorld.h"
#include "render/TexturePackLoader.h"
#include "input/KeybindManager.h"

#include <random>
#include <filesystem>
#include <fstream>
#include <map>

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
const char* WINDOW_TITLE = "ForgeBound (Vulkan)";

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

// Vulkan Menu UI
VulkanMenuUIRenderer g_vulkanUI;
VulkanMainMenu g_mainMenu;
VulkanWorldSelectScreen g_worldSelectScreen;
VulkanWorldCreateScreen g_worldCreateScreen;
VulkanPauseMenu g_pauseMenu;
VulkanLoadingScreen g_loadingScreen;
VulkanSettingsMenu g_settingsMenu;
VulkanTexturePackScreen g_texturePackScreen;
TitleScreenWorld g_titleScreenWorld;

// Texture pack loader
TexturePackLoader g_texturePack;

// Loading state
int totalChunksToLoad = 0;
int chunksLoaded = 0;
std::string loadingMessage = "Loading...";

// World save/load
WorldSaveLoad worldSaveLoad;
WorldSettings worldSettings;

// Menu state
int mainMenuSelection = 0;
int worldSelectSelection = 0;
int pauseMenuSelection = 0;
std::vector<std::string> savedWorlds;
GameState lastGameState = GameState::MAIN_MENU;  // For detecting state changes
bool showSettings = false;
bool showTexturePacks = false;
std::string newWorldName = "New World";
std::string newWorldSeed = "";
int settingsTab = 0;  // 0=Video, 1=Audio, 2=Controls

// Mouse state for menus
double mouseX = 0, mouseY = 0;
bool mouseDown = false;
bool mouseClicked = false;  // True for one frame on click

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
    std::cout << "=== ForgeBound (Vulkan Backend) ===" << std::endl;

    // Initialize crash handler
    Core::CrashHandler::instance().initialize("ForgeBound-VK", "InfDev 2.0");

    // Load config
    g_config.load();
    g_config.renderer = RendererType::VULKAN;  // Force Vulkan backend

    // Disable OpenGL mesh operations for Vulkan backend
    world.useOpenGLMeshes = false;
    std::cout << "[Vulkan] Set world.useOpenGLMeshes = " << (world.useOpenGLMeshes ? "true" : "false") << std::endl;

    // Initialize thread pool for async chunk/mesh generation
    world.initThreadPool(g_config.chunkThreads, g_config.meshThreads);

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

    // Initialize Vulkan menu system
    std::cout << "\n=== Initializing Menu System ===" << std::endl;
    g_vulkanUI.init(g_renderer.get(), WINDOW_WIDTH, WINDOW_HEIGHT);
    g_mainMenu.init(&g_vulkanUI);
    g_worldSelectScreen.init(&g_vulkanUI);
    g_worldCreateScreen.init(&g_vulkanUI);
    g_pauseMenu.init(&g_vulkanUI);
    g_loadingScreen.init(&g_vulkanUI);
    g_settingsMenu.init(&g_vulkanUI);
    g_texturePackScreen.init(&g_vulkanUI, &g_texturePack);
    std::cout << "Vulkan menu system initialized" << std::endl;

    // Initialize title screen world with Vulkan mode
    std::cout << "\n=== Initializing Title Screen ===" << std::endl;
    g_titleScreenWorld.init(g_config.titleScreen, true);  // true = Vulkan mode
    g_titleScreenWorld.setProjection(WINDOW_WIDTH, WINDOW_HEIGHT);
    std::cout << "Title screen world initialized (Vulkan mode)" << std::endl;

    std::cout << "\n=== ForgeBound (Vulkan) Started ===" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  WASD - Move, Mouse - Look" << std::endl;
    std::cout << "  Space/Ctrl - Up/Down, Shift - Sprint" << std::endl;
    std::cout << "  Left Click - Break, Right Click - Place" << std::endl;
    std::cout << "  1-9 - Select block, Scroll - Cycle blocks" << std::endl;
    std::cout << "  ESC - Pause, F3 - Debug, F11 - Fullscreen" << std::endl;
    std::cout << "\n=== Menu System (No Visual UI Yet) ===" << std::endl;
    std::cout << "Current state: MAIN_MENU" << std::endl;
    std::cout << "Main Menu buttons (click regions):" << std::endl;
    std::cout << "  Singleplayer: center of screen, y=" << (WINDOW_HEIGHT/2 - 50) << std::endl;
    std::cout << "  Settings:     center of screen, y=" << (WINDOW_HEIGHT/2 + 10) << std::endl;
    std::cout << "  Quit:         center of screen, y=" << (WINDOW_HEIGHT/2 + 70) << std::endl;

    // Initialize world settings
    world.renderDistance = g_config.renderDistance;
    world.gpuCullingEnabled = true;

    // Helper: Load world metadata from file
    auto loadWorldMeta = [](const std::string& worldPath) -> std::map<std::string, std::string> {
        std::map<std::string, std::string> meta;
        std::ifstream file(worldPath + "/world.meta");
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                size_t eq = line.find('=');
                if (eq != std::string::npos) {
                    meta[line.substr(0, eq)] = line.substr(eq + 1);
                }
            }
        }
        return meta;
    };

    // Helper: Check if mouse is inside a rectangle
    auto isMouseInRect = [](float x, float y, float w, float h) -> bool {
        return mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h;
    };

    // Helper: Start a new world with given settings
    auto startNewWorld = [&](const std::string& worldName, const std::string& seed) {
        std::cout << "\nGenerating world: " << worldName << std::endl;

        // Setup world settings
        worldSettings.worldName = worldName;
        worldSettings.seed = seed.empty() ? std::to_string(std::random_device{}()) : seed;
        worldSettings.computeSeed();
        world.setSeed(worldSettings.seedValue);

        // Reset world state
        world.reset();

        // Calculate expected chunk count for loading progress
        int loadRadius = std::min(g_config.renderDistance, 8);
        int diameter = loadRadius * 2 + 1;
        totalChunksToLoad = diameter * diameter;
        chunksLoaded = 0;

        // Trigger initial chunk loading
        world.update(camera.position);
        gameState = GameState::LOADING;
        loadingMessage = "Generating terrain...";
    };

    // Helper: Load existing world
    auto loadWorld = [&](const std::string& worldName) {
        std::cout << "\nLoading world: " << worldName << std::endl;

        std::string worldPath = "saves/" + worldName;

        // Load world metadata
        auto meta = loadWorldMeta(worldPath);
        if (!meta.empty()) {
            worldSettings.worldName = worldName;

            // Get seed from metadata
            if (meta.count("seed")) {
                worldSettings.seedValue = std::stoll(meta["seed"]);
                world.setSeed(worldSettings.seedValue);
            }

            // Load player position if available
            glm::vec3 playerPos;
            float yaw = 0, pitch = 0;
            bool isFlying = false;
            if (WorldSaveLoad::loadPlayer(worldPath, playerPos, yaw, pitch, isFlying)) {
                camera.position = playerPos;
                camera.setOrientation(yaw, pitch);
            }

            // Reset and start loading
            world.reset();

            int loadRadius = std::min(g_config.renderDistance, 8);
            int diameter = loadRadius * 2 + 1;
            totalChunksToLoad = diameter * diameter;
            chunksLoaded = 0;

            world.update(camera.position);
            gameState = GameState::LOADING;
            loadingMessage = "Loading world...";

            // Set world save path for chunk caching
            worldSaveLoad.currentWorldPath = worldPath;
            worldSaveLoad.hasLoadedWorld = true;
            world.setWorldSavePath(worldPath);
        } else {
            std::cerr << "Failed to load world metadata: " << worldName << std::endl;
        }
    };

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        // Delta time
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Input
        glfwPollEvents();

        // Get mouse position for menus
        glfwGetCursorPos(window, &mouseX, &mouseY);
        bool wasMouseDown = mouseDown;
        mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        mouseClicked = mouseDown && !wasMouseDown;  // Detect click (press edge)

        processInput(window);

        // Update time of day
        if (doDaylightCycle && gameState == GameState::PLAYING) {
            timeOfDay += deltaTime * 0.001f; // ~16 minute day cycle
            if (timeOfDay > 1.0f) timeOfDay -= 1.0f;
        }

        // Log state changes
        if (gameState != lastGameState) {
            const char* stateNames[] = {"MAIN_MENU", "WORLD_SELECT", "WORLD_CREATE", "LOADING", "PLAYING", "PAUSED"};
            std::cout << "[GameState] Changed to: " << stateNames[static_cast<int>(gameState)] << std::endl;
            lastGameState = gameState;
        }

        // Game state logic - use new menu classes
        switch (gameState) {
            case GameState::MAIN_MENU: {
                // Handle overlay menus first
                if (showSettings) {
                    g_settingsMenu.update(mouseX, mouseY, mouseDown, deltaTime);
                    SettingsAction settingsAction = g_settingsMenu.getAction();
                    if (settingsAction == SettingsAction::BACK) {
                        showSettings = false;
                    }
                } else if (showTexturePacks) {
                    g_texturePackScreen.update(mouseX, mouseY, mouseDown, deltaTime);
                    TexturePackAction texAction = g_texturePackScreen.getAction();
                    if (texAction == TexturePackAction::DONE) {
                        showTexturePacks = false;
                    }
                } else {
                    // Update main menu
                    g_mainMenu.update(mouseX, mouseY, mouseDown);
                    MenuAction action = g_mainMenu.getAction();
                    if (action == MenuAction::PLAY_GAME) {
                        std::cout << "[Menu] Play Game clicked" << std::endl;
                        g_worldSelectScreen.refreshWorldList();
                        gameState = GameState::WORLD_SELECT;
                    } else if (action == MenuAction::SETTINGS) {
                        std::cout << "[Menu] Settings clicked" << std::endl;
                        g_settingsMenu.refreshFromConfig();
                        showSettings = true;
                    } else if (action == MenuAction::TEXTURE_PACKS) {
                        std::cout << "[Menu] Texture Packs clicked" << std::endl;
                        g_texturePackScreen.refreshPackList();
                        showTexturePacks = true;
                    } else if (action == MenuAction::REFRESH_WORLD) {
                        std::cout << "[Menu] Refresh World clicked" << std::endl;
                        g_titleScreenWorld.cleanup();
                        g_titleScreenWorld.init(g_config.titleScreen, true);  // true = Vulkan mode
                    } else if (action == MenuAction::COPY_SEED) {
                        std::cout << "[Menu] Copy Seed clicked" << std::endl;
                        std::string seedStr = std::to_string(g_titleScreenWorld.getCurrentSeed());
                        glfwSetClipboardString(window, seedStr.c_str());
                    } else if (action == MenuAction::EXIT) {
                        std::cout << "[Menu] Exit clicked" << std::endl;
                        glfwSetWindowShouldClose(window, GLFW_TRUE);
                    }
                }

                // Update title screen world (generates chunks in background)
                g_titleScreenWorld.update(deltaTime);
                break;
            }

            case GameState::WORLD_SELECT: {
                g_worldSelectScreen.update(mouseX, mouseY, mouseDown, deltaTime);
                VulkanWorldSelectAction action = g_worldSelectScreen.getAction();
                if (action == VulkanWorldSelectAction::BACK) {
                    gameState = GameState::MAIN_MENU;
                } else if (action == VulkanWorldSelectAction::CREATE_WORLD) {
                    g_worldCreateScreen.reset();
                    gameState = GameState::WORLD_CREATE;
                } else if (action == VulkanWorldSelectAction::PLAY_SELECTED) {
                    const auto* selectedWorld = g_worldSelectScreen.getSelectedWorld();
                    if (selectedWorld) {
                        std::cout << "[Menu] Loading world: " << selectedWorld->name << std::endl;
                        loadWorld(selectedWorld->name);
                    }
                } else if (action == VulkanWorldSelectAction::DELETE_SELECTED) {
                    const auto* selectedWorld = g_worldSelectScreen.getSelectedWorld();
                    if (selectedWorld) {
                        std::cout << "[Menu] Deleting world: " << selectedWorld->name << std::endl;
                        std::filesystem::remove_all(selectedWorld->folderPath);
                        g_worldSelectScreen.refreshWorldList();
                    }
                }
                break;
            }

            case GameState::WORLD_CREATE: {
                g_worldCreateScreen.update(mouseX, mouseY, mouseDown, deltaTime);
                VulkanWorldCreateAction action = g_worldCreateScreen.getAction();
                if (action == VulkanWorldCreateAction::BACK) {
                    gameState = GameState::WORLD_SELECT;
                } else if (action == VulkanWorldCreateAction::CREATE) {
                    std::string name = g_worldCreateScreen.getWorldName();
                    std::string seed = g_worldCreateScreen.getSeed();
                    startNewWorld(name, seed);
                }
                break;
            }

            case GameState::LOADING: {
                // Process chunk generation
                world.update(camera.position);
                chunksLoaded = static_cast<int>(world.getChunkCount());

                float progress = totalChunksToLoad > 0 ?
                    static_cast<float>(chunksLoaded) / totalChunksToLoad : 0.0f;

                if (progress >= 0.8f) {
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
                    // Disable burst mode to stop filesystem spam during gameplay
                    world.burstMode = false;
                    world.initialLoadComplete = true;
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    cursorEnabled = false;
                    std::cout << "World loaded! Spawning at " << spawnPos.x << ", " << spawnPos.y << ", " << spawnPos.z << std::endl;
                }
                break;
            }

            case GameState::PLAYING: {
                // Update world with timing
                static int updateCounter = 0;
                auto updateStart = std::chrono::high_resolution_clock::now();
                world.update(camera.position);
                auto updateEnd = std::chrono::high_resolution_clock::now();
                auto updateMs = std::chrono::duration_cast<std::chrono::milliseconds>(updateEnd - updateStart).count();
                if (updateCounter++ % 60 == 0 || updateMs > 100) {
                    std::cout << "[Timing] world.update() took " << updateMs << "ms" << std::endl;
                }

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
                // Handle settings overlay
                if (showSettings) {
                    g_settingsMenu.update(mouseX, mouseY, mouseDown, deltaTime);
                    SettingsAction settingsAction = g_settingsMenu.getAction();
                    if (settingsAction == SettingsAction::BACK) {
                        showSettings = false;
                    }
                } else {
                    g_pauseMenu.update(mouseX, mouseY, mouseDown);
                    VulkanPauseAction action = g_pauseMenu.getAction();
                    if (action == VulkanPauseAction::RESUME) {
                        gameState = GameState::PLAYING;
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                        cursorEnabled = false;
                        firstMouse = true;
                    } else if (action == VulkanPauseAction::SETTINGS) {
                        g_settingsMenu.refreshFromConfig();
                        showSettings = true;
                    } else if (action == VulkanPauseAction::SAVE_QUIT) {
                        // Save world
                        std::string worldPath = "saves/" + worldSettings.worldName;
                        std::filesystem::create_directories(worldPath);
                        WorldSaveLoad::saveWorldMeta(worldPath, worldSettings.worldName,
                            static_cast<int>(worldSettings.seedValue),
                            static_cast<int>(worldSettings.generationType),
                            worldSettings.maxYHeight);
                        WorldSaveLoad::savePlayer(worldPath, camera.position, camera.yaw, camera.pitch, false);

                        // Return to main menu
                        world.reset();
                        gameState = GameState::MAIN_MENU;
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                        cursorEnabled = true;
                    }
                }
                break;
            }

            default:
                break;
        }

        // Render
        if (g_renderer) {
            // Build camera data - use title screen camera when in MAIN_MENU
            float aspectRatio = static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT);
            glm::mat4 view, projection;
            glm::vec3 camPos, camFront;

            if (gameState == GameState::MAIN_MENU && g_titleScreenWorld.isReady()) {
                // Use title screen world's camera
                view = g_titleScreenWorld.getViewMatrix();
                projection = g_titleScreenWorld.getProjectionMatrix();
                camPos = g_titleScreenWorld.getCameraPosition();
                camFront = glm::normalize(g_titleScreenWorld.orbitCenter - camPos);
            } else {
                // Use player camera
                view = camera.getViewMatrix();
                projection = glm::perspective(glm::radians(camera.fov), aspectRatio, 0.1f, 500.0f);
                camPos = camera.position;
                camFront = camera.front;
            }

            Render::CameraData cameraData;
            cameraData.view = view;
            cameraData.projection = projection;
            cameraData.viewProjection = projection * view;
            cameraData.invView = glm::inverse(view);
            cameraData.invProjection = glm::inverse(projection);
            cameraData.invViewProjection = glm::inverse(cameraData.viewProjection);
            cameraData.position = camPos;
            cameraData.forward = camFront;
            // Use title screen near/far planes when applicable
            bool isTitleScreen = gameState == GameState::MAIN_MENU && g_titleScreenWorld.isReady();
            cameraData.nearPlane = isTitleScreen ? 1.0f : 0.1f;
            cameraData.farPlane = isTitleScreen ? 800.0f : 500.0f;
            cameraData.fov = isTitleScreen ? 65.0f : camera.fov;
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
            fog.renderDistance = isTitleScreen ? static_cast<float>(g_titleScreenWorld.world->renderDistance * 16) : static_cast<float>(world.renderDistance * 16);
            fog.isUnderwater = false;

            g_renderer->setLighting(lighting);
            g_renderer->setFog(fog);

            // Set menu mode based on game state
            bool inMenu = (gameState == GameState::MAIN_MENU ||
                          gameState == GameState::WORLD_SELECT ||
                          gameState == GameState::WORLD_CREATE ||
                          gameState == GameState::LOADING ||
                          gameState == GameState::PAUSED);
            g_renderer->setMenuMode(inMenu);

            // Use darker background for menus
            if (inMenu) {
                g_renderer->setMenuClearColor(glm::vec4(0.05f, 0.06f, 0.08f, 1.0f));
            }

            // Render frame
            g_renderer->beginFrame();

            static int renderCounter = 0;
            auto renderStart = std::chrono::high_resolution_clock::now();

            // Render title screen world in MAIN_MENU, otherwise render game world
            if (gameState == GameState::MAIN_MENU && g_titleScreenWorld.isReady() && g_titleScreenWorld.world) {
                g_renderer->render(*g_titleScreenWorld.world, cameraData);
            } else {
                g_renderer->render(world, cameraData);
            }

            auto renderEnd = std::chrono::high_resolution_clock::now();
            auto renderMs = std::chrono::duration_cast<std::chrono::milliseconds>(renderEnd - renderStart).count();
            if (renderCounter++ % 60 == 0 || renderMs > 100) {
                std::cout << "[Timing] render() took " << renderMs << "ms" << std::endl;
            }

            // Render UI overlay for menu states using new menu classes
            if (inMenu) {
                g_renderer->beginUIOverlay();

                switch (gameState) {
                    case GameState::MAIN_MENU:
                        g_mainMenu.render();
                        // Render overlay menus on top
                        if (showSettings) {
                            g_settingsMenu.render();
                        } else if (showTexturePacks) {
                            g_texturePackScreen.render();
                        }
                        break;

                    case GameState::WORLD_SELECT:
                        g_worldSelectScreen.render();
                        break;

                    case GameState::WORLD_CREATE:
                        g_worldCreateScreen.render(deltaTime);
                        break;

                    case GameState::LOADING: {
                        float progress = totalChunksToLoad > 0 ?
                            static_cast<float>(chunksLoaded) / static_cast<float>(totalChunksToLoad) : 0.0f;
                        g_loadingScreen.setMessage(loadingMessage);
                        g_loadingScreen.setProgress(progress);
                        g_loadingScreen.render();
                        break;
                    }

                    case GameState::PAUSED:
                        g_pauseMenu.render();
                        // Render settings overlay on top if open
                        if (showSettings) {
                            g_settingsMenu.render();
                        }
                        break;

                    default:
                        break;
                }

                g_renderer->endUIOverlay();
            }

            g_renderer->endFrame();
        }
    }

    // Cleanup
    std::cout << "\nShutting down..." << std::endl;
    g_titleScreenWorld.cleanup();
    g_renderer.reset();
    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "Goodbye!" << std::endl;
    return 0;
}
