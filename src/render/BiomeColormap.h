#pragma once

// Biome Colormap Generator
// Creates color lookup textures for grass, foliage, and water based on temperature/humidity
// Similar to Minecraft's grass.png and foliage.png colormaps

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <vector>
#include <cmath>
#include <iostream>

// Colormap size (256x256 - temperature on X, humidity on Y)
constexpr int COLORMAP_SIZE = 256;

class BiomeColormap {
public:
    GLuint grassColormap = 0;    // Grass/foliage color lookup
    GLuint waterColormap = 0;    // Water color lookup (optional)

    void generate() {
        generateGrassColormap();
        generateWaterColormap();
        std::cout << "[BiomeColormap] Generated grass and water colormaps ("
                  << COLORMAP_SIZE << "x" << COLORMAP_SIZE << ")" << std::endl;
    }

    void bind(GLuint grassUnit = 4, GLuint waterUnit = 5) const {
        glActiveTexture(GL_TEXTURE0 + grassUnit);
        glBindTexture(GL_TEXTURE_2D, grassColormap);
        glActiveTexture(GL_TEXTURE0 + waterUnit);
        glBindTexture(GL_TEXTURE_2D, waterColormap);
    }

    void destroy() {
        if (grassColormap) {
            glDeleteTextures(1, &grassColormap);
            grassColormap = 0;
        }
        if (waterColormap) {
            glDeleteTextures(1, &waterColormap);
            waterColormap = 0;
        }
    }

    // Get grass color for a given temperature and humidity (0-1 range)
    static glm::vec3 getGrassColor(float temperature, float humidity) {
        // Clamp inputs
        temperature = glm::clamp(temperature, 0.0f, 1.0f);
        humidity = glm::clamp(humidity, 0.0f, 1.0f);

        // Minecraft-style triangular colormap
        // Hot+Dry = Yellow/Brown, Cold+Wet = Dark Green, Hot+Wet = Bright Green

        // Base green color
        glm::vec3 coldDry(0.55f, 0.65f, 0.45f);   // Grayish green (taiga/mountains)
        glm::vec3 coldWet(0.30f, 0.50f, 0.25f);   // Dark green (swamp/cold forest)
        glm::vec3 hotDry(0.75f, 0.70f, 0.35f);    // Yellow/brown (savanna/desert edge)
        glm::vec3 hotWet(0.45f, 0.75f, 0.30f);    // Bright green (jungle/tropical)

        // Bilinear interpolation
        glm::vec3 coldMix = glm::mix(coldDry, coldWet, humidity);
        glm::vec3 hotMix = glm::mix(hotDry, hotWet, humidity);
        return glm::mix(coldMix, hotMix, temperature);
    }

    // Get water color for a given temperature (0-1 range)
    static glm::vec3 getWaterColor(float temperature, float humidity) {
        temperature = glm::clamp(temperature, 0.0f, 1.0f);
        humidity = glm::clamp(humidity, 0.0f, 1.0f);

        // Water colors: cold = deep blue, warm = turquoise, swamp = murky green
        glm::vec3 coldWater(0.15f, 0.25f, 0.55f);    // Deep blue (cold ocean)
        glm::vec3 warmWater(0.20f, 0.50f, 0.60f);    // Turquoise (tropical)
        glm::vec3 swampWater(0.25f, 0.35f, 0.25f);   // Murky green (swamp)

        glm::vec3 baseMix = glm::mix(coldWater, warmWater, temperature);
        // Add swamp influence at high humidity
        return glm::mix(baseMix, swampWater, humidity * 0.5f);
    }

private:
    void generateGrassColormap() {
        std::vector<uint8_t> pixels(COLORMAP_SIZE * COLORMAP_SIZE * 3);

        for (int y = 0; y < COLORMAP_SIZE; y++) {
            for (int x = 0; x < COLORMAP_SIZE; x++) {
                float temperature = static_cast<float>(x) / (COLORMAP_SIZE - 1);
                float humidity = static_cast<float>(y) / (COLORMAP_SIZE - 1);

                glm::vec3 color = getGrassColor(temperature, humidity);

                int idx = (y * COLORMAP_SIZE + x) * 3;
                pixels[idx + 0] = static_cast<uint8_t>(color.r * 255);
                pixels[idx + 1] = static_cast<uint8_t>(color.g * 255);
                pixels[idx + 2] = static_cast<uint8_t>(color.b * 255);
            }
        }

        glGenTextures(1, &grassColormap);
        glBindTexture(GL_TEXTURE_2D, grassColormap);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, COLORMAP_SIZE, COLORMAP_SIZE,
                     0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    void generateWaterColormap() {
        std::vector<uint8_t> pixels(COLORMAP_SIZE * COLORMAP_SIZE * 3);

        for (int y = 0; y < COLORMAP_SIZE; y++) {
            for (int x = 0; x < COLORMAP_SIZE; x++) {
                float temperature = static_cast<float>(x) / (COLORMAP_SIZE - 1);
                float humidity = static_cast<float>(y) / (COLORMAP_SIZE - 1);

                glm::vec3 color = getWaterColor(temperature, humidity);

                int idx = (y * COLORMAP_SIZE + x) * 3;
                pixels[idx + 0] = static_cast<uint8_t>(color.r * 255);
                pixels[idx + 1] = static_cast<uint8_t>(color.g * 255);
                pixels[idx + 2] = static_cast<uint8_t>(color.b * 255);
            }
        }

        glGenTextures(1, &waterColormap);
        glBindTexture(GL_TEXTURE_2D, waterColormap);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, COLORMAP_SIZE, COLORMAP_SIZE,
                     0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
};

// Tintable texture slots (textures that should be multiplied by biome color)
namespace TintableSlots {
    constexpr int GRASS_TOP = 2;     // grass_top
    constexpr int GRASS_SIDE = 3;    // grass_side (only top portion is tinted in shader)
    constexpr int LEAVES = 8;        // leaves_oak
    constexpr int WATER = 11;        // water_still

    inline bool isTintable(int slot) {
        return slot == GRASS_TOP || slot == LEAVES;
    }

    inline bool isGrassSide(int slot) {
        return slot == GRASS_SIDE;
    }

    inline bool isWater(int slot) {
        return slot == WATER;
    }
}
