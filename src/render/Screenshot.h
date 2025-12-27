#pragma once

// Screenshot capture utility for world thumbnails
// Uses stb_image_write for PNG output

#include <glad/gl.h>
#include <string>
#include <vector>
#include <iostream>

// stb_image headers (implementation in stb_impl.cpp)
#include "stb_image.h"
#include "stb_image_write.h"

class Screenshot {
public:
    // Capture current framebuffer and save as PNG
    // Returns true on success
    static bool capture(const std::string& filepath, int width, int height) {
        // Allocate buffer for pixels (RGBA)
        std::vector<unsigned char> pixels(width * height * 4);

        // Read pixels from framebuffer
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        // Check for OpenGL errors
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            std::cerr << "OpenGL error during screenshot capture: " << err << std::endl;
            return false;
        }

        // Flip image vertically (OpenGL reads bottom-to-top)
        std::vector<unsigned char> flipped(width * height * 4);
        for (int y = 0; y < height; y++) {
            memcpy(&flipped[y * width * 4],
                   &pixels[(height - 1 - y) * width * 4],
                   width * 4);
        }

        // Save as PNG
        int result = stbi_write_png(filepath.c_str(), width, height, 4, flipped.data(), width * 4);

        if (result == 0) {
            std::cerr << "Failed to write screenshot to: " << filepath << std::endl;
            return false;
        }

        return true;
    }

    // Capture and resize to thumbnail size
    static bool captureThumbnail(const std::string& filepath, int srcWidth, int srcHeight,
                                  int thumbWidth = 160, int thumbHeight = 90) {
        // Allocate buffer for full-size pixels (RGB - we'll use RGB for smaller files)
        std::vector<unsigned char> pixels(srcWidth * srcHeight * 3);

        // Read pixels from framebuffer (RGB)
        glReadPixels(0, 0, srcWidth, srcHeight, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

        // Check for OpenGL errors
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            std::cerr << "OpenGL error during thumbnail capture: " << err << std::endl;
            return false;
        }

        // Simple box filter downscale
        std::vector<unsigned char> thumbnail(thumbWidth * thumbHeight * 3);

        float scaleX = static_cast<float>(srcWidth) / thumbWidth;
        float scaleY = static_cast<float>(srcHeight) / thumbHeight;

        for (int ty = 0; ty < thumbHeight; ty++) {
            for (int tx = 0; tx < thumbWidth; tx++) {
                // Calculate source region
                int srcX0 = static_cast<int>(tx * scaleX);
                int srcY0 = static_cast<int>(ty * scaleY);
                int srcX1 = static_cast<int>((tx + 1) * scaleX);
                int srcY1 = static_cast<int>((ty + 1) * scaleY);

                // Average pixels in region
                int r = 0, g = 0, b = 0, count = 0;
                for (int sy = srcY0; sy < srcY1 && sy < srcHeight; sy++) {
                    for (int sx = srcX0; sx < srcX1 && sx < srcWidth; sx++) {
                        // Flip Y coordinate (OpenGL is bottom-up)
                        int flippedY = srcHeight - 1 - sy;
                        int idx = (flippedY * srcWidth + sx) * 3;
                        r += pixels[idx];
                        g += pixels[idx + 1];
                        b += pixels[idx + 2];
                        count++;
                    }
                }

                if (count > 0) {
                    int thumbIdx = (ty * thumbWidth + tx) * 3;
                    thumbnail[thumbIdx] = static_cast<unsigned char>(r / count);
                    thumbnail[thumbIdx + 1] = static_cast<unsigned char>(g / count);
                    thumbnail[thumbIdx + 2] = static_cast<unsigned char>(b / count);
                }
            }
        }

        // Save as PNG
        int result = stbi_write_png(filepath.c_str(), thumbWidth, thumbHeight, 3,
                                    thumbnail.data(), thumbWidth * 3);

        if (result == 0) {
            std::cerr << "Failed to write thumbnail to: " << filepath << std::endl;
            return false;
        }

        return true;
    }

    // Load a thumbnail image as an OpenGL texture
    static GLuint loadThumbnailTexture(const std::string& filepath) {
        int width, height, channels;
        unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels, 3);

        if (!data) {
            return 0;  // Failed to load
        }

        GLuint textureID;
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);

        // Set texture parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Upload texture data
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);

        stbi_image_free(data);

        return textureID;
    }
};
