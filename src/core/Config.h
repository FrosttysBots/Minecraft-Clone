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

// Title screen background source
enum class TitleScreenSource {
    RANDOM = 0,       // Random seed each launch
    CUSTOM_SEED = 1,  // User-specified seed
    SAVED_WORLD = 2   // Load from saved world
};

// Title screen world settings
struct TitleScreenSettings {
    TitleScreenSource sourceMode = TitleScreenSource::RANDOM;
    std::string customSeed = "";
    std::string savedWorldPath = "";
    int renderDistance = 8;
    float continentScale = 25.0f;
    float mountainScale = 50.0f;
    float detailScale = 5.0f;
    int generationType = 0;
};

// Anti-Aliasing modes
enum class AntiAliasMode {
    NONE = 0,         // No anti-aliasing
    FXAA = 1,         // Fast approximate AA (post-process)
    MSAA_2X = 2,      // 2x Multisample AA
    MSAA_4X = 3,      // 4x Multisample AA
    MSAA_8X = 4,      // 8x Multisample AA
    TAA = 5           // Temporal AA
};

// Texture quality levels
enum class TextureQuality {
    LOW = 0,          // 1/4 resolution
    MEDIUM = 1,       // 1/2 resolution
    HIGH = 2,         // Full resolution
    ULTRA = 3         // Full resolution + high-quality filtering
};

// Graphics preset levels
enum class GraphicsPreset {
    LOW = 0,
    MEDIUM = 1,
    HIGH = 2,
    ULTRA = 3,
    CUSTOM = 4        // User-customized settings
};

// Ambient Occlusion type
enum class AOType {
    SSAO = 0,         // Standard Screen-Space Ambient Occlusion
    HBAO = 1          // Horizon-Based Ambient Occlusion (better quality, slightly slower)
};

// Ambient Occlusion quality
enum class AOQuality {
    OFF = 0,
    LOW = 1,          // 8 samples / 4 directions
    MEDIUM = 2,       // 16 samples / 6 directions
    HIGH = 3,         // 32 samples / 8 directions
    ULTRA = 4         // 64 samples / 12 directions
};

// Shadow quality levels
enum class ShadowQuality {
    OFF = 0,
    LOW = 1,          // 512 resolution, 1 cascade
    MEDIUM = 2,       // 1024 resolution, 2 cascades
    HIGH = 3,         // 2048 resolution, 3 cascades
    ULTRA = 4         // 4096 resolution, 4 cascades
};

// Cloud quality levels (affects render steps)
enum class CloudQuality {
    VERY_LOW = 0,     // 4 steps
    LOW = 1,          // 8 steps
    MEDIUM = 2,       // 12 steps
    HIGH = 3          // 16 steps
};

// Cloud rendering style
enum class CloudStyle {
    SIMPLE = 0,       // Simple 3D puffy clouds
    VOLUMETRIC = 1    // Full volumetric ray-marched clouds [Experimental]
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

    // Title Screen Settings
    TitleScreenSettings titleScreen;

    // Graphics
    int windowWidth = 1280;
    int windowHeight = 720;
    bool fullscreen = false;
    bool vsync = true;
    int fov = 70;
    int renderDistance = 16;
    int maxChunksPerFrame = 8;     // Reasonable default for new players
    int maxMeshesPerFrame = 8;     // Reasonable default for new players
    float fogDensity = 0.00015f;

    // Performance
    bool useHighPerformanceGPU = true;  // Prefer discrete GPU
    int chunkCacheSize = 500;           // Max chunks in memory
    int chunkThreads = 0;               // 0 = auto-detect based on CPU
    int meshThreads = 0;                // 0 = auto-detect based on CPU
    bool autoTuneOnStartup = true;      // Auto-configure settings on first run

    // Graphics Preset
    GraphicsPreset graphicsPreset = GraphicsPreset::HIGH;

    // Anti-Aliasing
    AntiAliasMode antiAliasing = AntiAliasMode::FXAA;

    // Texture Quality
    TextureQuality textureQuality = TextureQuality::HIGH;
    int anisotropicFiltering = 8;       // 1, 2, 4, 8, or 16

    // Quality Settings
    bool enableSSAO = true;             // Screen-space ambient occlusion
    AOType aoType = AOType::HBAO;       // AO algorithm (SSAO or HBAO)
    int ssaoSamples = 16;               // SSAO kernel size (8, 16, 32) - reduced default from 32
    float ssaoRadius = 1.5f;            // SSAO sample radius
    float ssaoBias = 0.03f;             // SSAO depth bias
    float ssaoScale = 0.5f;             // SSAO resolution scale (0.5 = half-res, 1.0 = full)
    float hbaoIntensity = 1.5f;         // HBAO intensity multiplier
    int hbaoDirections = 8;             // HBAO ray directions (4, 6, 8, 12)
    int hbaoSteps = 4;                  // HBAO steps per direction (2, 4, 6, 8)
    AOQuality aoQuality = AOQuality::MEDIUM;

    bool enableShadows = true;          // Shadow mapping
    int shadowResolution = 2048;        // Shadow map resolution per cascade
    int shadowCascades = 3;             // Number of shadow cascades (1-4)
    ShadowQuality shadowQuality = ShadowQuality::HIGH;

    bool enableHiZCulling = true;       // Hi-Z occlusion culling
    bool enableDeferredRendering = false; // Deferred rendering disabled - forward only

    bool showPerformanceStats = true;   // Show FPS and timing overlay

    // Post-Processing Effects
    bool enableBloom = true;            // Bloom/glow effect
    float bloomIntensity = 0.5f;        // Bloom strength (0.0 - 2.0)
    float bloomThreshold = 1.0f;        // Brightness threshold for bloom

    bool enableMotionBlur = false;      // Motion blur effect
    float motionBlurStrength = 0.5f;    // Motion blur intensity (0.0 - 1.0)

    // Cloud settings
    bool enableClouds = true;           // Enable cloud rendering
    CloudStyle cloudStyle = CloudStyle::SIMPLE;  // Simple or Volumetric clouds
    CloudQuality cloudQuality = CloudQuality::MEDIUM;  // Cloud render quality

    bool enableWaterAnimation = true;   // Water surface animation (disable for performance)
    bool enableBatchedRendering = true; // Sodium-style batched indirect rendering

    bool enableVignette = true;         // Screen edge darkening
    float vignetteIntensity = 0.3f;     // Vignette strength

    bool enableColorGrading = true;     // Color correction
    float gamma = 2.2f;                 // Display gamma
    float exposure = 1.0f;              // Exposure adjustment
    float saturation = 1.0f;            // Color saturation

    // FSR / Upscaling
    UpscaleMode upscaleMode = UpscaleMode::NATIVE;  // FSR upscaling preset
    bool enableFSR = false;                          // Enable FSR upscaling
    float fsrSharpness = 0.5f;                       // FSR sharpening strength (0-2)

    // Gameplay
    float mouseSensitivity = 0.1f;
    bool invertY = false;
    float dayLength = 1440.0f;  // Day cycle: 24 real minutes = 24 game hours (1 min = 1 hour)

    // Audio (for future)
    float masterVolume = 1.0f;
    float musicVolume = 0.5f;
    float sfxVolume = 1.0f;

    // Apply a graphics preset
    void applyPreset(GraphicsPreset preset) {
        graphicsPreset = preset;

        switch (preset) {
            case GraphicsPreset::LOW:
                renderDistance = 10;
                antiAliasing = AntiAliasMode::NONE;
                textureQuality = TextureQuality::LOW;
                anisotropicFiltering = 1;
                enableSSAO = false;
                aoQuality = AOQuality::OFF;
                enableShadows = false;  // Disable shadows on Low
                shadowQuality = ShadowQuality::OFF;
                shadowResolution = 512;
                shadowCascades = 1;
                ssaoSamples = 8;
                ssaoScale = 0.25f;  // Quarter resolution (if enabled)
                enableBloom = false;
                enableMotionBlur = false;
                enableVignette = false;
                enableColorGrading = false;  // Disable for performance
                enableFSR = true;
                upscaleMode = UpscaleMode::PERFORMANCE;
                maxChunksPerFrame = 4;   // Low values for weak hardware
                maxMeshesPerFrame = 4;
                chunkCacheSize = 500;    // Reduce memory usage
                break;

            case GraphicsPreset::MEDIUM:
                renderDistance = 16;
                antiAliasing = AntiAliasMode::FXAA;
                textureQuality = TextureQuality::MEDIUM;
                anisotropicFiltering = 4;
                enableSSAO = true;
                aoQuality = AOQuality::LOW;
                enableShadows = true;
                shadowQuality = ShadowQuality::MEDIUM;
                shadowResolution = 1024;
                shadowCascades = 2;
                ssaoSamples = 16;
                ssaoScale = 0.5f;  // Half resolution for performance
                enableBloom = true;
                bloomIntensity = 0.3f;
                enableMotionBlur = false;
                enableVignette = true;
                vignetteIntensity = 0.2f;
                enableColorGrading = true;
                enableFSR = true;
                upscaleMode = UpscaleMode::BALANCED;
                maxChunksPerFrame = 8;   // Reasonable value
                maxMeshesPerFrame = 8;
                chunkCacheSize = 1000;
                break;

            case GraphicsPreset::HIGH:
                renderDistance = 24;
                antiAliasing = AntiAliasMode::FXAA;
                textureQuality = TextureQuality::HIGH;
                anisotropicFiltering = 8;
                enableSSAO = true;
                aoQuality = AOQuality::MEDIUM;
                enableShadows = true;
                shadowQuality = ShadowQuality::HIGH;
                shadowResolution = 2048;
                shadowCascades = 3;
                ssaoSamples = 16;  // Reduced from 32
                ssaoScale = 0.75f;  // 3/4 resolution for balance
                enableBloom = true;
                bloomIntensity = 0.5f;
                enableMotionBlur = false;
                enableVignette = true;
                vignetteIntensity = 0.3f;
                enableColorGrading = true;
                enableFSR = false;
                upscaleMode = UpscaleMode::NATIVE;
                maxChunksPerFrame = 16;   // Higher for good hardware
                maxMeshesPerFrame = 16;
                chunkCacheSize = 2000;
                break;

            case GraphicsPreset::ULTRA:
                renderDistance = 32;
                antiAliasing = AntiAliasMode::TAA;
                textureQuality = TextureQuality::ULTRA;
                anisotropicFiltering = 16;
                enableSSAO = true;
                aoQuality = AOQuality::ULTRA;
                enableShadows = true;
                shadowQuality = ShadowQuality::ULTRA;
                shadowResolution = 4096;
                shadowCascades = 4;
                ssaoSamples = 32;  // Reduced from 64
                ssaoScale = 1.0f;  // Full resolution for ULTRA
                enableBloom = true;
                bloomIntensity = 0.5f;
                enableMotionBlur = true;
                motionBlurStrength = 0.3f;
                enableVignette = true;
                vignetteIntensity = 0.3f;
                enableColorGrading = true;
                enableFSR = false;
                upscaleMode = UpscaleMode::NATIVE;
                maxChunksPerFrame = 32;   // Maximum for top-tier hardware
                maxMeshesPerFrame = 32;
                chunkCacheSize = 4000;
                break;

            case GraphicsPreset::CUSTOM:
                // Don't change anything for custom
                break;
        }

        // Update fog based on render distance
        fogDensity = 0.008f / static_cast<float>(renderDistance);

        std::cout << "Applied graphics preset: " << getPresetName(preset) << std::endl;
    }

    // Get preset name as string
    static std::string getPresetName(GraphicsPreset preset) {
        switch (preset) {
            case GraphicsPreset::LOW: return "Low";
            case GraphicsPreset::MEDIUM: return "Medium";
            case GraphicsPreset::HIGH: return "High";
            case GraphicsPreset::ULTRA: return "Ultra";
            case GraphicsPreset::CUSTOM: return "Custom";
            default: return "Unknown";
        }
    }

    // Get anti-alias mode name
    static std::string getAAModeName(AntiAliasMode mode) {
        switch (mode) {
            case AntiAliasMode::NONE: return "Off";
            case AntiAliasMode::FXAA: return "FXAA";
            case AntiAliasMode::MSAA_2X: return "MSAA 2x";
            case AntiAliasMode::MSAA_4X: return "MSAA 4x";
            case AntiAliasMode::MSAA_8X: return "MSAA 8x";
            case AntiAliasMode::TAA: return "TAA";
            default: return "Unknown";
        }
    }

    // Get texture quality name
    static std::string getTextureQualityName(TextureQuality quality) {
        switch (quality) {
            case TextureQuality::LOW: return "Low";
            case TextureQuality::MEDIUM: return "Medium";
            case TextureQuality::HIGH: return "High";
            case TextureQuality::ULTRA: return "Ultra";
            default: return "Unknown";
        }
    }

    // Get quality level name (for AO/Shadow)
    static std::string getQualityLevelName(int level) {
        switch (level) {
            case 0: return "Off";
            case 1: return "Low";
            case 2: return "Medium";
            case 3: return "High";
            case 4: return "Ultra";
            default: return "Unknown";
        }
    }

    // Apply hardware-based auto-tuning
    void autoTune() {
        std::cout << "Auto-tuning settings based on detected hardware..." << std::endl;

        // Determine appropriate preset based on GPU tier
        GraphicsPreset recommendedPreset;
        switch (g_hardware.gpuTier) {
            case GPUTier::ULTRA:
                recommendedPreset = GraphicsPreset::ULTRA;
                break;
            case GPUTier::HIGH:
                recommendedPreset = GraphicsPreset::HIGH;
                break;
            case GPUTier::MID:
                recommendedPreset = GraphicsPreset::MEDIUM;
                break;
            case GPUTier::LOW:
            default:
                recommendedPreset = GraphicsPreset::LOW;
                break;
        }

        // Apply the recommended preset
        applyPreset(recommendedPreset);

        // Apply CPU-based thread counts
        if (chunkThreads == 0) chunkThreads = g_hardware.recommendedChunkThreads;
        if (meshThreads == 0) meshThreads = g_hardware.recommendedMeshThreads;

        // Override FSR based on hardware recommendation if needed
        if (g_hardware.recommendedUpscaleMode != UpscaleMode::NATIVE) {
            upscaleMode = g_hardware.recommendedUpscaleMode;
            enableFSR = true;
        }

        std::cout << "Auto-tune complete. Settings optimized for "
                  << g_hardware.getTierName() << " tier hardware." << std::endl;
        std::cout << "Applied preset: " << getPresetName(graphicsPreset) << std::endl;
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
        file << "graphicsPreset=" << static_cast<int>(graphicsPreset) << "\n";
        file << "antiAliasing=" << static_cast<int>(antiAliasing) << "\n";
        file << "textureQuality=" << static_cast<int>(textureQuality) << "\n";
        file << "anisotropicFiltering=" << anisotropicFiltering << "\n";

        file << "\n[Performance]\n";
        file << "useHighPerformanceGPU=" << (useHighPerformanceGPU ? "true" : "false") << "\n";
        file << "chunkCacheSize=" << chunkCacheSize << "\n";
        file << "chunkThreads=" << chunkThreads << "\n";
        file << "meshThreads=" << meshThreads << "\n";
        file << "autoTuneOnStartup=" << (autoTuneOnStartup ? "true" : "false") << "\n";

        file << "\n[Quality]\n";
        file << "enableSSAO=" << (enableSSAO ? "true" : "false") << "\n";
        file << "aoType=" << static_cast<int>(aoType) << "\n";
        file << "ssaoSamples=" << ssaoSamples << "\n";
        file << "ssaoRadius=" << ssaoRadius << "\n";
        file << "ssaoBias=" << ssaoBias << "\n";
        file << "ssaoScale=" << ssaoScale << "\n";
        file << "hbaoIntensity=" << hbaoIntensity << "\n";
        file << "hbaoDirections=" << hbaoDirections << "\n";
        file << "hbaoSteps=" << hbaoSteps << "\n";
        file << "aoQuality=" << static_cast<int>(aoQuality) << "\n";
        file << "enableShadows=" << (enableShadows ? "true" : "false") << "\n";
        file << "shadowResolution=" << shadowResolution << "\n";
        file << "shadowCascades=" << shadowCascades << "\n";
        file << "shadowQuality=" << static_cast<int>(shadowQuality) << "\n";
        file << "enableHiZCulling=" << (enableHiZCulling ? "true" : "false") << "\n";
        file << "enableDeferredRendering=" << (enableDeferredRendering ? "true" : "false") << "\n";
        file << "showPerformanceStats=" << (showPerformanceStats ? "true" : "false") << "\n";

        file << "\n[PostProcessing]\n";
        file << "enableBloom=" << (enableBloom ? "true" : "false") << "\n";
        file << "bloomIntensity=" << bloomIntensity << "\n";
        file << "bloomThreshold=" << bloomThreshold << "\n";
        file << "enableMotionBlur=" << (enableMotionBlur ? "true" : "false") << "\n";
        file << "motionBlurStrength=" << motionBlurStrength << "\n";
        file << "enableClouds=" << (enableClouds ? "true" : "false") << "\n";
        file << "cloudStyle=" << static_cast<int>(cloudStyle) << "\n";
        file << "cloudQuality=" << static_cast<int>(cloudQuality) << "\n";
        file << "enableWaterAnimation=" << (enableWaterAnimation ? "true" : "false") << "\n";
        file << "enableBatchedRendering=" << (enableBatchedRendering ? "true" : "false") << "\n";
        file << "enableVignette=" << (enableVignette ? "true" : "false") << "\n";
        file << "vignetteIntensity=" << vignetteIntensity << "\n";
        file << "enableColorGrading=" << (enableColorGrading ? "true" : "false") << "\n";
        file << "gamma=" << gamma << "\n";
        file << "exposure=" << exposure << "\n";
        file << "saturation=" << saturation << "\n";

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
            else if (key == "graphicsPreset") graphicsPreset = static_cast<GraphicsPreset>(std::stoi(value));
            else if (key == "antiAliasing") antiAliasing = static_cast<AntiAliasMode>(std::stoi(value));
            else if (key == "textureQuality") textureQuality = static_cast<TextureQuality>(std::stoi(value));
            else if (key == "anisotropicFiltering") anisotropicFiltering = std::stoi(value);
            // Performance
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
            else if (key == "ssaoScale") ssaoScale = std::stof(value);
            else if (key == "aoQuality") aoQuality = static_cast<AOQuality>(std::stoi(value));
            else if (key == "aoType") aoType = static_cast<AOType>(std::stoi(value));
            else if (key == "hbaoIntensity") hbaoIntensity = std::stof(value);
            else if (key == "hbaoDirections") hbaoDirections = std::stoi(value);
            else if (key == "hbaoSteps") hbaoSteps = std::stoi(value);
            else if (key == "enableShadows") enableShadows = (value == "true");
            else if (key == "shadowResolution") shadowResolution = std::stoi(value);
            else if (key == "shadowCascades") shadowCascades = std::stoi(value);
            else if (key == "shadowQuality") shadowQuality = static_cast<ShadowQuality>(std::stoi(value));
            else if (key == "enableHiZCulling") enableHiZCulling = (value == "true");
            else if (key == "enableDeferredRendering") enableDeferredRendering = (value == "true");
            else if (key == "showPerformanceStats") showPerformanceStats = (value == "true");
            // Post-processing settings
            else if (key == "enableBloom") enableBloom = (value == "true");
            else if (key == "bloomIntensity") bloomIntensity = std::stof(value);
            else if (key == "bloomThreshold") bloomThreshold = std::stof(value);
            else if (key == "enableMotionBlur") enableMotionBlur = (value == "true");
            else if (key == "motionBlurStrength") motionBlurStrength = std::stof(value);
            else if (key == "enableClouds") enableClouds = (value == "true");
            else if (key == "cloudStyle") cloudStyle = static_cast<CloudStyle>(std::stoi(value));
            else if (key == "cloudQuality") cloudQuality = static_cast<CloudQuality>(std::stoi(value));
            else if (key == "enableWaterAnimation") enableWaterAnimation = (value == "true");
            else if (key == "enableBatchedRendering") enableBatchedRendering = (value == "true");
            else if (key == "enableVignette") enableVignette = (value == "true");
            else if (key == "vignetteIntensity") vignetteIntensity = std::stof(value);
            else if (key == "enableColorGrading") enableColorGrading = (value == "true");
            else if (key == "gamma") gamma = std::stof(value);
            else if (key == "exposure") exposure = std::stof(value);
            else if (key == "saturation") saturation = std::stof(value);
            // Upscaling settings
            else if (key == "enableFSR") enableFSR = (value == "true");
            else if (key == "upscaleMode") upscaleMode = static_cast<UpscaleMode>(std::stoi(value));
            else if (key == "fsrSharpness") fsrSharpness = std::stof(value);
            // Gameplay
            else if (key == "mouseSensitivity") mouseSensitivity = std::stof(value);
            else if (key == "invertY") invertY = (value == "true");
            else if (key == "dayLength") dayLength = std::stof(value);
            // Audio
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
