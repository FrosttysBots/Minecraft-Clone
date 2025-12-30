#pragma once

// Texture Pack Loader
// Loads albedo and normal map textures from a texture pack folder
// Falls back to procedural generation if textures not found

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <filesystem>
#include <iostream>
#include <unordered_map>

// stb_image for loading PNGs
#include "stb_image.h"

// Texture atlas constants
constexpr int TEX_SIZE = 16;      // Each texture is 16x16
constexpr int TEX_ATLAS_DIM = 16; // 16x16 grid = 256 textures max
constexpr int TEX_ATLAS_PX = TEX_SIZE * TEX_ATLAS_DIM; // 256x256 atlas

// Block texture slot mapping (must match Block.h order)
struct BlockTextureSlots {
    static constexpr int STONE = 0;
    static constexpr int DIRT = 1;
    static constexpr int GRASS_TOP = 2;
    static constexpr int GRASS_SIDE = 3;
    static constexpr int COBBLESTONE = 4;
    static constexpr int PLANKS = 5;
    static constexpr int LOG_SIDE = 6;
    static constexpr int LOG_TOP = 7;
    static constexpr int LEAVES = 8;
    static constexpr int SAND = 9;
    static constexpr int GRAVEL = 10;
    static constexpr int WATER = 11;
    static constexpr int BEDROCK = 12;
    static constexpr int COAL_ORE = 13;
    static constexpr int IRON_ORE = 14;
    static constexpr int GOLD_ORE = 15;
    static constexpr int DIAMOND_ORE = 16;  // Row 1, col 0
    static constexpr int GLASS = 17;
    static constexpr int BRICK = 18;
    static constexpr int SNOW = 19;
    static constexpr int CACTUS_SIDE = 20;
    static constexpr int CACTUS_TOP = 21;
    static constexpr int GLOWSTONE = 22;
    static constexpr int LAVA = 23;
};

// Texture names for file loading
static const std::unordered_map<int, std::string> TEXTURE_NAMES = {
    {BlockTextureSlots::STONE, "stone"},
    {BlockTextureSlots::DIRT, "dirt"},
    {BlockTextureSlots::GRASS_TOP, "grass_top"},
    {BlockTextureSlots::GRASS_SIDE, "grass_side"},
    {BlockTextureSlots::COBBLESTONE, "cobblestone"},
    {BlockTextureSlots::PLANKS, "planks_oak"},
    {BlockTextureSlots::LOG_SIDE, "log_oak"},
    {BlockTextureSlots::LOG_TOP, "log_oak_top"},
    {BlockTextureSlots::LEAVES, "leaves_oak"},
    {BlockTextureSlots::SAND, "sand"},
    {BlockTextureSlots::GRAVEL, "gravel"},
    {BlockTextureSlots::WATER, "water_still"},
    {BlockTextureSlots::BEDROCK, "bedrock"},
    {BlockTextureSlots::COAL_ORE, "coal_ore"},
    {BlockTextureSlots::IRON_ORE, "iron_ore"},
    {BlockTextureSlots::GOLD_ORE, "gold_ore"},
    {BlockTextureSlots::DIAMOND_ORE, "diamond_ore"},
    {BlockTextureSlots::GLASS, "glass"},
    {BlockTextureSlots::BRICK, "brick"},
    {BlockTextureSlots::SNOW, "snow"},
    {BlockTextureSlots::CACTUS_SIDE, "cactus_side"},
    {BlockTextureSlots::CACTUS_TOP, "cactus_top"},
    {BlockTextureSlots::GLOWSTONE, "glowstone"},
    {BlockTextureSlots::LAVA, "lava_still"},
};

class TexturePackLoader {
public:
    GLuint albedoAtlas = 0;    // Main color texture
    GLuint normalAtlas = 0;    // Normal map texture
    bool hasNormalMaps = false;
    std::string packName = "default";

    // Load texture pack from folder
    // Expected structure:
    //   textures/pack_name/stone.png
    //   textures/pack_name/stone_n.png (normal map, optional)
    bool loadFromFolder(const std::string& folderPath) {
        std::filesystem::path basePath(folderPath);

        if (!std::filesystem::exists(basePath)) {
            std::cout << "[TexturePack] Folder not found: " << folderPath << std::endl;
            return false;
        }

        packName = basePath.filename().string();
        std::cout << "[TexturePack] Loading texture pack: " << packName << std::endl;

        // Create atlas pixel buffers
        std::vector<uint8_t> albedoPixels(TEX_ATLAS_PX * TEX_ATLAS_PX * 4, 255);
        std::vector<uint8_t> normalPixels(TEX_ATLAS_PX * TEX_ATLAS_PX * 4, 0);

        // Initialize normal map with flat normal (pointing up in tangent space)
        // RGB = (128, 128, 255) = normal pointing straight out
        for (int i = 0; i < TEX_ATLAS_PX * TEX_ATLAS_PX; i++) {
            normalPixels[i * 4 + 0] = 128;  // X = 0 (centered)
            normalPixels[i * 4 + 1] = 128;  // Y = 0 (centered)
            normalPixels[i * 4 + 2] = 255;  // Z = 1 (pointing out)
            normalPixels[i * 4 + 3] = 255;  // Alpha
        }

        int loadedCount = 0;
        int normalCount = 0;

        // Load each texture
        for (const auto& [slot, name] : TEXTURE_NAMES) {
            // Try to load albedo texture
            std::string albedoPath = (basePath / (name + ".png")).string();
            if (loadTextureToAtlas(albedoPath, slot, albedoPixels)) {
                loadedCount++;
            }

            // Try to load normal map
            std::string normalPath = (basePath / (name + "_n.png")).string();
            if (loadTextureToAtlas(normalPath, slot, normalPixels)) {
                normalCount++;
            }
        }

        std::cout << "[TexturePack] Loaded " << loadedCount << " albedo textures, "
                  << normalCount << " normal maps" << std::endl;

        hasNormalMaps = (normalCount > 0);

        // Upload to GPU
        createGLTexture(albedoAtlas, albedoPixels, true);  // Albedo with mipmaps
        createGLTexture(normalAtlas, normalPixels, true);   // Normal maps with mipmaps

        return loadedCount > 0;
    }

    // Generate procedural textures with optional normal maps
    void generateProcedural() {
        std::cout << "[TexturePack] Generating procedural textures with normal maps" << std::endl;

        std::vector<uint8_t> albedoPixels(TEX_ATLAS_PX * TEX_ATLAS_PX * 4, 255);
        std::vector<uint8_t> normalPixels(TEX_ATLAS_PX * TEX_ATLAS_PX * 4, 0);

        // Initialize flat normals
        for (int i = 0; i < TEX_ATLAS_PX * TEX_ATLAS_PX; i++) {
            normalPixels[i * 4 + 0] = 128;
            normalPixels[i * 4 + 1] = 128;
            normalPixels[i * 4 + 2] = 255;
            normalPixels[i * 4 + 3] = 255;
        }

        // Generate each texture with accompanying normal map
        generateStone(albedoPixels, normalPixels, 0, 0);
        generateDirt(albedoPixels, normalPixels, 1, 0);
        generateGrassTop(albedoPixels, normalPixels, 2, 0);
        generateGrassSide(albedoPixels, normalPixels, 3, 0);
        generateCobblestone(albedoPixels, normalPixels, 4, 0);
        generatePlanks(albedoPixels, normalPixels, 5, 0);
        generateLogSide(albedoPixels, normalPixels, 6, 0);
        generateLogTop(albedoPixels, normalPixels, 7, 0);
        generateLeaves(albedoPixels, normalPixels, 8, 0);
        generateSand(albedoPixels, normalPixels, 9, 0);
        generateGravel(albedoPixels, normalPixels, 10, 0);
        generateWater(albedoPixels, normalPixels, 11, 0);
        generateBedrock(albedoPixels, normalPixels, 12, 0);
        generateOre(albedoPixels, normalPixels, 13, 0, 30, 30, 35);      // Coal
        generateOre(albedoPixels, normalPixels, 14, 0, 200, 170, 145);   // Iron
        generateOre(albedoPixels, normalPixels, 15, 0, 250, 210, 50);    // Gold
        generateOre(albedoPixels, normalPixels, 0, 1, 80, 230, 235);     // Diamond
        generateGlass(albedoPixels, normalPixels, 1, 1);
        generateBrick(albedoPixels, normalPixels, 2, 1);
        generateSnow(albedoPixels, normalPixels, 3, 1);
        generateCactusSide(albedoPixels, normalPixels, 4, 1);
        generateCactusTop(albedoPixels, normalPixels, 5, 1);
        generateGlowstone(albedoPixels, normalPixels, 6, 1);
        generateLava(albedoPixels, normalPixels, 7, 1);

        hasNormalMaps = true;

        // Upload to GPU
        createGLTexture(albedoAtlas, albedoPixels, true);
        createGLTexture(normalAtlas, normalPixels, true);

        packName = "procedural";
    }

    void bind(GLuint albedoUnit = 0, GLuint normalUnit = 3) const {
        glActiveTexture(GL_TEXTURE0 + albedoUnit);
        glBindTexture(GL_TEXTURE_2D, albedoAtlas);

        if (hasNormalMaps) {
            glActiveTexture(GL_TEXTURE0 + normalUnit);
            glBindTexture(GL_TEXTURE_2D, normalAtlas);
        }
    }

    void bindAlbedoOnly(GLuint unit = 0) const {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, albedoAtlas);
    }

    bool normalMapsAvailable() const {
        return normalAtlas != 0;
    }

    void destroy() {
        if (albedoAtlas) {
            glDeleteTextures(1, &albedoAtlas);
            albedoAtlas = 0;
        }
        if (normalAtlas) {
            glDeleteTextures(1, &normalAtlas);
            normalAtlas = 0;
        }
    }

private:
    // Load a single texture file into the atlas at the given slot
    bool loadTextureToAtlas(const std::string& path, int slot, std::vector<uint8_t>& atlasPixels) {
        if (!std::filesystem::exists(path)) {
            return false;
        }

        int width, height, channels;
        stbi_set_flip_vertically_on_load(false);
        unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);

        if (!data) {
            std::cerr << "[TexturePack] Failed to load: " << path << std::endl;
            return false;
        }

        // Calculate atlas position
        int atlasX = (slot % TEX_ATLAS_DIM) * TEX_SIZE;
        int atlasY = (slot / TEX_ATLAS_DIM) * TEX_SIZE;

        // Copy pixels (resize if necessary)
        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                // Sample from source (handle different sizes)
                int srcX = (x * width) / TEX_SIZE;
                int srcY = (y * height) / TEX_SIZE;
                int srcIdx = (srcY * width + srcX) * 4;

                int dstX = atlasX + x;
                int dstY = atlasY + y;
                int dstIdx = (dstY * TEX_ATLAS_PX + dstX) * 4;

                atlasPixels[dstIdx + 0] = data[srcIdx + 0];
                atlasPixels[dstIdx + 1] = data[srcIdx + 1];
                atlasPixels[dstIdx + 2] = data[srcIdx + 2];
                atlasPixels[dstIdx + 3] = data[srcIdx + 3];
            }
        }

        stbi_image_free(data);
        return true;
    }

    void createGLTexture(GLuint& texID, const std::vector<uint8_t>& pixels, bool mipmap) {
        if (texID) {
            glDeleteTextures(1, &texID);
        }

        glGenTextures(1, &texID);
        glBindTexture(GL_TEXTURE_2D, texID);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_ATLAS_PX, TEX_ATLAS_PX,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        // Nearest neighbor for pixelated look (albedo), linear for normals
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmap ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        if (mipmap) {
            glGenerateMipmap(GL_TEXTURE_2D);
        }
    }

    // ==================== PROCEDURAL TEXTURE GENERATORS ====================
    // Each generates both albedo and normal map

    float noise(int x, int y, int seed) {
        int n = x + y * 57 + seed * 131;
        n = (n << 13) ^ n;
        return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
    }

    void setPixel(std::vector<uint8_t>& pixels, int ax, int ay, int lx, int ly,
                  uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        int px = ax * TEX_SIZE + lx;
        int py = ay * TEX_SIZE + ly;
        int idx = (py * TEX_ATLAS_PX + px) * 4;
        pixels[idx + 0] = r;
        pixels[idx + 1] = g;
        pixels[idx + 2] = b;
        pixels[idx + 3] = a;
    }

    // Set normal at pixel - nx, ny, nz should be in range [-1, 1]
    void setNormal(std::vector<uint8_t>& normals, int ax, int ay, int lx, int ly,
                   float nx, float ny, float nz) {
        // Convert from [-1,1] to [0,255]
        uint8_t r = static_cast<uint8_t>((nx * 0.5f + 0.5f) * 255.0f);
        uint8_t g = static_cast<uint8_t>((ny * 0.5f + 0.5f) * 255.0f);
        uint8_t b = static_cast<uint8_t>((nz * 0.5f + 0.5f) * 255.0f);
        setPixel(normals, ax, ay, lx, ly, r, g, b, 255);
    }

    // Generate height-based normal from noise
    void generateNormalFromHeight(std::vector<uint8_t>& normals, int ax, int ay,
                                   std::function<float(int, int)> heightFunc, float strength = 1.0f) {
        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                // Sample heights
                float h = heightFunc(x, y);
                float hL = heightFunc(x - 1, y);
                float hR = heightFunc(x + 1, y);
                float hU = heightFunc(x, y - 1);
                float hD = heightFunc(x, y + 1);

                // Calculate normal from height differences
                float dx = (hR - hL) * strength;
                float dy = (hD - hU) * strength;
                float dz = 1.0f;

                // Normalize
                float len = sqrtf(dx * dx + dy * dy + dz * dz);
                dx /= len;
                dy /= len;
                dz /= len;

                setNormal(normals, ax, ay, x, y, dx, dy, dz);
            }
        }
    }

    void generateStone(std::vector<uint8_t>& albedo, std::vector<uint8_t>& normals, int ax, int ay) {
        auto heightFunc = [this](int x, int y) {
            return noise(x, y, 42) * 0.3f + noise(x * 2, y * 2, 123) * 0.15f;
        };

        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                uint8_t base = 140;
                float n1 = noise(x, y, 42) * 20;
                float n2 = noise(x * 2, y * 2, 123) * 10;
                uint8_t v = static_cast<uint8_t>(std::clamp(base + n1 + n2, 80.0f, 170.0f));
                setPixel(albedo, ax, ay, x, y, v - 5, v, v + 8);
            }
        }
        generateNormalFromHeight(normals, ax, ay, heightFunc, 2.0f);
    }

    void generateDirt(std::vector<uint8_t>& albedo, std::vector<uint8_t>& normals, int ax, int ay) {
        auto heightFunc = [this](int x, int y) {
            float h = noise(x, y, 77) * 0.25f;
            if (noise(x, y, 999) > 0.7f) h -= 0.3f;
            return h;
        };

        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                uint8_t r = 145, g = 95, b = 55;
                float n = noise(x, y, 77) * 25;
                r = static_cast<uint8_t>(std::clamp(r + n, 0.0f, 255.0f));
                g = static_cast<uint8_t>(std::clamp(g + n, 0.0f, 255.0f));
                b = static_cast<uint8_t>(std::clamp(b + n, 0.0f, 255.0f));
                if (noise(x, y, 999) > 0.7f) { r -= 30; g -= 20; b -= 15; }
                setPixel(albedo, ax, ay, x, y, r, g, b);
            }
        }
        generateNormalFromHeight(normals, ax, ay, heightFunc, 1.5f);
    }

    void generateGrassTop(std::vector<uint8_t>& albedo, std::vector<uint8_t>& normals, int ax, int ay) {
        auto heightFunc = [this](int x, int y) {
            return noise(x * 3, y * 3, 55) * 0.15f;
        };

        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                uint8_t r = 75, g = 175, b = 95;
                float n = noise(x, y, 55) * 20;
                r = static_cast<uint8_t>(std::clamp(r + n, 0.0f, 255.0f));
                g = static_cast<uint8_t>(std::clamp(g + n, 0.0f, 255.0f));
                b = static_cast<uint8_t>(std::clamp(b + n, 0.0f, 255.0f));
                if (noise(x * 3, y * 3, 888) > 0.85f) { r += 30; g += 40; b += 20; }
                setPixel(albedo, ax, ay, x, y, r, g, b);
            }
        }
        generateNormalFromHeight(normals, ax, ay, heightFunc, 1.0f);
    }

    void generateGrassSide(std::vector<uint8_t>& albedo, std::vector<uint8_t>& normals, int ax, int ay) {
        auto heightFunc = [this](int x, int y) {
            if (y < 4) return noise(x, y, 55) * 0.1f;
            return noise(x, y, 77) * 0.2f;
        };

        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                if (y < 4) {
                    uint8_t r = 75, g = static_cast<uint8_t>(175 - y * 10), b = 95;
                    float n = noise(x, y, 55) * 15;
                    r = static_cast<uint8_t>(std::clamp(r + n, 0.0f, 255.0f));
                    g = static_cast<uint8_t>(std::clamp(g + n, 0.0f, 255.0f));
                    b = static_cast<uint8_t>(std::clamp(b + n, 0.0f, 255.0f));
                    setPixel(albedo, ax, ay, x, y, r, g, b);
                } else {
                    uint8_t r = 145, g = 95, b = 55;
                    float n = noise(x, y, 77) * 20;
                    r = static_cast<uint8_t>(std::clamp(r + n, 0.0f, 255.0f));
                    g = static_cast<uint8_t>(std::clamp(g + n, 0.0f, 255.0f));
                    b = static_cast<uint8_t>(std::clamp(b + n, 0.0f, 255.0f));
                    setPixel(albedo, ax, ay, x, y, r, g, b);
                }
            }
        }
        generateNormalFromHeight(normals, ax, ay, heightFunc, 1.5f);
    }

    void generateCobblestone(std::vector<uint8_t>& albedo, std::vector<uint8_t>& normals, int ax, int ay) {
        auto heightFunc = [this](int x, int y) {
            int cellX = (x + static_cast<int>(noise(x, y, 11) * 3)) / 4;
            int cellY = (y + static_cast<int>(noise(x, y, 22) * 3)) / 4;
            return noise(cellX, cellY, 33) * 0.5f;
        };

        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                uint8_t base = 120;
                int cellX = (x + static_cast<int>(noise(x, y, 11) * 3)) / 4;
                int cellY = (y + static_cast<int>(noise(x, y, 22) * 3)) / 4;
                float cellNoise = noise(cellX, cellY, 33) * 35;
                uint8_t v = static_cast<uint8_t>(std::clamp(base + cellNoise, 70.0f, 160.0f));
                setPixel(albedo, ax, ay, x, y, v + 5, v, v - 5);
            }
        }
        generateNormalFromHeight(normals, ax, ay, heightFunc, 3.0f);
    }

    void generatePlanks(std::vector<uint8_t>& albedo, std::vector<uint8_t>& normals, int ax, int ay) {
        auto heightFunc = [this](int x, int y) {
            float h = sin(y * 0.8f + noise(x, y, 44) * 2) * 0.1f;
            if (y % 4 == 0 || x % 8 == 0) h -= 0.2f;
            return h;
        };

        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                uint8_t r = 195, g = 155, b = 95;
                float grain = sin(y * 0.8f + noise(x, y, 44) * 2) * 10;
                r = static_cast<uint8_t>(std::clamp(static_cast<int>(r) + static_cast<int>(grain), 0, 255));
                g = static_cast<uint8_t>(std::clamp(static_cast<int>(g) + static_cast<int>(grain), 0, 255));
                if (y % 4 == 0 || x % 8 == 0) { r -= 25; g -= 20; b -= 15; }
                float n = noise(x, y, 88) * 8;
                r = static_cast<uint8_t>(std::clamp(r + n, 0.0f, 255.0f));
                g = static_cast<uint8_t>(std::clamp(g + n, 0.0f, 255.0f));
                b = static_cast<uint8_t>(std::clamp(b + n, 0.0f, 255.0f));
                setPixel(albedo, ax, ay, x, y, r, g, b);
            }
        }
        generateNormalFromHeight(normals, ax, ay, heightFunc, 2.0f);
    }

    void generateLogSide(std::vector<uint8_t>& albedo, std::vector<uint8_t>& normals, int ax, int ay) {
        auto heightFunc = [this](int x, int y) {
            return noise(x / 2, y, 55) * 0.3f;
        };

        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                uint8_t r = 85, g = 60, b = 40;
                float bark = noise(x / 2, y, 55) * 20;
                r = static_cast<uint8_t>(std::clamp(static_cast<float>(r) + bark, 40.0f, 120.0f));
                g = static_cast<uint8_t>(std::clamp(static_cast<float>(g) + bark * 0.7f, 30.0f, 90.0f));
                b = static_cast<uint8_t>(std::clamp(static_cast<float>(b) + bark * 0.5f, 20.0f, 70.0f));
                setPixel(albedo, ax, ay, x, y, r, g, b);
            }
        }
        generateNormalFromHeight(normals, ax, ay, heightFunc, 2.5f);
    }

    void generateLogTop(std::vector<uint8_t>& albedo, std::vector<uint8_t>& normals, int ax, int ay) {
        int cx = TEX_SIZE / 2, cy = TEX_SIZE / 2;
        auto heightFunc = [this, cx, cy](int x, int y) {
            float dist = sqrtf(static_cast<float>((x - cx) * (x - cx) + (y - cy) * (y - cy)));
            if (dist < 6) return sin(dist * 1.5f) * 0.15f;
            return noise(x, y, 66) * 0.2f;
        };

        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                float dist = sqrtf(static_cast<float>((x - cx) * (x - cx) + (y - cy) * (y - cy)));
                if (dist < 6) {
                    uint8_t r = 180, g = 145, b = 90;
                    float ring = sin(dist * 1.5f) * 15;
                    r = static_cast<uint8_t>(std::clamp(static_cast<int>(r) + static_cast<int>(ring), 140, 210));
                    g = static_cast<uint8_t>(std::clamp(static_cast<int>(g) + static_cast<int>(ring), 110, 175));
                    setPixel(albedo, ax, ay, x, y, r, g, b);
                } else {
                    uint8_t r = 85, g = 60, b = 40;
                    float n = noise(x, y, 66) * 15;
                    r = static_cast<uint8_t>(std::clamp(r + n, 0.0f, 255.0f));
                    g = static_cast<uint8_t>(std::clamp(g + n, 0.0f, 255.0f));
                    b = static_cast<uint8_t>(std::clamp(b + n, 0.0f, 255.0f));
                    setPixel(albedo, ax, ay, x, y, r, g, b);
                }
            }
        }
        generateNormalFromHeight(normals, ax, ay, heightFunc, 2.0f);
    }

    void generateLeaves(std::vector<uint8_t>& albedo, std::vector<uint8_t>& normals, int ax, int ay) {
        auto heightFunc = [this](int x, int y) {
            return noise(x * 2, y * 2, 77) * 0.4f;
        };

        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                uint8_t r = 55, g = 140, b = 45;
                float n = noise(x * 2, y * 2, 77);
                if (n > 0.3f) { r += 25; g += 35; b += 15; }
                if (n < -0.5f) { r -= 20; g -= 25; b -= 10; }
                float v = noise(x, y, 99) * 12;
                r = static_cast<uint8_t>(std::clamp(r + v, 0.0f, 255.0f));
                g = static_cast<uint8_t>(std::clamp(g + v, 0.0f, 255.0f));
                b = static_cast<uint8_t>(std::clamp(b + v, 0.0f, 255.0f));
                setPixel(albedo, ax, ay, x, y, r, g, b);
            }
        }
        generateNormalFromHeight(normals, ax, ay, heightFunc, 2.0f);
    }

    void generateSand(std::vector<uint8_t>& albedo, std::vector<uint8_t>& normals, int ax, int ay) {
        auto heightFunc = [this](int x, int y) {
            float h = noise(x, y, 111) * 0.1f;
            if (noise(x * 4, y * 4, 222) > 0.8f) h += 0.15f;
            return h;
        };

        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                uint8_t r = 230, g = 205, b = 160;
                float n = noise(x, y, 111) * 15;
                r = static_cast<uint8_t>(std::clamp(r + n, 0.0f, 255.0f));
                g = static_cast<uint8_t>(std::clamp(g + n, 0.0f, 255.0f));
                b = static_cast<uint8_t>(std::clamp(b + n, 0.0f, 255.0f));
                if (noise(x * 4, y * 4, 222) > 0.8f) { r -= 30; g -= 25; b -= 20; }
                setPixel(albedo, ax, ay, x, y, r, g, b);
            }
        }
        generateNormalFromHeight(normals, ax, ay, heightFunc, 0.8f);
    }

    void generateGravel(std::vector<uint8_t>& albedo, std::vector<uint8_t>& normals, int ax, int ay) {
        auto heightFunc = [this](int x, int y) {
            return noise(x, y, 333) * 0.4f;
        };

        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                uint8_t base = 130;
                float n = noise(x, y, 333) * 40;
                uint8_t v = static_cast<uint8_t>(std::clamp(base + n, 80.0f, 175.0f));
                uint8_t r = v, g = v, b = v;
                if (noise(x * 2, y * 2, 444) > 0.5f) { r += 10; g += 5; }
                setPixel(albedo, ax, ay, x, y, r, g, b);
            }
        }
        generateNormalFromHeight(normals, ax, ay, heightFunc, 3.0f);
    }

    void generateWater(std::vector<uint8_t>& albedo, std::vector<uint8_t>& normals, int ax, int ay) {
        // Water gets flat normals (actual waves are in shader)
        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                uint8_t r = 40, g = 100, b = 180;
                float wave = sin((x + y) * 0.5f) * 15;
                b = static_cast<uint8_t>(std::clamp(static_cast<float>(b) + wave, 140.0f, 220.0f));
                g = static_cast<uint8_t>(std::clamp(static_cast<float>(g) + wave * 0.5f, 80.0f, 130.0f));
                setPixel(albedo, ax, ay, x, y, r, g, b, 200);
                setNormal(normals, ax, ay, x, y, 0, 0, 1);
            }
        }
    }

    void generateBedrock(std::vector<uint8_t>& albedo, std::vector<uint8_t>& normals, int ax, int ay) {
        auto heightFunc = [this](int x, int y) {
            return noise(x, y, 555) * 0.3f;
        };

        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                uint8_t base = 35;
                float n = noise(x, y, 555) * 20;
                uint8_t v = static_cast<uint8_t>(std::clamp(base + n, 15.0f, 55.0f));
                setPixel(albedo, ax, ay, x, y, v, v, v + 5);
            }
        }
        generateNormalFromHeight(normals, ax, ay, heightFunc, 2.5f);
    }

    void generateOre(std::vector<uint8_t>& albedo, std::vector<uint8_t>& normals, int ax, int ay,
                     uint8_t oreR, uint8_t oreG, uint8_t oreB) {
        auto heightFunc = [this](int x, int y) {
            float h = noise(x, y, 42) * 0.2f;
            if (noise(x * 3, y * 3, 666) > 0.55f) h += 0.25f;
            return h;
        };

        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                uint8_t v = static_cast<uint8_t>(140 + noise(x, y, 42) * 15);
                uint8_t r = v - 5, g = v, b = v + 8;
                if (noise(x * 3, y * 3, 666) > 0.55f) {
                    r = oreR; g = oreG; b = oreB;
                }
                setPixel(albedo, ax, ay, x, y, r, g, b);
            }
        }
        generateNormalFromHeight(normals, ax, ay, heightFunc, 2.0f);
    }

    void generateGlass(std::vector<uint8_t>& albedo, std::vector<uint8_t>& normals, int ax, int ay) {
        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                uint8_t r = 200, g = 220, b = 240, a = 60;
                if (x == 0 || x == 15 || y == 0 || y == 15) { r = 180; g = 200; b = 220; a = 180; }
                if ((x + y) % 8 < 2) { r = 240; g = 250; b = 255; a = 100; }
                setPixel(albedo, ax, ay, x, y, r, g, b, a);
                setNormal(normals, ax, ay, x, y, 0, 0, 1);
            }
        }
    }

    void generateBrick(std::vector<uint8_t>& albedo, std::vector<uint8_t>& normals, int ax, int ay) {
        auto heightFunc = [](int x, int y) {
            int row = y / 4;
            int offset = (row % 2) * 4;
            if (y % 4 == 0 || (x + offset) % 8 == 0) return -0.3f;
            return 0.0f;
        };

        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                uint8_t r = 175, g = 85, b = 65;
                float n = noise(x, y, 111) * 15;
                r = static_cast<uint8_t>(std::clamp(r + n, 0.0f, 255.0f));
                g = static_cast<uint8_t>(std::clamp(g + n, 0.0f, 255.0f));
                b = static_cast<uint8_t>(std::clamp(b + n, 0.0f, 255.0f));

                int row = y / 4;
                int offset = (row % 2) * 4;
                if (y % 4 == 0 || (x + offset) % 8 == 0) {
                    r = 200; g = 195; b = 180;
                    n = noise(x, y, 222) * 10;
                    r = static_cast<uint8_t>(std::clamp(r + n, 0.0f, 255.0f));
                    g = static_cast<uint8_t>(std::clamp(g + n, 0.0f, 255.0f));
                    b = static_cast<uint8_t>(std::clamp(b + n, 0.0f, 255.0f));
                }
                setPixel(albedo, ax, ay, x, y, r, g, b);
            }
        }
        generateNormalFromHeight(normals, ax, ay, heightFunc, 3.0f);
    }

    void generateSnow(std::vector<uint8_t>& albedo, std::vector<uint8_t>& normals, int ax, int ay) {
        auto heightFunc = [this](int x, int y) {
            return noise(x * 2, y * 2, 1234) * 0.1f;
        };

        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                uint8_t r = 245, g = 250, b = 255;
                float n = noise(x * 2, y * 2, 1234) * 8;
                r = static_cast<uint8_t>(std::clamp(static_cast<float>(r) + n, 235.0f, 255.0f));
                g = static_cast<uint8_t>(std::clamp(static_cast<float>(g) + n, 240.0f, 255.0f));
                if (noise(x * 5, y * 5, 5678) > 0.9f) { r = 255; g = 255; b = 255; }
                setPixel(albedo, ax, ay, x, y, r, g, b);
            }
        }
        generateNormalFromHeight(normals, ax, ay, heightFunc, 0.5f);
    }

    void generateCactusSide(std::vector<uint8_t>& albedo, std::vector<uint8_t>& normals, int ax, int ay) {
        auto heightFunc = [this](int x, int y) {
            float h = 0;
            if ((x + 2) % 4 == 0) h -= 0.3f;
            h += noise(x, y, 321) * 0.1f;
            return h;
        };

        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                uint8_t r = 50, g = 120, b = 45;
                float n = noise(x, y, 321) * 15;
                r = static_cast<uint8_t>(std::clamp(r + n, 0.0f, 255.0f));
                g = static_cast<uint8_t>(std::clamp(g + n, 0.0f, 255.0f));
                b = static_cast<uint8_t>(std::clamp(b + n, 0.0f, 255.0f));
                if ((x + 2) % 4 == 0) { r -= 15; g -= 20; b -= 10; }
                if (noise(x * 4, y * 4, 654) > 0.85f) { r = 200; g = 195; b = 150; }
                setPixel(albedo, ax, ay, x, y, r, g, b);
            }
        }
        generateNormalFromHeight(normals, ax, ay, heightFunc, 2.5f);
    }

    void generateCactusTop(std::vector<uint8_t>& albedo, std::vector<uint8_t>& normals, int ax, int ay) {
        int cx = TEX_SIZE / 2, cy = TEX_SIZE / 2;
        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                float dist = sqrtf(static_cast<float>((x - cx) * (x - cx) + (y - cy) * (y - cy)));
                uint8_t r = 65, g = 140, b = 55;
                if (dist < 5) { r = 80; g = 160; b = 70; }
                float n = noise(x, y, 987) * 10;
                r = static_cast<uint8_t>(std::clamp(r + n, 0.0f, 255.0f));
                g = static_cast<uint8_t>(std::clamp(g + n, 0.0f, 255.0f));
                b = static_cast<uint8_t>(std::clamp(b + n, 0.0f, 255.0f));
                setPixel(albedo, ax, ay, x, y, r, g, b);
                setNormal(normals, ax, ay, x, y, 0, 0, 1);
            }
        }
    }

    void generateGlowstone(std::vector<uint8_t>& albedo, std::vector<uint8_t>& normals, int ax, int ay) {
        auto heightFunc = [this](int x, int y) {
            float n1 = noise(x * 2, y * 2, 1111);
            if (n1 > 0.2f) return 0.2f;
            if (n1 < -0.4f) return -0.3f;
            return 0.0f;
        };

        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                uint8_t r = 140, g = 110, b = 60;
                float n1 = noise(x * 2, y * 2, 1111);
                float n2 = noise(x * 3, y * 3, 2222);
                if (n1 > 0.2f) {
                    float brightness = (n1 - 0.2f) * 1.5f;
                    r = static_cast<uint8_t>(std::clamp(140 + brightness * 115, 140.0f, 255.0f));
                    g = static_cast<uint8_t>(std::clamp(110 + brightness * 100, 110.0f, 210.0f));
                    b = static_cast<uint8_t>(std::clamp(60 + brightness * 40, 60.0f, 100.0f));
                }
                if (n2 > 0.7f) { r = 255; g = 230; b = 120; }
                if (n1 < -0.4f) { r = 100; g = 75; b = 40; }
                setPixel(albedo, ax, ay, x, y, r, g, b);
            }
        }
        generateNormalFromHeight(normals, ax, ay, heightFunc, 2.0f);
    }

    void generateLava(std::vector<uint8_t>& albedo, std::vector<uint8_t>& normals, int ax, int ay) {
        // Lava gets mostly flat normals (animated in shader)
        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                float n1 = noise(x * 2, y * 2, 3333);
                float n2 = noise(x, y, 4444);
                uint8_t r, g, b;
                if (n1 > 0.3f) {
                    float heat = (n1 - 0.3f) * 2.0f;
                    r = 255;
                    g = static_cast<uint8_t>(std::clamp(100 + heat * 120, 100.0f, 220.0f));
                    b = static_cast<uint8_t>(std::clamp(heat * 50, 0.0f, 50.0f));
                } else if (n1 > -0.2f) {
                    r = 230; g = 80; b = 20;
                } else {
                    float cool = (-0.2f - n1) * 2.0f;
                    r = static_cast<uint8_t>(std::clamp(180 - cool * 120, 60.0f, 180.0f));
                    g = static_cast<uint8_t>(std::clamp(50 - cool * 40, 10.0f, 50.0f));
                    b = static_cast<uint8_t>(std::clamp(20 - cool * 15, 5.0f, 20.0f));
                }
                if (n2 > 0.8f && n1 > 0.0f) { r = 255; g = 255; b = 100; }
                setPixel(albedo, ax, ay, x, y, r, g, b);
                setNormal(normals, ax, ay, x, y, 0, 0, 1);
            }
        }
    }
};
