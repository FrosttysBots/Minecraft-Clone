#pragma once

// Item Texture Atlas
// Generates procedural textures for all items (tools, materials, food, armor)

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <cmath>
#include "../core/Item.h"

// Item texture configuration (same size as block textures)
constexpr int ITEM_TEXTURE_SIZE = 16;
constexpr int ITEM_ATLAS_SIZE = 16;  // 16x16 grid = 256 slots
constexpr int ITEM_ATLAS_PIXELS = ITEM_TEXTURE_SIZE * ITEM_ATLAS_SIZE;

class ItemAtlas {
public:
    GLuint textureID = 0;

    void generate() {
        std::vector<uint8_t> pixels(ITEM_ATLAS_PIXELS * ITEM_ATLAS_PIXELS * 4, 0);

        // Row 0: Materials (slots 0-15)
        generateStick(pixels, 0, 0);
        generateCoal(pixels, 1, 0);
        generateCharcoal(pixels, 2, 0);
        generateIronIngot(pixels, 3, 0);
        generateGoldIngot(pixels, 4, 0);
        generateDiamond(pixels, 5, 0);
        generateFlint(pixels, 6, 0);
        generateLeather(pixels, 7, 0);
        generateString(pixels, 8, 0);
        generateFeather(pixels, 9, 0);
        generateBone(pixels, 10, 0);
        generateBrickItem(pixels, 11, 0);
        generateClay(pixels, 12, 0);

        // Row 1: Pickaxes (slots 16-20)
        generatePickaxe(pixels, 0, 1, ToolTier::WOOD);
        generatePickaxe(pixels, 1, 1, ToolTier::STONE);
        generatePickaxe(pixels, 2, 1, ToolTier::IRON);
        generatePickaxe(pixels, 3, 1, ToolTier::GOLD);
        generatePickaxe(pixels, 4, 1, ToolTier::DIAMOND);

        // Row 1 continued: Axes (slots 21-25)
        generateAxe(pixels, 5, 1, ToolTier::WOOD);
        generateAxe(pixels, 6, 1, ToolTier::STONE);
        generateAxe(pixels, 7, 1, ToolTier::IRON);
        generateAxe(pixels, 8, 1, ToolTier::GOLD);
        generateAxe(pixels, 9, 1, ToolTier::DIAMOND);

        // Row 1 continued: Shovels (slots 26-30)
        generateShovel(pixels, 10, 1, ToolTier::WOOD);
        generateShovel(pixels, 11, 1, ToolTier::STONE);
        generateShovel(pixels, 12, 1, ToolTier::IRON);
        generateShovel(pixels, 13, 1, ToolTier::GOLD);
        generateShovel(pixels, 14, 1, ToolTier::DIAMOND);

        // Row 2: Hoes (slots 31-35)
        generateHoe(pixels, 15, 1, ToolTier::WOOD);
        generateHoe(pixels, 0, 2, ToolTier::STONE);
        generateHoe(pixels, 1, 2, ToolTier::IRON);
        generateHoe(pixels, 2, 2, ToolTier::GOLD);
        generateHoe(pixels, 3, 2, ToolTier::DIAMOND);

        // Row 2 continued: Swords (slots 36-40)
        generateSword(pixels, 4, 2, ToolTier::WOOD);
        generateSword(pixels, 5, 2, ToolTier::STONE);
        generateSword(pixels, 6, 2, ToolTier::IRON);
        generateSword(pixels, 7, 2, ToolTier::GOLD);
        generateSword(pixels, 8, 2, ToolTier::DIAMOND);

        // Row 3: Helmets (slots 48-52)
        generateHelmet(pixels, 0, 3, ToolTier::NONE);    // Leather
        generateHelmet(pixels, 1, 3, ToolTier::IRON);
        generateHelmet(pixels, 2, 3, ToolTier::GOLD);
        generateHelmet(pixels, 3, 3, ToolTier::DIAMOND);
        generateHelmet(pixels, 4, 3, ToolTier::STONE);   // Chainmail (gray)

        // Row 3 continued: Chestplates (slots 53-57)
        generateChestplate(pixels, 5, 3, ToolTier::NONE);
        generateChestplate(pixels, 6, 3, ToolTier::IRON);
        generateChestplate(pixels, 7, 3, ToolTier::GOLD);
        generateChestplate(pixels, 8, 3, ToolTier::DIAMOND);
        generateChestplate(pixels, 9, 3, ToolTier::STONE);

        // Row 3 continued: Leggings (slots 58-62)
        generateLeggings(pixels, 10, 3, ToolTier::NONE);
        generateLeggings(pixels, 11, 3, ToolTier::IRON);
        generateLeggings(pixels, 12, 3, ToolTier::GOLD);
        generateLeggings(pixels, 13, 3, ToolTier::DIAMOND);
        generateLeggings(pixels, 14, 3, ToolTier::STONE);

        // Row 4: Boots (slots 63-67)
        generateBoots(pixels, 15, 3, ToolTier::NONE);
        generateBoots(pixels, 0, 4, ToolTier::IRON);
        generateBoots(pixels, 1, 4, ToolTier::GOLD);
        generateBoots(pixels, 2, 4, ToolTier::DIAMOND);
        generateBoots(pixels, 3, 4, ToolTier::STONE);

        // Row 5: Food (slots 80-96)
        generateApple(pixels, 0, 5);
        generateGoldenApple(pixels, 1, 5);
        generateBread(pixels, 2, 5);
        generateRawMeat(pixels, 3, 5);       // Raw porkchop
        generateCookedMeat(pixels, 4, 5);    // Cooked porkchop
        generateRawMeat(pixels, 5, 5);       // Raw beef
        generateSteak(pixels, 6, 5);         // Cooked beef (steak)
        generateChicken(pixels, 7, 5, false); // Raw chicken
        generateChicken(pixels, 8, 5, true);  // Cooked chicken
        generateRawMeat(pixels, 9, 5);       // Raw mutton
        generateCookedMeat(pixels, 10, 5);   // Cooked mutton
        generateCarrot(pixels, 11, 5);
        generatePotato(pixels, 12, 5, false);
        generatePotato(pixels, 13, 5, true);  // Baked
        generateMelon(pixels, 14, 5);
        generateCookie(pixels, 15, 5);
        generateRottenFlesh(pixels, 0, 6);

        // Row 7: Misc items (slots 112-116)
        generateBucket(pixels, 0, 7, 0);      // Empty
        generateBucket(pixels, 1, 7, 1);      // Water
        generateBucket(pixels, 2, 7, 2);      // Lava
        generateBowl(pixels, 3, 7);
        generateMushroomStew(pixels, 4, 7);

        // Upload to GPU
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ITEM_ATLAS_PIXELS, ITEM_ATLAS_PIXELS,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

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
        int x = slot % ITEM_ATLAS_SIZE;
        int y = slot / ITEM_ATLAS_SIZE;
        float u = static_cast<float>(x) / ITEM_ATLAS_SIZE;
        float v = static_cast<float>(y) / ITEM_ATLAS_SIZE;
        float size = 1.0f / ITEM_ATLAS_SIZE;
        return glm::vec4(u, v, u + size, v + size);
    }

    // Map ItemType to texture slot
    static int getTextureSlot(ItemType type) {
        return getItemProperties(type).textureSlot;
    }

private:
    // Helper to set a pixel
    void setPixel(std::vector<uint8_t>& pixels, int atlasX, int atlasY,
                  int localX, int localY, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        int px = atlasX * ITEM_TEXTURE_SIZE + localX;
        int py = atlasY * ITEM_TEXTURE_SIZE + localY;
        int idx = (py * ITEM_ATLAS_PIXELS + px) * 4;
        pixels[idx + 0] = r;
        pixels[idx + 1] = g;
        pixels[idx + 2] = b;
        pixels[idx + 3] = a;
    }

    // Get tier-specific colors
    void getTierColor(ToolTier tier, uint8_t& r, uint8_t& g, uint8_t& b) {
        switch (tier) {
            case ToolTier::WOOD:    r = 139; g = 90;  b = 43;  break;  // Brown
            case ToolTier::STONE:   r = 128; g = 128; b = 128; break;  // Gray
            case ToolTier::IRON:    r = 200; g = 200; b = 200; break;  // Light gray
            case ToolTier::GOLD:    r = 255; g = 215; b = 0;   break;  // Gold
            case ToolTier::DIAMOND: r = 80;  g = 220; b = 235; break;  // Cyan
            default:                r = 180; g = 130; b = 80;  break;  // Leather brown
        }
    }

    // Stick handle color
    void getHandleColor(uint8_t& r, uint8_t& g, uint8_t& b) {
        r = 139; g = 90; b = 43;  // Brown wood
    }

    // ==================== MATERIALS ====================

    void generateStick(std::vector<uint8_t>& pixels, int ax, int ay) {
        // Diagonal stick
        for (int i = 3; i < 14; i++) {
            int x = 15 - i;
            int y = i;
            setPixel(pixels, ax, ay, x, y, 139, 90, 43);
            setPixel(pixels, ax, ay, x - 1, y, 160, 110, 60);  // Highlight
        }
    }

    void generateCoal(std::vector<uint8_t>& pixels, int ax, int ay) {
        // Black chunk with shiny spots
        for (int y = 4; y < 12; y++) {
            for (int x = 4; x < 12; x++) {
                if ((x + y) % 7 == 0) continue;  // Irregular shape
                uint8_t v = 30 + (((x * 7 + y * 13) % 20));
                setPixel(pixels, ax, ay, x, y, v, v, v);
            }
        }
        // Shiny spots
        setPixel(pixels, ax, ay, 6, 6, 80, 80, 80);
        setPixel(pixels, ax, ay, 9, 8, 70, 70, 70);
    }

    void generateCharcoal(std::vector<uint8_t>& pixels, int ax, int ay) {
        // Similar to coal but with brown tint
        for (int y = 4; y < 12; y++) {
            for (int x = 4; x < 12; x++) {
                if ((x + y) % 6 == 0) continue;
                uint8_t v = 35 + (((x * 5 + y * 11) % 20));
                setPixel(pixels, ax, ay, x, y, v + 10, v, v - 5);
            }
        }
    }

    void generateIronIngot(std::vector<uint8_t>& pixels, int ax, int ay) {
        // Ingot shape
        for (int y = 5; y < 11; y++) {
            for (int x = 3; x < 13; x++) {
                uint8_t base = 180;
                if (y < 7) base = 200;  // Top lighter
                if (x < 5 || x > 10) base -= 20;  // Sides darker
                setPixel(pixels, ax, ay, x, y, base, base, base);
            }
        }
        // Highlight
        setPixel(pixels, ax, ay, 6, 6, 230, 230, 230);
    }

    void generateGoldIngot(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 5; y < 11; y++) {
            for (int x = 3; x < 13; x++) {
                uint8_t r = 255, g = 200, b = 50;
                if (y < 7) { r = 255; g = 220; b = 80; }
                if (x < 5 || x > 10) { r -= 30; g -= 30; b -= 20; }
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
        setPixel(pixels, ax, ay, 6, 6, 255, 255, 150);
    }

    void generateDiamond(std::vector<uint8_t>& pixels, int ax, int ay) {
        // Diamond shape
        int cx = 8, cy = 8;
        for (int y = 2; y < 14; y++) {
            for (int x = 2; x < 14; x++) {
                int dx = std::abs(x - cx);
                int dy = std::abs(y - cy);
                if (dx + dy <= 6) {
                    uint8_t r = 80, g = 220, b = 235;
                    if (dx + dy <= 3) { r = 120; g = 240; b = 255; }  // Center brighter
                    setPixel(pixels, ax, ay, x, y, r, g, b);
                }
            }
        }
    }

    void generateFlint(std::vector<uint8_t>& pixels, int ax, int ay) {
        // Dark gray arrowhead shape
        for (int y = 2; y < 14; y++) {
            int width = (14 - y) / 2;
            for (int x = 8 - width; x < 8 + width; x++) {
                if (x >= 0 && x < 16) {
                    uint8_t v = 50 + ((x * 3 + y * 7) % 30);
                    setPixel(pixels, ax, ay, x, y, v, v, v);
                }
            }
        }
    }

    void generateLeather(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 3; y < 13; y++) {
            for (int x = 3; x < 13; x++) {
                uint8_t r = 160, g = 100, b = 60;
                r += ((x * 5 + y * 3) % 20) - 10;
                g += ((x * 5 + y * 3) % 15) - 7;
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateString(std::vector<uint8_t>& pixels, int ax, int ay) {
        // Wavy white line
        for (int x = 2; x < 14; x++) {
            int y = 8 + static_cast<int>(std::sin(x * 0.8f) * 2);
            setPixel(pixels, ax, ay, x, y, 240, 240, 240);
            setPixel(pixels, ax, ay, x, y + 1, 220, 220, 220);
        }
    }

    void generateFeather(std::vector<uint8_t>& pixels, int ax, int ay) {
        // White feather
        for (int y = 2; y < 14; y++) {
            int x = 8 + (y - 8) / 3;
            for (int dx = -2; dx <= 2; dx++) {
                if (x + dx >= 0 && x + dx < 16) {
                    uint8_t v = 255 - std::abs(dx) * 20;
                    setPixel(pixels, ax, ay, x + dx, y, v, v, v);
                }
            }
        }
        // Quill
        for (int y = 10; y < 15; y++) {
            setPixel(pixels, ax, ay, 10, y, 200, 180, 150);
        }
    }

    void generateBone(std::vector<uint8_t>& pixels, int ax, int ay) {
        // Bone shape
        for (int y = 4; y < 12; y++) {
            setPixel(pixels, ax, ay, 7, y, 240, 235, 220);
            setPixel(pixels, ax, ay, 8, y, 240, 235, 220);
        }
        // Ends
        for (int x = 5; x < 11; x++) {
            setPixel(pixels, ax, ay, x, 3, 240, 235, 220);
            setPixel(pixels, ax, ay, x, 4, 240, 235, 220);
            setPixel(pixels, ax, ay, x, 11, 240, 235, 220);
            setPixel(pixels, ax, ay, x, 12, 240, 235, 220);
        }
    }

    void generateBrickItem(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 5; y < 11; y++) {
            for (int x = 3; x < 13; x++) {
                uint8_t r = 180, g = 80, b = 60;
                if ((x + y) % 3 == 0) { r -= 20; g -= 10; b -= 10; }
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateClay(std::vector<uint8_t>& pixels, int ax, int ay) {
        for (int y = 5; y < 11; y++) {
            for (int x = 5; x < 11; x++) {
                uint8_t r = 160, g = 165, b = 175;
                r += ((x * 7 + y * 3) % 10) - 5;
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    // ==================== TOOLS ====================

    void generatePickaxe(std::vector<uint8_t>& pixels, int ax, int ay, ToolTier tier) {
        uint8_t tr, tg, tb;
        getTierColor(tier, tr, tg, tb);

        // Handle (diagonal)
        for (int i = 6; i < 15; i++) {
            setPixel(pixels, ax, ay, i, i, 139, 90, 43);
        }

        // Head (horizontal at top)
        for (int x = 1; x < 10; x++) {
            setPixel(pixels, ax, ay, x, 2, tr, tg, tb);
            setPixel(pixels, ax, ay, x, 3, tr - 20, tg - 20, tb - 20);
        }
        // Vertical part
        for (int y = 4; y < 7; y++) {
            setPixel(pixels, ax, ay, 5, y, tr, tg, tb);
        }
    }

    void generateAxe(std::vector<uint8_t>& pixels, int ax, int ay, ToolTier tier) {
        uint8_t tr, tg, tb;
        getTierColor(tier, tr, tg, tb);

        // Handle
        for (int i = 5; i < 15; i++) {
            setPixel(pixels, ax, ay, i, i, 139, 90, 43);
        }

        // Axe head (curved blade)
        for (int y = 2; y < 9; y++) {
            int width = std::min(y - 1, 8 - y) + 2;
            for (int x = 2; x < 2 + width; x++) {
                setPixel(pixels, ax, ay, x, y, tr, tg, tb);
            }
        }
    }

    void generateShovel(std::vector<uint8_t>& pixels, int ax, int ay, ToolTier tier) {
        uint8_t tr, tg, tb;
        getTierColor(tier, tr, tg, tb);

        // Handle
        for (int i = 4; i < 15; i++) {
            setPixel(pixels, ax, ay, 8, i, 139, 90, 43);
        }

        // Shovel head (rounded)
        for (int y = 1; y < 7; y++) {
            int halfWidth = (y < 3) ? y + 1 : 4 - (y - 3);
            for (int x = 8 - halfWidth; x <= 8 + halfWidth; x++) {
                setPixel(pixels, ax, ay, x, y, tr, tg, tb);
            }
        }
    }

    void generateHoe(std::vector<uint8_t>& pixels, int ax, int ay, ToolTier tier) {
        uint8_t tr, tg, tb;
        getTierColor(tier, tr, tg, tb);

        // Handle
        for (int i = 5; i < 15; i++) {
            setPixel(pixels, ax, ay, i, i, 139, 90, 43);
        }

        // Hoe head (L-shape)
        for (int x = 2; x < 8; x++) {
            setPixel(pixels, ax, ay, x, 3, tr, tg, tb);
            setPixel(pixels, ax, ay, x, 4, tr - 15, tg - 15, tb - 15);
        }
        for (int y = 3; y < 7; y++) {
            setPixel(pixels, ax, ay, 2, y, tr, tg, tb);
        }
    }

    void generateSword(std::vector<uint8_t>& pixels, int ax, int ay, ToolTier tier) {
        uint8_t tr, tg, tb;
        getTierColor(tier, tr, tg, tb);

        // Handle
        for (int y = 11; y < 15; y++) {
            setPixel(pixels, ax, ay, 7, y, 139, 90, 43);
            setPixel(pixels, ax, ay, 8, y, 139, 90, 43);
        }

        // Guard
        for (int x = 5; x < 11; x++) {
            setPixel(pixels, ax, ay, x, 10, 100, 100, 100);
        }

        // Blade
        for (int y = 1; y < 10; y++) {
            setPixel(pixels, ax, ay, 7, y, tr, tg, tb);
            setPixel(pixels, ax, ay, 8, y, tr - 20, tg - 20, tb - 20);
        }
        // Point
        setPixel(pixels, ax, ay, 7, 0, tr, tg, tb);
    }

    // ==================== ARMOR ====================

    void generateHelmet(std::vector<uint8_t>& pixels, int ax, int ay, ToolTier tier) {
        uint8_t tr, tg, tb;
        getTierColor(tier, tr, tg, tb);

        // Helmet dome
        for (int y = 3; y < 10; y++) {
            int halfWidth = (y < 6) ? y - 2 : 4;
            for (int x = 8 - halfWidth; x <= 8 + halfWidth; x++) {
                uint8_t r = tr, g = tg, b = tb;
                if (y > 7) { r -= 30; g -= 30; b -= 30; }  // Bottom darker
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
        // Face opening
        for (int y = 7; y < 10; y++) {
            for (int x = 6; x < 10; x++) {
                setPixel(pixels, ax, ay, x, y, 0, 0, 0, 0);  // Transparent
            }
        }
    }

    void generateChestplate(std::vector<uint8_t>& pixels, int ax, int ay, ToolTier tier) {
        uint8_t tr, tg, tb;
        getTierColor(tier, tr, tg, tb);

        // Body
        for (int y = 2; y < 14; y++) {
            int halfWidth = (y < 4) ? 2 : ((y < 12) ? 4 : 3);
            for (int x = 8 - halfWidth; x <= 8 + halfWidth; x++) {
                setPixel(pixels, ax, ay, x, y, tr, tg, tb);
            }
        }
        // Arm holes (darker)
        for (int y = 4; y < 8; y++) {
            setPixel(pixels, ax, ay, 3, y, tr - 40, tg - 40, tb - 40);
            setPixel(pixels, ax, ay, 12, y, tr - 40, tg - 40, tb - 40);
        }
    }

    void generateLeggings(std::vector<uint8_t>& pixels, int ax, int ay, ToolTier tier) {
        uint8_t tr, tg, tb;
        getTierColor(tier, tr, tg, tb);

        // Waist
        for (int x = 4; x < 12; x++) {
            setPixel(pixels, ax, ay, x, 2, tr, tg, tb);
            setPixel(pixels, ax, ay, x, 3, tr, tg, tb);
        }
        // Left leg
        for (int y = 4; y < 14; y++) {
            setPixel(pixels, ax, ay, 5, y, tr, tg, tb);
            setPixel(pixels, ax, ay, 6, y, tr - 15, tg - 15, tb - 15);
        }
        // Right leg
        for (int y = 4; y < 14; y++) {
            setPixel(pixels, ax, ay, 9, y, tr, tg, tb);
            setPixel(pixels, ax, ay, 10, y, tr - 15, tg - 15, tb - 15);
        }
    }

    void generateBoots(std::vector<uint8_t>& pixels, int ax, int ay, ToolTier tier) {
        uint8_t tr, tg, tb;
        getTierColor(tier, tr, tg, tb);

        // Left boot
        for (int y = 4; y < 12; y++) {
            setPixel(pixels, ax, ay, 4, y, tr, tg, tb);
            setPixel(pixels, ax, ay, 5, y, tr, tg, tb);
        }
        for (int x = 3; x < 7; x++) {
            setPixel(pixels, ax, ay, x, 12, tr - 20, tg - 20, tb - 20);
        }

        // Right boot
        for (int y = 4; y < 12; y++) {
            setPixel(pixels, ax, ay, 10, y, tr, tg, tb);
            setPixel(pixels, ax, ay, 11, y, tr, tg, tb);
        }
        for (int x = 9; x < 13; x++) {
            setPixel(pixels, ax, ay, x, 12, tr - 20, tg - 20, tb - 20);
        }
    }

    // ==================== FOOD ====================

    void generateApple(std::vector<uint8_t>& pixels, int ax, int ay) {
        // Red apple
        for (int y = 4; y < 13; y++) {
            int halfWidth = (y < 8) ? (y - 3) : (13 - y);
            for (int x = 8 - halfWidth; x <= 8 + halfWidth; x++) {
                uint8_t r = 200, g = 30, b = 30;
                if (x < 7) r -= 30;  // Shadow
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
        // Stem
        setPixel(pixels, ax, ay, 8, 3, 80, 50, 20);
        setPixel(pixels, ax, ay, 8, 2, 80, 50, 20);
        // Leaf
        setPixel(pixels, ax, ay, 9, 3, 50, 150, 50);
        setPixel(pixels, ax, ay, 10, 2, 50, 150, 50);
    }

    void generateGoldenApple(std::vector<uint8_t>& pixels, int ax, int ay) {
        // Golden apple
        for (int y = 4; y < 13; y++) {
            int halfWidth = (y < 8) ? (y - 3) : (13 - y);
            for (int x = 8 - halfWidth; x <= 8 + halfWidth; x++) {
                uint8_t r = 255, g = 200, b = 50;
                if (x < 7) { r -= 30; g -= 30; }
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
        setPixel(pixels, ax, ay, 8, 3, 80, 50, 20);
        setPixel(pixels, ax, ay, 8, 2, 80, 50, 20);
    }

    void generateBread(std::vector<uint8_t>& pixels, int ax, int ay) {
        // Bread loaf
        for (int y = 6; y < 12; y++) {
            for (int x = 3; x < 13; x++) {
                uint8_t r = 200, g = 150, b = 80;
                if (y < 8) { r = 220; g = 170; b = 100; }  // Top crust lighter
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateRawMeat(std::vector<uint8_t>& pixels, int ax, int ay) {
        // Pink raw meat
        for (int y = 5; y < 12; y++) {
            for (int x = 4; x < 12; x++) {
                uint8_t r = 230, g = 140, b = 140;
                if ((x + y) % 3 == 0) { r = 200; g = 100; b = 100; }  // Fat streaks
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateCookedMeat(std::vector<uint8_t>& pixels, int ax, int ay) {
        // Brown cooked meat
        for (int y = 5; y < 12; y++) {
            for (int x = 4; x < 12; x++) {
                uint8_t r = 150, g = 90, b = 60;
                if ((x + y) % 4 == 0) { r = 180; g = 120; b = 80; }
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateSteak(std::vector<uint8_t>& pixels, int ax, int ay) {
        // T-bone steak shape
        for (int y = 4; y < 13; y++) {
            for (int x = 3; x < 13; x++) {
                if (x == 8 && y > 5 && y < 11) continue;  // Bone gap
                uint8_t r = 140, g = 80, b = 50;
                if ((x + y) % 3 == 0) { r += 20; g += 10; }
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
        // Bone
        for (int y = 6; y < 11; y++) {
            setPixel(pixels, ax, ay, 8, y, 240, 235, 220);
        }
    }

    void generateChicken(std::vector<uint8_t>& pixels, int ax, int ay, bool cooked) {
        // Drumstick shape
        uint8_t r = cooked ? 160 : 255;
        uint8_t g = cooked ? 100 : 200;
        uint8_t b = cooked ? 60 : 180;

        // Leg
        for (int y = 4; y < 10; y++) {
            int width = (y < 7) ? 3 : 2;
            for (int x = 7 - width; x < 7 + width; x++) {
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
        // Bone sticking out
        for (int y = 10; y < 14; y++) {
            setPixel(pixels, ax, ay, 7, y, 240, 235, 220);
        }
    }

    void generateCarrot(std::vector<uint8_t>& pixels, int ax, int ay) {
        // Orange carrot
        for (int y = 4; y < 14; y++) {
            int width = std::max(1, (14 - y) / 2);
            for (int x = 8 - width; x <= 8 + width; x++) {
                setPixel(pixels, ax, ay, x, y, 255, 140, 40);
            }
        }
        // Green top
        setPixel(pixels, ax, ay, 7, 3, 50, 150, 50);
        setPixel(pixels, ax, ay, 8, 2, 50, 150, 50);
        setPixel(pixels, ax, ay, 9, 3, 50, 150, 50);
    }

    void generatePotato(std::vector<uint8_t>& pixels, int ax, int ay, bool baked) {
        uint8_t r = baked ? 180 : 200;
        uint8_t g = baked ? 140 : 170;
        uint8_t b = baked ? 80 : 120;

        for (int y = 5; y < 12; y++) {
            for (int x = 4; x < 12; x++) {
                uint8_t pr = r, pg = g, pb = b;
                pr += ((x * 3 + y * 7) % 20) - 10;
                pg += ((x * 3 + y * 7) % 15) - 7;
                setPixel(pixels, ax, ay, x, y, pr, pg, pb);
            }
        }
    }

    void generateMelon(std::vector<uint8_t>& pixels, int ax, int ay) {
        // Triangular melon slice
        for (int y = 4; y < 13; y++) {
            int width = 13 - y;
            for (int x = 8 - width / 2; x <= 8 + width / 2; x++) {
                uint8_t r = 255, g = 100, b = 100;
                if (y > 10) { r = 100; g = 180; b = 80; }  // Rind
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    void generateCookie(std::vector<uint8_t>& pixels, int ax, int ay) {
        // Round cookie with chocolate chips
        for (int y = 5; y < 11; y++) {
            for (int x = 5; x < 11; x++) {
                setPixel(pixels, ax, ay, x, y, 210, 170, 100);
            }
        }
        // Chocolate chips
        setPixel(pixels, ax, ay, 6, 6, 70, 40, 20);
        setPixel(pixels, ax, ay, 9, 7, 70, 40, 20);
        setPixel(pixels, ax, ay, 7, 9, 70, 40, 20);
    }

    void generateRottenFlesh(std::vector<uint8_t>& pixels, int ax, int ay) {
        // Greenish brown
        for (int y = 4; y < 12; y++) {
            for (int x = 4; x < 12; x++) {
                uint8_t r = 120, g = 100, b = 60;
                if ((x + y) % 4 == 0) { r -= 30; g += 20; }  // Green spots
                setPixel(pixels, ax, ay, x, y, r, g, b);
            }
        }
    }

    // ==================== MISC ====================

    void generateBucket(std::vector<uint8_t>& pixels, int ax, int ay, int contents) {
        // Bucket shape (gray metal)
        for (int y = 4; y < 13; y++) {
            int halfWidth = 2 + (y - 4) / 2;
            for (int x = 8 - halfWidth; x <= 8 + halfWidth; x++) {
                setPixel(pixels, ax, ay, x, y, 180, 180, 180);
            }
        }

        // Fill with contents
        if (contents > 0) {
            uint8_t cr, cg, cb;
            if (contents == 1) { cr = 60; cg = 100; cb = 200; }  // Water
            else { cr = 255; cg = 100; cb = 30; }  // Lava

            for (int y = 6; y < 12; y++) {
                int halfWidth = 1 + (y - 6) / 2;
                for (int x = 8 - halfWidth; x <= 8 + halfWidth; x++) {
                    setPixel(pixels, ax, ay, x, y, cr, cg, cb);
                }
            }
        }

        // Handle
        for (int x = 5; x < 11; x++) {
            setPixel(pixels, ax, ay, x, 3, 150, 150, 150);
        }
    }

    void generateBowl(std::vector<uint8_t>& pixels, int ax, int ay) {
        // Wooden bowl
        for (int y = 8; y < 13; y++) {
            int halfWidth = 2 + (y - 8);
            for (int x = 8 - halfWidth; x <= 8 + halfWidth; x++) {
                setPixel(pixels, ax, ay, x, y, 139, 90, 43);
            }
        }
        // Hollow inside
        for (int y = 9; y < 12; y++) {
            int halfWidth = 1 + (y - 9);
            for (int x = 8 - halfWidth; x <= 8 + halfWidth; x++) {
                setPixel(pixels, ax, ay, x, y, 100, 60, 30);
            }
        }
    }

    void generateMushroomStew(std::vector<uint8_t>& pixels, int ax, int ay) {
        // Bowl with stew
        generateBowl(pixels, ax, ay);

        // Brown stew
        for (int y = 9; y < 12; y++) {
            int halfWidth = 1 + (y - 9);
            for (int x = 8 - halfWidth; x <= 8 + halfWidth; x++) {
                setPixel(pixels, ax, ay, x, y, 140, 100, 70);
            }
        }
        // Mushroom bits
        setPixel(pixels, ax, ay, 7, 10, 200, 50, 50);
        setPixel(pixels, ax, ay, 9, 10, 200, 180, 150);
    }
};
