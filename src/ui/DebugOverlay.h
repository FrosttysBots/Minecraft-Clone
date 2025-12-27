#pragma once

// Debug Overlay (F3 Screen)
// Minecraft-style debug information display with color coding
// Responsive layout that adapts to screen size

#include <glad/gl.h>
#include "MenuUI.h"
#include "../core/Camera.h"
#include "../core/Player.h"
#include "../world/World.h"
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>

// Windows-specific includes for system info
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
// Undefine problematic Windows macros that conflict with our code
#undef CREATE_NEW
#undef DELETE
#undef TRANSPARENT
#undef near
#undef far
#endif

// Color scheme for debug info
namespace DebugColors {
    const glm::vec4 TITLE = glm::vec4(1.0f, 1.0f, 0.4f, 1.0f);       // Yellow - section titles
    const glm::vec4 LABEL = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);       // Gray - labels
    const glm::vec4 VALUE = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);       // White - values
    const glm::vec4 GOOD = glm::vec4(0.4f, 1.0f, 0.4f, 1.0f);        // Green - good values
    const glm::vec4 WARN = glm::vec4(1.0f, 0.8f, 0.2f, 1.0f);        // Orange - warning
    const glm::vec4 BAD = glm::vec4(1.0f, 0.3f, 0.3f, 1.0f);         // Red - bad values
    const glm::vec4 POS_X = glm::vec4(1.0f, 0.4f, 0.4f, 1.0f);       // Red - X axis
    const glm::vec4 POS_Y = glm::vec4(0.4f, 1.0f, 0.4f, 1.0f);       // Green - Y axis
    const glm::vec4 POS_Z = glm::vec4(0.4f, 0.6f, 1.0f, 1.0f);       // Blue - Z axis
    const glm::vec4 TIME = glm::vec4(0.6f, 0.8f, 1.0f, 1.0f);        // Light blue - time
    const glm::vec4 BIOME = glm::vec4(0.5f, 1.0f, 0.8f, 1.0f);       // Cyan - biome
    const glm::vec4 MEMORY = glm::vec4(1.0f, 0.6f, 1.0f, 1.0f);      // Pink - memory
    const glm::vec4 BG = glm::vec4(0.0f, 0.0f, 0.0f, 0.7f);          // Semi-transparent background
}

class DebugOverlay {
public:
    MenuUIRenderer* ui = nullptr;
    bool visible = false;

    // Cached values (updated each frame)
    float currentFps = 0.0f;
    float frameTime = 0.0f;
    glm::vec3 playerPos = glm::vec3(0);
    glm::vec3 playerVelocity = glm::vec3(0);
    float yaw = 0.0f;
    float pitch = 0.0f;
    int chunkX = 0;
    int chunkZ = 0;
    int blockX = 0;
    int blockY = 0;
    int blockZ = 0;
    float timeOfDay = 0.0f;
    int loadedChunks = 0;
    int loadedMeshes = 0;
    int renderedChunks = 0;
    int renderedSubChunks = 0;
    size_t vertexMemory = 0;
    bool isFlying = false;
    bool isInWater = false;
    bool isOnGround = false;
    std::string facingDirection = "North";
    std::string biome = "Plains";
    std::string gameVersion = "VoxelEngine 1.0";

    // GPU info
    std::string gpuName = "";
    std::string openglVersion = "";
    std::string rendererBackend = "OpenGL 4.6";  // Current renderer (OpenGL/Vulkan)

    // System info (CPU/RAM)
    std::string cpuName = "";
    size_t totalRAM = 0;          // Total system RAM in bytes
    size_t usedRAM = 0;           // Used system RAM in bytes
    size_t processRAM = 0;        // RAM used by this process in bytes
    float cpuUsage = 0.0f;        // CPU usage percentage (0-100)

    // Performance counters
    int drawCalls = 0;
    int triangleCount = 0;
    int culledChunks = 0;
    float gpuTime = 0.0f;         // GPU frame time in ms
    float meshGenTime = 0.0f;     // Mesh generation time in ms

    // GPU memory info (NVIDIA NVX extension)
    size_t gpuTotalVRAM = 0;      // Total GPU VRAM in KB
    size_t gpuAvailVRAM = 0;      // Available GPU VRAM in KB
    float gpuVRAMUsage = 0.0f;    // GPU VRAM usage percentage

    // CPU timing for usage calculation
#ifdef _WIN32
    ULARGE_INTEGER lastCPU = {0};
    ULARGE_INTEGER lastSysCPU = {0};
    ULARGE_INTEGER lastUserCPU = {0};
    int numProcessors = 1;
    HANDLE selfProcess = nullptr;
    float cpuUpdateTimer = 0.0f;
#endif

    // Layout constants (stb_easy_font uses ~6 pixels per character)
    static constexpr float CHAR_WIDTH = 6.0f;      // Width per character (stb_easy_font)
    static constexpr float LINE_HEIGHT = 18.0f;    // Height per line
    static constexpr float SECTION_GAP = 6.0f;     // Extra gap between sections
    static constexpr float PADDING = 10.0f;        // Edge padding
    static constexpr float PANEL_WIDTH = 220.0f;   // Fixed panel width (narrower for right side)

    void init(MenuUIRenderer* uiRenderer) {
        ui = uiRenderer;
        initSystemInfo();
    }

    void initSystemInfo() {
#ifdef _WIN32
        // Get CPU name from registry
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char cpuBrand[256] = {0};
            DWORD bufSize = sizeof(cpuBrand);
            if (RegQueryValueExA(hKey, "ProcessorNameString", NULL, NULL,
                (LPBYTE)cpuBrand, &bufSize) == ERROR_SUCCESS) {
                cpuName = cpuBrand;
                // Trim whitespace
                size_t start = cpuName.find_first_not_of(" ");
                size_t end = cpuName.find_last_not_of(" ");
                if (start != std::string::npos) {
                    cpuName = cpuName.substr(start, end - start + 1);
                }
            }
            RegCloseKey(hKey);
        }

        // Get number of processors for CPU usage calculation
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        numProcessors = sysInfo.dwNumberOfProcessors;

        // Get process handle
        selfProcess = GetCurrentProcess();

        // Initialize CPU timing
        FILETIME ftime, fsys, fuser;
        GetSystemTimeAsFileTime(&ftime);
        memcpy(&lastCPU, &ftime, sizeof(FILETIME));

        GetProcessTimes(selfProcess, &ftime, &ftime, &fsys, &fuser);
        memcpy(&lastSysCPU, &fsys, sizeof(FILETIME));
        memcpy(&lastUserCPU, &fuser, sizeof(FILETIME));

        // Get total RAM
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&memInfo);
        totalRAM = memInfo.ullTotalPhys;
#endif
    }

    void toggle() {
        visible = !visible;
    }

    void update(const Camera& camera, const Player* player, const World& world,
                float fps, float deltaTime, float currentTimeOfDay) {
        currentFps = fps;
        frameTime = deltaTime * 1000.0f;  // Convert to ms

        if (player) {
            playerPos = player->position;
            playerVelocity = player->velocity;
            isFlying = player->isFlying;
            isInWater = player->isInWater;
            // Estimate "on ground" based on velocity
            isOnGround = !isFlying && std::abs(player->velocity.y) < 0.01f;
        }

        yaw = camera.yaw;
        pitch = camera.pitch;

        // Calculate chunk and block position
        chunkX = static_cast<int>(std::floor(playerPos.x / 16.0f));
        chunkZ = static_cast<int>(std::floor(playerPos.z / 16.0f));
        blockX = static_cast<int>(std::floor(playerPos.x));
        blockY = static_cast<int>(std::floor(playerPos.y));
        blockZ = static_cast<int>(std::floor(playerPos.z));

        timeOfDay = currentTimeOfDay;
        loadedChunks = static_cast<int>(world.chunks.size());
        loadedMeshes = static_cast<int>(world.meshes.size());

        // Facing direction based on yaw
        float normalizedYaw = std::fmod(yaw + 360.0f, 360.0f);
        if (normalizedYaw >= 315.0f || normalizedYaw < 45.0f) {
            facingDirection = "South (+Z)";
        } else if (normalizedYaw >= 45.0f && normalizedYaw < 135.0f) {
            facingDirection = "West (-X)";
        } else if (normalizedYaw >= 135.0f && normalizedYaw < 225.0f) {
            facingDirection = "North (-Z)";
        } else {
            facingDirection = "East (+X)";
        }

        // Update system info (CPU/RAM) - only every 0.5 seconds to reduce overhead
        updateSystemInfo(deltaTime);
    }

    void updateSystemInfo(float deltaTime) {
#ifdef _WIN32
        cpuUpdateTimer += deltaTime;
        if (cpuUpdateTimer < 0.5f) return;  // Update every 500ms
        cpuUpdateTimer = 0.0f;

        // Update RAM usage
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&memInfo);
        usedRAM = memInfo.ullTotalPhys - memInfo.ullAvailPhys;

        // Get process memory usage
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(selfProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
            processRAM = pmc.WorkingSetSize;
        }

        // Calculate CPU usage for this process
        FILETIME ftime, fsys, fuser;
        ULARGE_INTEGER now, sys, user;

        GetSystemTimeAsFileTime(&ftime);
        memcpy(&now, &ftime, sizeof(FILETIME));

        GetProcessTimes(selfProcess, &ftime, &ftime, &fsys, &fuser);
        memcpy(&sys, &fsys, sizeof(FILETIME));
        memcpy(&user, &fuser, sizeof(FILETIME));

        double percent = (sys.QuadPart - lastSysCPU.QuadPart) +
                        (user.QuadPart - lastUserCPU.QuadPart);
        percent /= (now.QuadPart - lastCPU.QuadPart);
        percent /= numProcessors;
        cpuUsage = static_cast<float>(percent * 100.0);

        lastCPU = now;
        lastUserCPU = user;
        lastSysCPU = sys;
#endif
        // Update GPU VRAM usage (NVIDIA NVX extension)
        updateGPUMemoryInfo();
    }

    void updateGPUMemoryInfo() {
        // Use raw hex values to avoid macro conflicts
        // NVIDIA: GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX = 0x9048
        // NVIDIA: GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX = 0x9049
        // AMD: GL_TEXTURE_FREE_MEMORY_ATI = 0x87FC

        GLint totalMemKB = 0;
        GLint availMemKB = 0;

        // Try NVIDIA extension first
        glGetIntegerv(0x9048, &totalMemKB);
        if (totalMemKB > 0) {
            glGetIntegerv(0x9049, &availMemKB);
            gpuTotalVRAM = static_cast<size_t>(totalMemKB);
            gpuAvailVRAM = static_cast<size_t>(availMemKB);
            size_t usedVRAM = gpuTotalVRAM - gpuAvailVRAM;
            gpuVRAMUsage = (gpuTotalVRAM > 0) ?
                (static_cast<float>(usedVRAM) / static_cast<float>(gpuTotalVRAM) * 100.0f) : 0.0f;
        } else {
            // Try AMD extension
            GLint freeMemATI[4] = {0};
            glGetIntegerv(0x87FC, freeMemATI);
            if (freeMemATI[0] > 0) {
                gpuAvailVRAM = static_cast<size_t>(freeMemATI[0]);
                // AMD doesn't provide total, so we can't calculate percentage accurately
                gpuVRAMUsage = 0.0f;
            }
        }
        // Clear any GL errors from unsupported extensions
        glGetError();
    }

    void setPerformanceStats(int draws, int tris, int culled, float gpuMs, float meshMs) {
        drawCalls = draws;
        triangleCount = tris;
        culledChunks = culled;
        gpuTime = gpuMs;
        meshGenTime = meshMs;
    }

    void setGPUInfo(const std::string& gpu, const std::string& gl) {
        gpuName = gpu;
        openglVersion = gl;
    }

    void setRendererBackend(const std::string& backend) {
        rendererBackend = backend;
    }

    void setRenderStats(int chunks, int subChunks, size_t vramUsed) {
        renderedChunks = chunks;
        renderedSubChunks = subChunks;
        vertexMemory = vramUsed;
    }

    void render() {
        if (!visible || !ui) return;

        float screenWidth = static_cast<float>(ui->windowWidth);

        // Calculate if we need single column mode (narrow screens)
        bool singleColumn = screenWidth < 600.0f;

        float leftX = PADDING;
        // Right panel: position from right edge - moved toward center for long text
        float rightX = screenWidth - PANEL_WIDTH - 160.0f;

        // Ensure right panel doesn't overlap left panel (need gap between them)
        float minRightX = PANEL_WIDTH + PADDING * 3;
        if (!singleColumn && rightX < minRightX) {
            rightX = minRightX;
        }

        float y = PADDING;

        // Left side - Position and World info
        float leftEndY = renderLeftPanel(leftX, y);

        // Right side - Performance and System info
        if (singleColumn) {
            // Single column mode: render right panel below left panel
            renderRightPanel(leftX, leftEndY + SECTION_GAP);
        } else {
            // Two column mode: render side by side
            renderRightPanel(rightX, y);
        }
    }

private:
    // Draw a single line of text with background
    void drawLine(const std::string& text, float x, float y, const glm::vec4& color) {
        float textWidth = text.length() * 10.0f;  // Wide spacing
        ui->drawRect(x, y, textWidth + 16, LINE_HEIGHT, DebugColors::BG);
        ui->drawText(text, x + 6, y + 3, color, 1.0f);
    }

    // Draw a label: value pair with generous spacing
    void drawKeyValue(const std::string& key, const std::string& value,
                      float x, float y, const glm::vec4& valueColor) {
        std::string labelPart = key + ":";
        std::string fullText = labelPart + "  " + value;  // Extra spaces

        // Wide spacing for background
        float textWidth = fullText.length() * 10.0f;

        // Draw background
        ui->drawRect(x, y, textWidth + 20, LINE_HEIGHT, DebugColors::BG);

        // Draw label in gray
        ui->drawText(labelPart, x + 6, y + 3, DebugColors::LABEL, 1.0f);

        // Draw value with BIG gap (12px per label char)
        float labelWidth = labelPart.length() * 12.0f + 10.0f;  // Extra 10px gap
        ui->drawText(value, x + 6 + labelWidth, y + 3, valueColor, 1.0f);
    }

    std::string floatStr(float val, int precision = 2) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(precision) << val;
        return ss.str();
    }

    std::string timeStr(float t) {
        int hour = static_cast<int>(t * 24.0f) % 24;
        int minute = static_cast<int>(t * 24.0f * 60.0f) % 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
        return std::string(buf);
    }

    std::string memoryStr(size_t bytes) {
        if (bytes < 1024) return std::to_string(bytes) + " B";
        if (bytes < 1024 * 1024) return floatStr(bytes / 1024.0f, 1) + " KB";
        if (bytes < 1024ULL * 1024 * 1024) return floatStr(bytes / (1024.0f * 1024.0f), 1) + " MB";
        return floatStr(bytes / (1024.0f * 1024.0f * 1024.0f), 1) + " GB";
    }

    glm::vec4 fpsColor(float fps) {
        if (fps >= 60.0f) return DebugColors::GOOD;
        if (fps >= 30.0f) return DebugColors::WARN;
        return DebugColors::BAD;
    }

    // Returns the Y position after the last line
    float renderLeftPanel(float x, float startY) {
        float y = startY;

        // Title
        drawLine(gameVersion, x, y, DebugColors::TITLE);
        y += LINE_HEIGHT + SECTION_GAP;

        // === Position Section ===
        drawLine("[ Position ]", x, y, DebugColors::TITLE);
        y += LINE_HEIGHT;

        drawKeyValue("X", floatStr(playerPos.x, 3), x, y, DebugColors::POS_X);
        y += LINE_HEIGHT;

        drawKeyValue("Y", floatStr(playerPos.y, 3), x, y, DebugColors::POS_Y);
        y += LINE_HEIGHT;

        drawKeyValue("Z", floatStr(playerPos.z, 3), x, y, DebugColors::POS_Z);
        y += LINE_HEIGHT;

        std::string blockPos = std::to_string(blockX) + ", " +
                               std::to_string(blockY) + ", " +
                               std::to_string(blockZ);
        drawKeyValue("Block", blockPos, x, y, DebugColors::VALUE);
        y += LINE_HEIGHT;

        std::string chunkPos = std::to_string(chunkX) + ", " + std::to_string(chunkZ);
        drawKeyValue("Chunk", chunkPos, x, y, DebugColors::VALUE);
        y += LINE_HEIGHT + SECTION_GAP;

        // === Orientation Section ===
        drawLine("[ Orientation ]", x, y, DebugColors::TITLE);
        y += LINE_HEIGHT;

        drawKeyValue("Facing", facingDirection, x, y, DebugColors::VALUE);
        y += LINE_HEIGHT;

        std::string rotation = floatStr(yaw, 1) + " / " + floatStr(pitch, 1);
        drawKeyValue("Rotation", rotation, x, y, DebugColors::VALUE);
        y += LINE_HEIGHT + SECTION_GAP;

        // === Movement Section ===
        drawLine("[ Movement ]", x, y, DebugColors::TITLE);
        y += LINE_HEIGHT;

        std::string moveMode = isFlying ? "Flying" : (isInWater ? "Swimming" : "Walking");
        glm::vec4 modeColor = isFlying ? DebugColors::TIME :
                              (isInWater ? DebugColors::POS_Z : DebugColors::GOOD);
        drawKeyValue("Mode", moveMode, x, y, modeColor);
        y += LINE_HEIGHT;

        float speed = glm::length(glm::vec2(playerVelocity.x, playerVelocity.z));
        drawKeyValue("Speed", floatStr(speed, 2) + " m/s", x, y, DebugColors::VALUE);
        y += LINE_HEIGHT + SECTION_GAP;

        // === World Section ===
        drawLine("[ World ]", x, y, DebugColors::TITLE);
        y += LINE_HEIGHT;

        bool isDaytime = (timeOfDay >= 0.25f && timeOfDay < 0.75f);
        std::string timeDisplay = timeStr(timeOfDay) + (isDaytime ? " Day" : " Night");
        drawKeyValue("Time", timeDisplay, x, y, DebugColors::TIME);
        y += LINE_HEIGHT;

        drawKeyValue("Biome", biome, x, y, DebugColors::BIOME);
        y += LINE_HEIGHT;

        return y;
    }

    float renderRightPanel(float x, float startY) {
        float y = startY;

        // === Performance Section ===
        drawLine("[ Performance ]", x, y, DebugColors::TITLE);
        y += LINE_HEIGHT;

        std::string fpsStr = std::to_string(static_cast<int>(currentFps)) + " FPS";
        drawKeyValue("FPS", fpsStr, x, y, fpsColor(currentFps));
        y += LINE_HEIGHT;

        drawKeyValue("Frame", floatStr(frameTime, 2) + " ms", x, y, DebugColors::VALUE);
        y += LINE_HEIGHT;

        if (drawCalls > 0) {
            drawKeyValue("Draws", std::to_string(drawCalls), x, y, DebugColors::VALUE);
            y += LINE_HEIGHT;
        }

        if (triangleCount > 0) {
            std::string triStr = triangleCount > 1000000 ?
                floatStr(triangleCount / 1000000.0f, 1) + "M" :
                (triangleCount > 1000 ? floatStr(triangleCount / 1000.0f, 1) + "K" :
                std::to_string(triangleCount));
            drawKeyValue("Tris", triStr, x, y, DebugColors::VALUE);
            y += LINE_HEIGHT;
        }
        y += SECTION_GAP;

        // === Chunks Section ===
        drawLine("[ Chunks ]", x, y, DebugColors::TITLE);
        y += LINE_HEIGHT;

        drawKeyValue("Loaded", std::to_string(loadedChunks), x, y, DebugColors::VALUE);
        y += LINE_HEIGHT;

        drawKeyValue("Meshes", std::to_string(loadedMeshes), x, y, DebugColors::VALUE);
        y += LINE_HEIGHT;

        drawKeyValue("Rendered", std::to_string(renderedChunks), x, y, DebugColors::GOOD);
        y += LINE_HEIGHT;

        drawKeyValue("SubChunks", std::to_string(renderedSubChunks), x, y, DebugColors::VALUE);
        y += LINE_HEIGHT;

        if (culledChunks > 0) {
            drawKeyValue("Culled", std::to_string(culledChunks), x, y, DebugColors::WARN);
            y += LINE_HEIGHT;
        }
        y += SECTION_GAP;

        // === Memory Section ===
        drawLine("[ Memory ]", x, y, DebugColors::TITLE);
        y += LINE_HEIGHT;

        drawKeyValue("VRAM", memoryStr(vertexMemory), x, y, DebugColors::MEMORY);
        y += LINE_HEIGHT;

        // Process RAM usage
        if (processRAM > 0) {
            drawKeyValue("Game RAM", memoryStr(processRAM), x, y, DebugColors::MEMORY);
            y += LINE_HEIGHT;
        }

        // System RAM usage
        if (totalRAM > 0) {
            std::string ramStr = memoryStr(usedRAM) + " / " + memoryStr(totalRAM);
            float ramPercent = (float)usedRAM / (float)totalRAM * 100.0f;
            glm::vec4 ramColor = ramPercent > 90.0f ? DebugColors::BAD :
                                (ramPercent > 75.0f ? DebugColors::WARN : DebugColors::GOOD);
            drawKeyValue("Sys RAM", floatStr(ramPercent, 0) + "%", x, y, ramColor);
            y += LINE_HEIGHT;
        }
        y += SECTION_GAP;

        // === CPU Section ===
        drawLine("[ CPU ]", x, y, DebugColors::TITLE);
        y += LINE_HEIGHT;

        // CPU usage with color coding
        glm::vec4 cpuColor = cpuUsage > 80.0f ? DebugColors::BAD :
                            (cpuUsage > 50.0f ? DebugColors::WARN : DebugColors::GOOD);
        drawKeyValue("Usage", floatStr(cpuUsage, 1) + "%", x, y, cpuColor);
        y += LINE_HEIGHT;

        // Truncate CPU name if too long
        if (!cpuName.empty()) {
            std::string cpuDisplay = cpuName;
            if (cpuDisplay.length() > 20) {
                cpuDisplay = cpuDisplay.substr(0, 17) + "...";
            }
            drawKeyValue("CPU", cpuDisplay, x, y, DebugColors::VALUE);
            y += LINE_HEIGHT;
        }
        y += SECTION_GAP;

        // === GPU Section ===
        drawLine("[ GPU ]", x, y, DebugColors::TITLE);
        y += LINE_HEIGHT;

        // Renderer backend (OpenGL/Vulkan)
        drawKeyValue("Renderer", rendererBackend, x, y, DebugColors::GOOD);
        y += LINE_HEIGHT;

        // GPU VRAM usage with color coding
        if (gpuTotalVRAM > 0) {
            glm::vec4 vramColor = gpuVRAMUsage > 90.0f ? DebugColors::BAD :
                                 (gpuVRAMUsage > 75.0f ? DebugColors::WARN : DebugColors::GOOD);
            std::string vramStr = floatStr(gpuVRAMUsage, 0) + "% (" +
                memoryStr((gpuTotalVRAM - gpuAvailVRAM) * 1024) + ")";
            drawKeyValue("VRAM", vramStr, x, y, vramColor);
            y += LINE_HEIGHT;
        }

        // Truncate GPU name if too long
        std::string gpuDisplay = gpuName;
        if (gpuDisplay.length() > 20) {
            gpuDisplay = gpuDisplay.substr(0, 17) + "...";
        }
        drawKeyValue("GPU", gpuDisplay, x, y, DebugColors::VALUE);
        y += LINE_HEIGHT;

        // Truncate OpenGL version if too long
        std::string glDisplay = openglVersion;
        if (glDisplay.length() > 16) {
            glDisplay = glDisplay.substr(0, 13) + "...";
        }
        drawKeyValue("OpenGL", glDisplay, x, y, DebugColors::VALUE);
        y += LINE_HEIGHT + SECTION_GAP;

        // Controls hint
        drawLine("Press F3 to close", x, y, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
        y += LINE_HEIGHT;

        return y;
    }
};
