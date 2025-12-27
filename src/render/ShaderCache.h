#pragma once

// Shader Binary Caching System
// Uses GL_ARB_get_program_binary to cache compiled shaders to disk
// Reduces startup time by 2-5 seconds on subsequent launches

#include <glad/gl.h>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <filesystem>
#include <functional>

class ShaderCache {
public:
    static std::string cacheDirectory;
    static bool cachingEnabled;
    static bool extensionSupported;

    // Initialize the cache system - call once at startup
    static void init(const std::string& cacheDir = "shader_cache") {
        cacheDirectory = cacheDir;

        // Check if GL_ARB_get_program_binary is supported
        GLint numFormats = 0;
        glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &numFormats);
        extensionSupported = (numFormats > 0);

        if (extensionSupported) {
            // Create cache directory if it doesn't exist
            std::filesystem::create_directories(cacheDirectory);
            cachingEnabled = true;
            printf("[ShaderCache] Initialized with %d binary formats supported\n", numFormats);
        } else {
            cachingEnabled = false;
            printf("[ShaderCache] GL_ARB_get_program_binary not supported - caching disabled\n");
        }
    }

    // Generate a hash for shader source code
    static size_t hashShaderSource(const std::string& vertexSrc, const std::string& fragmentSrc) {
        std::hash<std::string> hasher;
        size_t h1 = hasher(vertexSrc);
        size_t h2 = hasher(fragmentSrc);
        return h1 ^ (h2 << 1);
    }

    // Get cache file path for a shader
    static std::string getCachePath(const std::string& shaderName, size_t hash) {
        return cacheDirectory + "/" + shaderName + "_" + std::to_string(hash) + ".bin";
    }

    // Try to load a cached shader binary
    static bool loadCachedProgram(GLuint program, const std::string& shaderName, size_t hash) {
        if (!cachingEnabled) return false;

        std::string cachePath = getCachePath(shaderName, hash);

        // Check if cache file exists
        if (!std::filesystem::exists(cachePath)) {
            return false;
        }

        // Read the binary file
        std::ifstream file(cachePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return false;

        std::streamsize size = file.tellg();
        if (size < sizeof(GLenum)) return false;

        file.seekg(0, std::ios::beg);

        // Read format
        GLenum binaryFormat;
        file.read(reinterpret_cast<char*>(&binaryFormat), sizeof(GLenum));

        // Read binary data
        std::vector<char> binary(size - sizeof(GLenum));
        file.read(binary.data(), binary.size());
        file.close();

        // Load the binary into the program
        glProgramBinary(program, binaryFormat, binary.data(), static_cast<GLsizei>(binary.size()));

        // Check if loading succeeded
        GLint status;
        glGetProgramiv(program, GL_LINK_STATUS, &status);

        if (status == GL_TRUE) {
            printf("[ShaderCache] Loaded cached binary for '%s'\n", shaderName.c_str());
            return true;
        } else {
            // Cache might be stale (driver update, etc.) - delete it
            std::filesystem::remove(cachePath);
            printf("[ShaderCache] Cached binary for '%s' was stale, removed\n", shaderName.c_str());
            return false;
        }
    }

    // Save a compiled program to cache
    static void saveProgramToCache(GLuint program, const std::string& shaderName, size_t hash) {
        if (!cachingEnabled) return;

        GLint binaryLength;
        glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &binaryLength);

        if (binaryLength <= 0) {
            printf("[ShaderCache] Failed to get binary length for '%s'\n", shaderName.c_str());
            return;
        }

        std::vector<char> binary(binaryLength);
        GLenum binaryFormat;
        glGetProgramBinary(program, binaryLength, nullptr, &binaryFormat, binary.data());

        std::string cachePath = getCachePath(shaderName, hash);
        std::ofstream file(cachePath, std::ios::binary);
        if (!file.is_open()) {
            printf("[ShaderCache] Failed to open cache file for writing: %s\n", cachePath.c_str());
            return;
        }

        // Write format first, then binary data
        file.write(reinterpret_cast<const char*>(&binaryFormat), sizeof(GLenum));
        file.write(binary.data(), binary.size());
        file.close();

        printf("[ShaderCache] Saved binary for '%s' (%d bytes)\n", shaderName.c_str(), binaryLength);
    }

    // Helper to compile a shader with error checking
    static bool compileShader(GLuint shader, const char* source, const std::string& shaderName) {
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);

        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetShaderInfoLog(shader, 512, nullptr, infoLog);
            printf("[ShaderCache] Shader compilation failed for '%s': %s\n", shaderName.c_str(), infoLog);
            return false;
        }
        return true;
    }

    // Create and cache a complete shader program
    // Returns the program ID, or 0 on failure
    static GLuint createCachedProgram(
        const std::string& shaderName,
        const char* vertexSource,
        const char* fragmentSource
    ) {
        GLuint program = glCreateProgram();

        // Enable binary retrieval for this program
        glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);

        // Calculate hash of shader sources
        size_t hash = hashShaderSource(vertexSource, fragmentSource);

        // Try to load from cache first
        if (loadCachedProgram(program, shaderName, hash)) {
            return program;
        }

        // Cache miss - compile shaders
        printf("[ShaderCache] Compiling shader '%s'...\n", shaderName.c_str());

        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);

        if (!compileShader(vertexShader, vertexSource, shaderName + "_vert")) {
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);
            glDeleteProgram(program);
            return 0;
        }

        if (!compileShader(fragmentShader, fragmentSource, shaderName + "_frag")) {
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);
            glDeleteProgram(program);
            return 0;
        }

        // Link program
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);

        GLint success;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetProgramInfoLog(program, 512, nullptr, infoLog);
            printf("[ShaderCache] Program linking failed for '%s': %s\n", shaderName.c_str(), infoLog);
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);
            glDeleteProgram(program);
            return 0;
        }

        // Clean up shaders (no longer needed after linking)
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        // Save to cache for next time
        saveProgramToCache(program, shaderName, hash);

        return program;
    }

    // Clear all cached shaders
    static void clearCache() {
        if (std::filesystem::exists(cacheDirectory)) {
            for (const auto& entry : std::filesystem::directory_iterator(cacheDirectory)) {
                if (entry.path().extension() == ".bin") {
                    std::filesystem::remove(entry.path());
                }
            }
            printf("[ShaderCache] Cache cleared\n");
        }
    }

    // Load shader source from file
    // Returns empty string on failure
    static std::string loadShaderFile(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            printf("[ShaderCache] Failed to open shader file: %s\n", filepath.c_str());
            return "";
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    // Create and cache a shader program from files
    // Returns the program ID, or 0 on failure
    static GLuint createCachedProgramFromFiles(
        const std::string& shaderName,
        const std::string& vertexPath,
        const std::string& fragmentPath
    ) {
        std::string vertexSource = loadShaderFile(vertexPath);
        std::string fragmentSource = loadShaderFile(fragmentPath);

        if (vertexSource.empty() || fragmentSource.empty()) {
            printf("[ShaderCache] Failed to load shader files for '%s'\n", shaderName.c_str());
            return 0;
        }

        return createCachedProgram(shaderName, vertexSource.c_str(), fragmentSource.c_str());
    }

    // Get cache statistics
    static void printCacheStats() {
        if (!std::filesystem::exists(cacheDirectory)) {
            printf("[ShaderCache] No cache directory\n");
            return;
        }

        int fileCount = 0;
        size_t totalSize = 0;

        for (const auto& entry : std::filesystem::directory_iterator(cacheDirectory)) {
            if (entry.path().extension() == ".bin") {
                fileCount++;
                totalSize += entry.file_size();
            }
        }

        printf("[ShaderCache] %d cached shaders, %.2f KB total\n",
               fileCount, totalSize / 1024.0f);
    }
};

// Static member definitions
inline std::string ShaderCache::cacheDirectory = "shader_cache";
inline bool ShaderCache::cachingEnabled = false;
inline bool ShaderCache::extensionSupported = false;
