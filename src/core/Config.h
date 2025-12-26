#pragma once

// Prevent Windows.h min/max macro conflicts
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <iostream>
#include <thread>
#include <algorithm>
#include <cctype>

// ============================================
// HARDWARE DETECTION
// ============================================

enum class GPUTier {
    UNKNOWN,
    LOW,      // Intel HD, GT 1030, RX 550, etc.
    MID,      // GTX 1060, RX 580, RTX 3050, etc.
    HIGH,     // RTX 3060-3070, RX 6700-6800, etc.
    ULTRA     // RTX 3080+, RTX 4080+, RX 6900+, etc.
};

// Renderer backend selection
enum class RendererType {
    OPENGL,
    VULKAN
};

// Upscaling modes (FSR quality presets)
enum class UpscaleMode {
    NATIVE = 0,       // 1.0x - No upscaling
    QUALITY = 1,      // 1.5x - 67% render scale (FSR Quality)
    BALANCED = 2,     // 1.7x - 59% render scale (FSR Balanced)
    PERFORMANCE = 3,  // 2.0x - 50% render scale (FSR Performance)
    ULTRA_PERF = 4    // 3.0x - 33% render scale (FSR Ultra Performance)
};

struct HardwareInfo {
    // GPU Info
    std::string gpuName = "Unknown";
    std::string gpuVendor = "Unknown";
    int vramMB = 0;
    GPUTier gpuTier = GPUTier::UNKNOWN;

    // Vendor-specific feature support
    bool isNVIDIA = false;
    bool isAMD = false;
    bool isIntel = false;
    bool supportsMeshShaders = false;      // NVIDIA Turing+ (GL_NV_mesh_shader)
    bool supportsBindlessTextures = false; // GL_ARB_bindless_texture
    bool supportsFSR = true;               // FSR 1.0 works on all GPUs

    // CPU Info
    int cpuCores = 4;
    int cpuThreads = 4;

    // Derived settings
    int recommendedRenderDistance = 16;
    int recommendedChunkThreads = 4;
    int recommendedMeshThreads = 4;
    int recommendedShadowRes = 2048;
    int recommendedSSAOSamples = 32;
    bool recommendedVolumetricClouds = true;
    UpscaleMode recommendedUpscaleMode = UpscaleMode::NATIVE;

    // Convert string to lowercase for comparison
    static std::string toLower(const std::string& s) {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return result;
    }

    // Classify GPU based on name and set vendor flags
    void classifyGPU() {
        std::string gpuLower = toLower(gpuName);

        // Reset vendor flags
        isNVIDIA = false;
        isAMD = false;
        isIntel = false;

        // NVIDIA Detection
        if (gpuLower.find("nvidia") != std::string::npos ||
            gpuLower.find("geforce") != std::string::npos) {
            gpuVendor = "NVIDIA";
            isNVIDIA = true;

            // RTX 40 series - ULTRA (Turing+ supports mesh shaders)
            if (gpuLower.find("4090") != std::string::npos ||
                gpuLower.find("4080") != std::string::npos) {
                gpuTier = GPUTier::ULTRA;
                supportsMeshShaders = true;  // Ada Lovelace
                recommendedUpscaleMode = UpscaleMode::NATIVE;
            }
            // RTX 40 mid and RTX 30 high - HIGH
            else if (gpuLower.find("4070") != std::string::npos ||
                     gpuLower.find("4060") != std::string::npos ||
                     gpuLower.find("3090") != std::string::npos ||
                     gpuLower.find("3080") != std::string::npos ||
                     gpuLower.find("3070") != std::string::npos) {
                gpuTier = GPUTier::HIGH;
                supportsMeshShaders = true;  // Ampere/Ada
                recommendedUpscaleMode = UpscaleMode::NATIVE;
            }
            // RTX 30 mid, RTX 20 series - MID (Turing+ has mesh shaders)
            else if (gpuLower.find("3060") != std::string::npos ||
                     gpuLower.find("3050") != std::string::npos ||
                     gpuLower.find("2080") != std::string::npos ||
                     gpuLower.find("2070") != std::string::npos ||
                     gpuLower.find("2060") != std::string::npos) {
                gpuTier = GPUTier::MID;
                supportsMeshShaders = true;  // Turing/Ampere
                recommendedUpscaleMode = UpscaleMode::QUALITY;
            }
            // GTX 10 series, GTX 16 series - MID-LOW (no mesh shaders)
            else if (gpuLower.find("1080") != std::string::npos ||
                     gpuLower.find("1070") != std::string::npos ||
                     gpuLower.find("1660") != std::string::npos ||
                     gpuLower.find("1650") != std::string::npos ||
                     gpuLower.find("1060") != std::string::npos ||
                     gpuLower.find("1050") != std::string::npos) {
                gpuTier = GPUTier::MID;
                supportsMeshShaders = false;  // Pascal/Turing (no RTX)
                recommendedUpscaleMode = UpscaleMode::BALANCED;
            }
            // Older or low-end
            else {
                gpuTier = GPUTier::LOW;
                supportsMeshShaders = false;
                recommendedUpscaleMode = UpscaleMode::PERFORMANCE;
            }
        }
        // AMD Detection
        else if (gpuLower.find("amd") != std::string::npos ||
                 gpuLower.find("radeon") != std::string::npos) {
            gpuVendor = "AMD";
            isAMD = true;

            // RX 7900 series - ULTRA (RDNA 3)
            if (gpuLower.find("7900") != std::string::npos) {
                gpuTier = GPUTier::ULTRA;
                supportsMeshShaders = true;  // RDNA 2+ has mesh shaders
                recommendedUpscaleMode = UpscaleMode::NATIVE;
            }
            // RX 6800-6900, RX 7700-7800 - HIGH (RDNA 2/3)
            else if (gpuLower.find("6900") != std::string::npos ||
                     gpuLower.find("6800") != std::string::npos ||
                     gpuLower.find("7800") != std::string::npos ||
                     gpuLower.find("7700") != std::string::npos) {
                gpuTier = GPUTier::HIGH;
                supportsMeshShaders = true;  // RDNA 2+
                recommendedUpscaleMode = UpscaleMode::NATIVE;
            }
            // RX 6600-6700, RX 5700 - MID
            else if (gpuLower.find("6700") != std::string::npos ||
                     gpuLower.find("6600") != std::string::npos) {
                gpuTier = GPUTier::MID;
                supportsMeshShaders = true;  // RDNA 2
                recommendedUpscaleMode = UpscaleMode::QUALITY;
            }
            else if (gpuLower.find("5700") != std::string::npos ||
                     gpuLower.find("5600") != std::string::npos) {
                gpuTier = GPUTier::MID;
                supportsMeshShaders = false;  // RDNA 1 - no mesh shaders
                recommendedUpscaleMode = UpscaleMode::BALANCED;
            }
            else {
                gpuTier = GPUTier::LOW;
                supportsMeshShaders = false;
                recommendedUpscaleMode = UpscaleMode::PERFORMANCE;
            }
        }
        // Intel Detection
        else if (gpuLower.find("intel") != std::string::npos) {
            gpuVendor = "Intel";
            isIntel = true;

            // Intel Arc - MID to HIGH (Xe-HPG supports mesh shaders)
            if (gpuLower.find("arc") != std::string::npos) {
                if (gpuLower.find("a770") != std::string::npos ||
                    gpuLower.find("a750") != std::string::npos) {
                    gpuTier = GPUTier::MID;
                    supportsMeshShaders = true;  // Xe-HPG
                    recommendedUpscaleMode = UpscaleMode::QUALITY;
                } else {
                    gpuTier = GPUTier::LOW;
                    supportsMeshShaders = true;  // Xe-HPG
                    recommendedUpscaleMode = UpscaleMode::BALANCED;
                }
            }
            // Intel integrated - LOW
            else {
                gpuTier = GPUTier::LOW;
                supportsMeshShaders = false;
                recommendedUpscaleMode = UpscaleMode::ULTRA_PERF;
            }
        }
        else {
            gpuTier = GPUTier::MID;  // Default to MID if unknown
            recommendedUpscaleMode = UpscaleMode::BALANCED;
        }

        // FSR 1.0 works on all GPUs (it's just shader-based)
        supportsFSR = true;
    }

    // Calculate recommended settings based on hardware
    void calculateRecommendations() {
        // CPU-based recommendations
        cpuCores = std::thread::hardware_concurrency();
        if (cpuCores == 0) cpuCores = 4;  // Fallback
        cpuThreads = cpuCores;

        // Thread allocation: more cores = more threads for chunk/mesh work
        recommendedChunkThreads = (std::max)(2, cpuCores / 2);
        recommendedMeshThreads = (std::max)(2, cpuCores / 2);

        // GPU-based recommendations
        switch (gpuTier) {
            case GPUTier::ULTRA:
                recommendedRenderDistance = 32;
                recommendedShadowRes = 4096;
                recommendedSSAOSamples = 64;
                recommendedVolumetricClouds = true;
                break;

            case GPUTier::HIGH:
                recommendedRenderDistance = 24;
                recommendedShadowRes = 2048;
                recommendedSSAOSamples = 32;
                recommendedVolumetricClouds = true;
                break;

            case GPUTier::MID:
                recommendedRenderDistance = 16;
                recommendedShadowRes = 1024;
                recommendedSSAOSamples = 16;
                recommendedVolumetricClouds = true;
                break;

            case GPUTier::LOW:
            default:
                recommendedRenderDistance = 10;
                recommendedShadowRes = 512;
                recommendedSSAOSamples = 8;
                recommendedVolumetricClouds = false;
                break;
        }

        // VRAM-based adjustments
        if (vramMB > 0) {
            if (vramMB >= 12000) {
                // 12GB+ VRAM: can push further
                recommendedRenderDistance = (std::max)(recommendedRenderDistance, 32);
            } else if (vramMB >= 8000) {
                // 8GB VRAM: good for high settings
                recommendedRenderDistance = (std::max)(recommendedRenderDistance, 24);
            } else if (vramMB < 4000) {
                // Under 4GB: reduce settings
                recommendedRenderDistance = (std::min)(recommendedRenderDistance, 12);
                recommendedShadowRes = (std::min)(recommendedShadowRes, 1024);
            }
        }
    }

    // Get tier name as string
    std::string getTierName() const {
        switch (gpuTier) {
            case GPUTier::ULTRA: return "ULTRA";
            case GPUTier::HIGH: return "HIGH";
            case GPUTier::MID: return "MID";
            case GPUTier::LOW: return "LOW";
            default: return "UNKNOWN";
        }
    }

    // Get upscale mode name as string
    std::string getUpscaleModeName() const {
        switch (recommendedUpscaleMode) {
            case UpscaleMode::NATIVE: return "NATIVE (1.0x)";
            case UpscaleMode::QUALITY: return "QUALITY (1.5x)";
            case UpscaleMode::BALANCED: return "BALANCED (1.7x)";
            case UpscaleMode::PERFORMANCE: return "PERFORMANCE (2.0x)";
            case UpscaleMode::ULTRA_PERF: return "ULTRA PERF (3.0x)";
            default: return "NATIVE (1.0x)";
        }
    }

    // Get render scale factor for upscale mode
    static float getRenderScale(UpscaleMode mode) {
        switch (mode) {
            case UpscaleMode::NATIVE: return 1.0f;
            case UpscaleMode::QUALITY: return 1.0f / 1.5f;      // 67%
            case UpscaleMode::BALANCED: return 1.0f / 1.7f;     // 59%
            case UpscaleMode::PERFORMANCE: return 0.5f;          // 50%
            case UpscaleMode::ULTRA_PERF: return 1.0f / 3.0f;   // 33%
            default: return 1.0f;
        }
    }

    void print() const {
        std::cout << "\n=== Hardware Detection ===" << std::endl;
        std::cout << "GPU: " << gpuName << std::endl;
        std::cout << "Vendor: " << gpuVendor << std::endl;
        std::cout << "VRAM: " << (vramMB > 0 ? std::to_string(vramMB) + " MB" : "Unknown") << std::endl;
        std::cout << "Performance Tier: " << getTierName() << std::endl;
        std::cout << "CPU Threads: " << cpuThreads << std::endl;
        std::cout << "\nVendor Features:" << std::endl;
        std::cout << "  NVIDIA: " << (isNVIDIA ? "Yes" : "No") << std::endl;
        std::cout << "  AMD: " << (isAMD ? "Yes" : "No") << std::endl;
        std::cout << "  Intel: " << (isIntel ? "Yes" : "No") << std::endl;
        std::cout << "  Mesh Shaders: " << (supportsMeshShaders ? "Yes" : "No") << std::endl;
        std::cout << "  FSR Support: " << (supportsFSR ? "Yes" : "No") << std::endl;
        std::cout << "\nRecommended Settings:" << std::endl;
        std::cout << "  Render Distance: " << recommendedRenderDistance << std::endl;
        std::cout << "  Shadow Resolution: " << recommendedShadowRes << std::endl;
        std::cout << "  SSAO Samples: " << recommendedSSAOSamples << std::endl;
        std::cout << "  Chunk Threads: " << recommendedChunkThreads << std::endl;
        std::cout << "  Mesh Threads: " << recommendedMeshThreads << std::endl;
        std::cout << "  Volumetric Clouds: " << (recommendedVolumetricClouds ? "Yes" : "No") << std::endl;
        std::cout << "  Upscale Mode: " << getUpscaleModeName() << std::endl;
        std::cout << "==========================\n" << std::endl;
    }
};

// Global hardware info
inline HardwareInfo g_hardware;

struct GameConfig {
    // Renderer Selection
    RendererType renderer = RendererType::OPENGL;  // OpenGL or Vulkan

    // Graphics
    int windowWidth = 1280;
    int windowHeight = 720;
    bool fullscreen = false;
    bool vsync = true;
    int fov = 70;
    int renderDistance = 24;
    int maxChunksPerFrame = 128;   // Increased for better hardware utilization
    int maxMeshesPerFrame = 64;    // Increased for better hardware utilization
    float fogDensity = 0.00015f;

    // Performance
    bool useHighPerformanceGPU = true;  // Prefer discrete GPU
    int chunkCacheSize = 500;           // Max chunks in memory
    int chunkThreads = 0;               // 0 = auto-detect based on CPU
    int meshThreads = 0;                // 0 = auto-detect based on CPU
    bool autoTuneOnStartup = true;      // Auto-configure settings on first run

    // Quality Settings
    bool enableSSAO = true;             // Screen-space ambient occlusion
    int ssaoSamples = 32;               // SSAO kernel size (16, 32, 64)
    float ssaoRadius = 1.5f;            // SSAO sample radius
    float ssaoBias = 0.03f;             // SSAO depth bias

    bool enableShadows = true;          // Shadow mapping
    int shadowResolution = 2048;        // Shadow map resolution per cascade
    int shadowCascades = 3;             // Number of shadow cascades (1-4)

    bool enableHiZCulling = true;       // Hi-Z occlusion culling
    bool enableDeferredRendering = true; // Use deferred rendering pipeline

    bool showPerformanceStats = true;   // Show FPS and timing overlay

    // FSR / Upscaling
    UpscaleMode upscaleMode = UpscaleMode::NATIVE;  // FSR upscaling preset
    bool enableFSR = false;                          // Enable FSR upscaling
    float fsrSharpness = 0.5f;                       // FSR sharpening strength (0-2)

    // Gameplay
    float mouseSensitivity = 0.1f;
    bool invertY = false;
    float dayLength = 120.0f;  // Day cycle length in seconds

    // Audio (for future)
    float masterVolume = 1.0f;
    float musicVolume = 0.5f;
    float sfxVolume = 1.0f;

    // Apply hardware-based auto-tuning
    void autoTune() {
        std::cout << "Auto-tuning settings based on detected hardware..." << std::endl;

        // Apply GPU-tier recommendations
        renderDistance = g_hardware.recommendedRenderDistance;
        shadowResolution = g_hardware.recommendedShadowRes;
        ssaoSamples = g_hardware.recommendedSSAOSamples;

        // Apply CPU-based thread counts
        if (chunkThreads == 0) chunkThreads = g_hardware.recommendedChunkThreads;
        if (meshThreads == 0) meshThreads = g_hardware.recommendedMeshThreads;

        // Scale other settings based on tier
        switch (g_hardware.gpuTier) {
            case GPUTier::ULTRA:
                maxChunksPerFrame = 256;   // Maximize throughput
                maxMeshesPerFrame = 128;
                chunkCacheSize = 8000;
                enableSSAO = true;
                enableShadows = true;
                shadowCascades = 4;
                break;

            case GPUTier::HIGH:
                maxChunksPerFrame = 192;
                maxMeshesPerFrame = 96;
                chunkCacheSize = 4000;
                enableSSAO = true;
                enableShadows = true;
                shadowCascades = 3;
                break;

            case GPUTier::MID:
                maxChunksPerFrame = 128;
                maxMeshesPerFrame = 64;
                chunkCacheSize = 2000;
                enableSSAO = true;
                enableShadows = true;
                shadowCascades = 2;
                break;

            case GPUTier::LOW:
            default:
                maxChunksPerFrame = 64;
                maxMeshesPerFrame = 32;
                chunkCacheSize = 1000;
                enableSSAO = false;
                enableShadows = true;
                shadowCascades = 1;
                shadowResolution = 512;
                break;
        }

        // Adjust fog based on render distance
        fogDensity = 0.008f / static_cast<float>(renderDistance);

        // Apply FSR recommendations based on GPU tier
        upscaleMode = g_hardware.recommendedUpscaleMode;
        enableFSR = (upscaleMode != UpscaleMode::NATIVE);
        fsrSharpness = 0.5f;  // Default sharpness

        std::cout << "Auto-tune complete. Settings optimized for "
                  << g_hardware.getTierName() << " tier hardware." << std::endl;
        if (enableFSR) {
            std::cout << "FSR upscaling enabled: " << g_hardware.getUpscaleModeName() << std::endl;
        }
    }

    // Save config to file
    bool save(const std::string& filename = "settings.cfg") {
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Failed to save config: " << filename << std::endl;
            return false;
        }

        file << "# Voxel Engine Settings\n\n";

        file << "[Renderer]\n";
        file << "renderer=" << (renderer == RendererType::VULKAN ? "vulkan" : "opengl") << "\n";

        file << "\n[Graphics]\n";
        file << "windowWidth=" << windowWidth << "\n";
        file << "windowHeight=" << windowHeight << "\n";
        file << "fullscreen=" << (fullscreen ? "true" : "false") << "\n";
        file << "vsync=" << (vsync ? "true" : "false") << "\n";
        file << "fov=" << fov << "\n";
        file << "renderDistance=" << renderDistance << "\n";
        file << "maxChunksPerFrame=" << maxChunksPerFrame << "\n";
        file << "maxMeshesPerFrame=" << maxMeshesPerFrame << "\n";
        file << "fogDensity=" << fogDensity << "\n";

        file << "\n[Performance]\n";
        file << "useHighPerformanceGPU=" << (useHighPerformanceGPU ? "true" : "false") << "\n";
        file << "chunkCacheSize=" << chunkCacheSize << "\n";
        file << "chunkThreads=" << chunkThreads << "\n";
        file << "meshThreads=" << meshThreads << "\n";
        file << "autoTuneOnStartup=" << (autoTuneOnStartup ? "true" : "false") << "\n";

        file << "\n[Quality]\n";
        file << "enableSSAO=" << (enableSSAO ? "true" : "false") << "\n";
        file << "ssaoSamples=" << ssaoSamples << "\n";
        file << "ssaoRadius=" << ssaoRadius << "\n";
        file << "ssaoBias=" << ssaoBias << "\n";
        file << "enableShadows=" << (enableShadows ? "true" : "false") << "\n";
        file << "shadowResolution=" << shadowResolution << "\n";
        file << "shadowCascades=" << shadowCascades << "\n";
        file << "enableHiZCulling=" << (enableHiZCulling ? "true" : "false") << "\n";
        file << "enableDeferredRendering=" << (enableDeferredRendering ? "true" : "false") << "\n";
        file << "showPerformanceStats=" << (showPerformanceStats ? "true" : "false") << "\n";

        file << "\n[Upscaling]\n";
        file << "enableFSR=" << (enableFSR ? "true" : "false") << "\n";
        file << "upscaleMode=" << static_cast<int>(upscaleMode) << "\n";
        file << "fsrSharpness=" << fsrSharpness << "\n";

        file << "\n[Gameplay]\n";
        file << "mouseSensitivity=" << mouseSensitivity << "\n";
        file << "invertY=" << (invertY ? "true" : "false") << "\n";
        file << "dayLength=" << dayLength << "\n";

        file << "\n[Audio]\n";
        file << "masterVolume=" << masterVolume << "\n";
        file << "musicVolume=" << musicVolume << "\n";
        file << "sfxVolume=" << sfxVolume << "\n";

        file.close();
        std::cout << "Config saved to " << filename << std::endl;
        return true;
    }

    // Load config from file
    bool load(const std::string& filename = "settings.cfg") {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cout << "No config file found, using defaults" << std::endl;
            return false;
        }

        std::string line;
        while (std::getline(file, line)) {
            // Skip comments and empty lines
            if (line.empty() || line[0] == '#' || line[0] == '[') continue;

            // Parse key=value
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);

            // Trim whitespace
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            while (!value.empty() && (value[0] == ' ' || value[0] == '\t')) value = value.substr(1);

            // Apply settings
            // Renderer
            if (key == "renderer") renderer = (value == "vulkan") ? RendererType::VULKAN : RendererType::OPENGL;
            // Graphics
            else if (key == "windowWidth") windowWidth = std::stoi(value);
            else if (key == "windowHeight") windowHeight = std::stoi(value);
            else if (key == "fullscreen") fullscreen = (value == "true");
            else if (key == "vsync") vsync = (value == "true");
            else if (key == "fov") fov = std::stoi(value);
            else if (key == "renderDistance") renderDistance = std::stoi(value);
            else if (key == "maxChunksPerFrame") maxChunksPerFrame = std::stoi(value);
            else if (key == "maxMeshesPerFrame") maxMeshesPerFrame = std::stoi(value);
            else if (key == "fogDensity") fogDensity = std::stof(value);
            else if (key == "useHighPerformanceGPU") useHighPerformanceGPU = (value == "true");
            else if (key == "chunkCacheSize") chunkCacheSize = std::stoi(value);
            else if (key == "chunkThreads") chunkThreads = std::stoi(value);
            else if (key == "meshThreads") meshThreads = std::stoi(value);
            else if (key == "autoTuneOnStartup") autoTuneOnStartup = (value == "true");
            // Quality settings
            else if (key == "enableSSAO") enableSSAO = (value == "true");
            else if (key == "ssaoSamples") ssaoSamples = std::stoi(value);
            else if (key == "ssaoRadius") ssaoRadius = std::stof(value);
            else if (key == "ssaoBias") ssaoBias = std::stof(value);
            else if (key == "enableShadows") enableShadows = (value == "true");
            else if (key == "shadowResolution") shadowResolution = std::stoi(value);
            else if (key == "shadowCascades") shadowCascades = std::stoi(value);
            else if (key == "enableHiZCulling") enableHiZCulling = (value == "true");
            else if (key == "enableDeferredRendering") enableDeferredRendering = (value == "true");
            else if (key == "showPerformanceStats") showPerformanceStats = (value == "true");
            // Upscaling settings
            else if (key == "enableFSR") enableFSR = (value == "true");
            else if (key == "upscaleMode") upscaleMode = static_cast<UpscaleMode>(std::stoi(value));
            else if (key == "fsrSharpness") fsrSharpness = std::stof(value);
            else if (key == "mouseSensitivity") mouseSensitivity = std::stof(value);
            else if (key == "invertY") invertY = (value == "true");
            else if (key == "dayLength") dayLength = std::stof(value);
            else if (key == "masterVolume") masterVolume = std::stof(value);
            else if (key == "musicVolume") musicVolume = std::stof(value);
            else if (key == "sfxVolume") sfxVolume = std::stof(value);
        }

        file.close();
        std::cout << "Config loaded from " << filename << std::endl;
        return true;
    }
};

// Global config instance
inline GameConfig g_config;
