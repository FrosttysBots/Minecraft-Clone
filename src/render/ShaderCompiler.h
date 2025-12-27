#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <optional>

namespace Render {

// Shader stage types
enum class ShaderStage {
    Vertex,
    Fragment,
    Compute,
    Geometry,
    TessControl,
    TessEvaluation,
    Task,      // NV mesh shader extension
    Mesh       // NV mesh shader extension
};

// Compiled shader data
struct CompiledShader {
    std::vector<uint32_t> spirv;    // SPIR-V bytecode
    std::string glslSource;          // Original GLSL source
    ShaderStage stage;
    std::string entryPoint = "main";
    uint64_t sourceHash = 0;         // For cache invalidation
};

// Shader compilation options
struct ShaderCompileOptions {
    bool generateDebugInfo = false;
    bool optimizeSize = false;
    bool optimizePerformance = true;
    std::vector<std::string> defines;           // Preprocessor defines
    std::vector<std::string> includePaths;      // Include search paths
    int glslVersion = 460;                       // GLSL version (e.g., 460)
    bool vulkanSemantics = true;                 // Use Vulkan GLSL semantics
};

// Shader compiler class
// Uses glslang to compile GLSL to SPIR-V with optional disk caching
class ShaderCompiler {
public:
    ShaderCompiler();
    ~ShaderCompiler();

    // Initialize glslang (call once at startup)
    static bool initialize();
    static void shutdown();

    // Compile GLSL source to SPIR-V
    std::optional<CompiledShader> compile(
        const std::string& source,
        ShaderStage stage,
        const ShaderCompileOptions& options = {},
        const std::string& debugName = ""
    );

    // Compile GLSL file to SPIR-V
    std::optional<CompiledShader> compileFile(
        const std::filesystem::path& path,
        ShaderStage stage,
        const ShaderCompileOptions& options = {}
    );

    // Load shader from file (GLSL) or cache (SPIR-V if available)
    std::optional<CompiledShader> loadShader(
        const std::filesystem::path& glslPath,
        ShaderStage stage,
        const ShaderCompileOptions& options = {}
    );

    // Cache management
    void setCacheDirectory(const std::filesystem::path& dir);
    void clearCache();
    bool isCached(const std::filesystem::path& glslPath) const;

    // Get last error message
    const std::string& getLastError() const { return m_lastError; }

    // Utility: detect shader stage from file extension
    static ShaderStage stageFromExtension(const std::string& ext);

    // Utility: get file extension for stage
    static std::string extensionForStage(ShaderStage stage);

private:
    // Load GLSL source with #include handling
    std::optional<std::string> loadSource(
        const std::filesystem::path& path,
        const std::vector<std::string>& includePaths
    );

    // Process #include directives recursively
    std::optional<std::string> processIncludes(
        const std::string& source,
        const std::filesystem::path& basePath,
        const std::vector<std::string>& includePaths,
        std::vector<std::filesystem::path>& includedFiles
    );

    // Compute hash for cache key
    uint64_t computeHash(const std::string& source, const ShaderCompileOptions& options);

    // Cache path for a given source file
    std::filesystem::path getCachePath(const std::filesystem::path& sourcePath, uint64_t hash) const;

    // Load cached SPIR-V
    std::optional<std::vector<uint32_t>> loadCached(const std::filesystem::path& cachePath) const;

    // Save SPIR-V to cache
    bool saveToCache(const std::filesystem::path& cachePath, const std::vector<uint32_t>& spirv) const;

    std::filesystem::path m_cacheDir = "shader_cache";
    std::string m_lastError;
    static bool s_initialized;
};

// Shader program descriptor for loading complete programs
struct ShaderProgramDesc {
    std::filesystem::path vertexPath;
    std::filesystem::path fragmentPath;
    std::filesystem::path geometryPath;      // Optional
    std::filesystem::path computePath;       // For compute shaders
    std::filesystem::path taskPath;          // For mesh shader pipeline
    std::filesystem::path meshPath;          // For mesh shader pipeline
    ShaderCompileOptions options;
};

// Utility function to load all shaders for a program
std::unordered_map<ShaderStage, CompiledShader> loadShaderProgram(
    ShaderCompiler& compiler,
    const ShaderProgramDesc& desc
);

} // namespace Render
