#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <cmath>
#include <random>

// Texture size for each block face
constexpr int TEXTURE_SIZE = 16;
// Number of textures per row in atlas
constexpr int ATLAS_SIZE = 16;
// Total atlas dimensions
constexpr int ATLAS_PIXELS = TEXTURE_SIZE * ATLAS_SIZE;

class TextureAtlas {
public:
    GLuint textureID = 0;

    void generate() {
        // Create pixel data for the atlas
        std::vector<uint8_t> pixels(ATLAS_PIXELS * ATLAS_PIXELS * 4, 255);

        // Generate each texture
        generateStone(pixels, 0, 0);
        generateDirt(pixels, 1, 0);
        generateGrassTop(pixels, 2, 0);
        generateGrassSide(pixels, 3, 0);
        generateCobblestone(pixels, 4, 0);
        generatePlanks(pixels, 5, 0);
        generateLogSide(pixels, 6, 0);
        generateLogTop(pixels, 7, 0);
        generateLeaves(pixels, 8, 0);
        generateSand(pixels, 9, 0);
        generateGravel(pixels, 10, 0);
        generateWater(pixels, 11, 0);
        generateBedrock(pixels, 12, 0);
        generateCoalOre(pixels, 13, 0);
        generateIronOre(pixels, 14, 0);
        generateGoldOre(pixels, 15, 0);
        generateDiamondOre(pixels, 0, 1);
        generateGlass(pixels, 1, 1);
        generateBrick(pixels, 2, 1);
        generateSnow(pixels, 3, 1);
        generateCactusSide(pixels, 4, 1);
        generateCactusTop(pixels, 5, 1);
        generateGlowstone(pixels, 6, 1);
        generateLava(pixels, 7, 1);

        // Upload to GPU
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ATLAS_PIXELS, ATLAS_PIXELS,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        // Pixelated look (nearest neighbor)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        glGenerateMipmap(GL_TEXTURE_2D);
    }

    void bind(GLuint unit = 0) const {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, textureID);
    }

    void destroy() {
        if (textureID) {
            glDeleteTextures(1, &textureID);
            textureID = 0;
        }
    }

    // Get UV coordinates for a texture slot
    static glm::vec4 getUV(int slot) {
        int x = slot % ATLAS_SIZE;
        int y = slot / ATLAS_SIZE;
        float u = static_cast<float>(x) / ATLAS_SIZE;
        float v = static_cast<float>(y) / ATLAS_SIZE;
        float size = 1.0f / ATLAS_SIZE;
        return glm::vec4(u, v, u + size, v + size);
    }

private:
    // Helper to set a pixel in the atlas
    void setPixel(std::vector<uint8_t>& pixels, int atlasX, int atlasY,
                  int localX, int localY, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        int px = atlasX * TEXTURE_SIZE + localX;
        int py = atlasY * TEXTURE_SIZE + localY;
        int idx = (py * ATLAS_PIXELS + px) * 4;
        pixels[idx + 0] = r;
        pixels[idx + 1] = g;
        pixels[idx + 2] = b;
        pixels[idx + 3] = a;
    }

    // Noise helper
    float noise(int x, int y, int seed) {
        int n = x + y * 57 + seed * 131;
        n = (n << 13) ^ n;
        return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
    }

    // Color variation helper
    void varyColor(uint8_t& r, uint8_t& g, uint8_t& b, int x, int y, int seed, int amount) {
        float n = noise(x, y, seed) * amount;
        r = static_cast<uint8_t>(std::clamp(static_cast<int>(r) + static_cast<int>(n), 0, 255));
        g = static_cast<uint8_t>(std::clamp(static_cast<int>(g) + static_cast<int>(n), 0, 255));
        b = static_cast<uint8_t>(std::clamp(static_cast<int>(b) + static_cast<int>(n), 0, 255));
    }

    // === TEXTURE GENERATORS - Unique stylized look ===

    void generateStone(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                uint8_t base = 140;
                float n1 = noise(x, y, 42) * 20;
                float n2 = noise(x * 2, y * 2, 123) * 10;
                uint8_t v = static_cast<uint8_t>(std::clamp(base + n1 + n2, 80.0f, 170.0f));
                // Slight blue-gray tint for unique look
                setPixel(pixels, ax, ay, x, y, v - 5, v, v + 8);
            }
        }
    }

    void generateDirt(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                // Rich brown with orange undertones
                uint8_t r = 145, g = 95, b = 55;
                varyColor(r, g, b, x, y, 77, 25);
                // Add some darker spots
                if (noise(x, y, 999) > 0.7f) {
                    r -= 30; g -= 20; b -= 15;
                }
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateGrassTop(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                // Vibrant green with teal undertones - unique look
                uint8_t r = 75, g = 175, b = 95;
                varyColor(r, g, b, x, y, 55, 20);
                // Scattered lighter spots like dew
                if (noise(x * 3, y * 3, 888) > 0.85f) {
                    r += 30; g += 40; b += 20;
                }
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateGrassSide(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                if (y < 4) {
                    // Grass on top with gradient
                    uint8_t r = 75, g = 175 - y * 10, b = 95;
                    varyColor(r, g, b, x, y, 55, 15);
                    setPixel(pixels, ax, ay, x, y, r, g, b);
                } else {
                    // Dirt below
                    uint8_t r = 145, g = 95, b = 55;
                    varyColor(r, g, b, x, y, 77, 20);
                    setPixel(pixels, ax, ay, x, y, r, g, b);
                }
            }
        }
    }

    void generateCobblestone(std::vector<uint8_t>& pixels, int ax, int ay) {
        // Create a cobbled pattern
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                uint8_t base = 120;
                // Create stone-like cells
                int cellX = (x + static_cast<int>(noise(x, y, 11) * 3)) / 4;
                int cellY = (y + static_cast<int>(noise(x, y, 22) * 3)) / 4;
                float cellNoise = noise(cellX, cellY, 33) * 35;
                uint8_t v = static_cast<uint8_t>(std::clamp(base + cellNoise, 70.0f, 160.0f));
                // Warm gray with slight brown tint
                setPixel(pixels, ax, ay, x, y, v + 5, v, v - 5);
            }
        }
    }

    void generatePlanks(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                // Warm honey-colored wood
                uint8_t r = 195, g = 155, b = 95;
                // Wood grain - horizontal lines
                float grain = sin(y * 0.8f + noise(x, y, 44) * 2) * 10;
                r = static_cast<uint8_t>(std::clamp(static_cast<int>(r) + static_cast<int>(grain), 0, 255));
                g = static_cast<uint8_t>(std::clamp(static_cast<int>(g) + static_cast<int>(grain), 0, 255));
                // Plank divisions
                if (y % 4 == 0 || x % 8 == 0) {
                    r -= 25; g -= 20; b -= 15;
                }
                varyColor(r, g, b, x, y, 88, 8);
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateLogSide(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                // Dark bark with reddish-brown tones
                uint8_t r = 85, g = 60, b = 40;
                // Vertical bark texture
                float bark = noise(x / 2, y, 55) * 20;
                r = static_cast<uint8_t>(std::clamp(static_cast<float>(r) + bark, 40.0f, 120.0f));
                g = static_cast<uint8_t>(std::clamp(static_cast<float>(g) + bark * 0.7f, 30.0f, 90.0f));
                b = static_cast<uint8_t>(std::clamp(static_cast<float>(b) + bark * 0.5f, 20.0f, 70.0f));
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateLogTop(std::vector<uint8_t>& pixels, int ax, int ay) {
        int cx = TEXTURE_SIZE / 2, cy = TEXTURE_SIZE / 2;
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                float dist = sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
                if (dist < 6) {
                    // Inner wood with rings
                    uint8_t r = 180, g = 145, b = 90;
                    float ring = sin(dist * 1.5f) * 15;
                    r = static_cast<uint8_t>(std::clamp(static_cast<int>(r) + static_cast<int>(ring), 140, 210));
                    g = static_cast<uint8_t>(std::clamp(static_cast<int>(g) + static_cast<int>(ring), 110, 175));
                    setPixel(pixels, ax, ay, x, y, r, g, b);
                } else {
                    // Bark edge
                    uint8_t r = 85, g = 60, b = 40;
                    varyColor(r, g, b, x, y, 66, 15);
                    setPixel(pixels, ax, ay, x, y, r, g, b);
                }
            }
        }
    }

    void generateLeaves(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                // Lush green with some transparency variation feel
                uint8_t r = 55, g = 140, b = 45;
                float n = noise(x * 2, y * 2, 77);
                if (n > 0.3f) {
                    r += 25; g += 35; b += 15;
                }
                if (n < -0.5f) {
                    r -= 20; g -= 25; b -= 10;
                }
                varyColor(r, g, b, x, y, 99, 12);
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateSand(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                // Warm peachy sand
                uint8_t r = 230, g = 205, b = 160;
                varyColor(r, g, b, x, y, 111, 15);
                // Scattered darker grains
                if (noise(x * 4, y * 4, 222) > 0.8f) {
                    r -= 30; g -= 25; b -= 20;
                }
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateGravel(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                // Mixed gray pebbles
                uint8_t base = 130;
                float n = noise(x, y, 333) * 40;
                uint8_t v = static_cast<uint8_t>(std::clamp(base + n, 80.0f, 175.0f));
                // Slight color variation in pebbles
                uint8_t r = v, g = v, b = v;
                if (noise(x * 2, y * 2, 444) > 0.5f) {
                    r += 10; g += 5;
                }
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateWater(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                // Deep blue with subtle waves
                uint8_t r = 40, g = 100, b = 180;
                float wave = sin((x + y) * 0.5f) * 15;
                b = static_cast<uint8_t>(std::clamp(static_cast<float>(b) + wave, 140.0f, 220.0f));
                g = static_cast<uint8_t>(std::clamp(static_cast<float>(g) + wave * 0.5f, 80.0f, 130.0f));
                setPixel(pixels, ax, ay, x, y, r, g, b, 200);
            }
        }
    }

    void generateBedrock(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                // Very dark with subtle texture
                uint8_t base = 35;
                float n = noise(x, y, 555) * 20;
                uint8_t v = static_cast<uint8_t>(std::clamp(base + n, 15.0f, 55.0f));
                setPixel(pixels, ax, ay, x, y, v, v, v + 5);
            }
        }
    }

    void generateCoalOre(std::vector<uint8_t>& pixels, int ax, int ay) {
        // Stone base with coal spots
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                uint8_t v = static_cast<uint8_t>(140 + noise(x, y, 42) * 15);
                uint8_t r = v - 5, g = v, b = v + 8;
                // Coal spots
                if (noise(x * 3, y * 3, 666) > 0.6f) {
                    r = 30; g = 30; b = 35;
                }
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateIronOre(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                uint8_t v = static_cast<uint8_t>(140 + noise(x, y, 42) * 15);
                uint8_t r = v - 5, g = v, b = v + 8;
                // Iron spots - peachy/tan color
                if (noise(x * 3, y * 3, 777) > 0.55f) {
                    r = 200; g = 170; b = 145;
                }
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateGoldOre(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                uint8_t v = static_cast<uint8_t>(140 + noise(x, y, 42) * 15);
                uint8_t r = v - 5, g = v, b = v + 8;
                // Gold spots - bright yellow
                if (noise(x * 3, y * 3, 888) > 0.6f) {
                    r = 250; g = 210; b = 50;
                }
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateDiamondOre(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                uint8_t v = static_cast<uint8_t>(140 + noise(x, y, 42) * 15);
                uint8_t r = v - 5, g = v, b = v + 8;
                // Diamond spots - bright cyan/teal
                if (noise(x * 3, y * 3, 999) > 0.6f) {
                    r = 80; g = 230; b = 235;
                }
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateGlass(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                // Light blue tint, mostly transparent
                uint8_t r = 200, g = 220, b = 240;
                uint8_t a = 60;
                // Frame edges
                if (x == 0 || x == 15 || y == 0 || y == 15) {
                    r = 180; g = 200; b = 220;
                    a = 180;
                }
                // Subtle reflection
                if ((x + y) % 8 < 2) {
                    r = 240; g = 250; b = 255;
                    a = 100;
                }
                setPixel(pixels, ax, ay, x, y, r, g, b, a);
            }
        }
    }

    void generateBrick(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                // Terracotta red-orange bricks
                uint8_t r = 175, g = 85, b = 65;
                varyColor(r, g, b, x, y, 111, 15);

                // Mortar lines
                bool isMortar = false;
                int row = y / 4;
                int offset = (row % 2) * 4;

                if (y % 4 == 0) isMortar = true;
                if ((x + offset) % 8 == 0) isMortar = true;

                if (isMortar) {
                    r = 200; g = 195; b = 180;
                    varyColor(r, g, b, x, y, 222, 10);
                }

                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateSnow(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                // Bright white with subtle blue shadows
                uint8_t r = 245, g = 250, b = 255;
                float n = noise(x * 2, y * 2, 1234) * 8;
                r = static_cast<uint8_t>(std::clamp(static_cast<float>(r) + n, 235.0f, 255.0f));
                g = static_cast<uint8_t>(std::clamp(static_cast<float>(g) + n, 240.0f, 255.0f));
                // Sparkle effect
                if (noise(x * 5, y * 5, 5678) > 0.9f) {
                    r = 255; g = 255; b = 255;
                }
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateCactusSide(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                // Dark green cactus
                uint8_t r = 50, g = 120, b = 45;
                varyColor(r, g, b, x, y, 321, 15);
                // Vertical ribs
                if ((x + 2) % 4 == 0) {
                    r -= 15; g -= 20; b -= 10;
                }
                // Spines
                if (noise(x * 4, y * 4, 654) > 0.85f) {
                    r = 200; g = 195; b = 150;
                }
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateCactusTop(std::vector<uint8_t>& pixels, int ax, int ay) {
        int cx = TEXTURE_SIZE / 2, cy = TEXTURE_SIZE / 2;
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                float dist = sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
                // Lighter green in center
                uint8_t r = 65, g = 140, b = 55;
                if (dist < 5) {
                    r = 80; g = 160; b = 70;
                }
                varyColor(r, g, b, x, y, 987, 10);
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateGlowstone(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                // Glowstone - golden yellow with bright spots like Minecraft
                // Base is tan/brown
                uint8_t r = 140, g = 110, b = 60;

                // Create irregular glowing crystal pattern
                float n1 = noise(x * 2, y * 2, 1111);
                float n2 = noise(x * 3, y * 3, 2222);

                // Bright yellow-orange glowing spots
                if (n1 > 0.2f) {
                    float brightness = (n1 - 0.2f) * 1.5f;
                    r = static_cast<uint8_t>(std::clamp(140 + brightness * 115, 140.0f, 255.0f));
                    g = static_cast<uint8_t>(std::clamp(110 + brightness * 100, 110.0f, 210.0f));
                    b = static_cast<uint8_t>(std::clamp(60 + brightness * 40, 60.0f, 100.0f));
                }

                // Extra bright spots
                if (n2 > 0.7f) {
                    r = 255; g = 230; b = 120;
                }

                // Dark cracks between crystals
                if (n1 < -0.4f) {
                    r = 100; g = 75; b = 40;
                }

                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateLava(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                // Lava - bright orange/red with dark crusted spots like Minecraft
                float n1 = noise(x * 2, y * 2, 3333);
                float n2 = noise(x, y, 4444);

                uint8_t r, g, b;

                if (n1 > 0.3f) {
                    // Bright molten lava - orange to yellow
                    float heat = (n1 - 0.3f) * 2.0f;
                    r = 255;
                    g = static_cast<uint8_t>(std::clamp(100 + heat * 120, 100.0f, 220.0f));
                    b = static_cast<uint8_t>(std::clamp(heat * 50, 0.0f, 50.0f));
                } else if (n1 > -0.2f) {
                    // Medium temperature - orange-red
                    r = 230;
                    g = 80;
                    b = 20;
                } else {
                    // Cooled crust - dark red/black
                    float cool = (-0.2f - n1) * 2.0f;
                    r = static_cast<uint8_t>(std::clamp(180 - cool * 120, 60.0f, 180.0f));
                    g = static_cast<uint8_t>(std::clamp(50 - cool * 40, 10.0f, 50.0f));
                    b = static_cast<uint8_t>(std::clamp(20 - cool * 15, 5.0f, 20.0f));
                }

                // Add some bright yellow hotspots
                if (n2 > 0.8f && n1 > 0.0f) {
                    r = 255; g = 255; b = 100;
                }

                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }
};
