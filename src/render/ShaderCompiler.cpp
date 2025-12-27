#include "ShaderCompiler.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <regex>
#include <functional>

// glslang headers
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>

namespace Render {

bool ShaderCompiler::s_initialized = false;

ShaderCompiler::ShaderCompiler() {
    if (!s_initialized) {
        initialize();
    }
}

ShaderCompiler::~ShaderCompiler() {
    // Don't shutdown here - let the application control lifetime
}

bool ShaderCompiler::initialize() {
    if (s_initialized) return true;

    s_initialized = glslang::InitializeProcess();
    if (!s_initialized) {
        std::cerr << "[ShaderCompiler] Failed to initialize glslang" << std::endl;
    }
    return s_initialized;
}

void ShaderCompiler::shutdown() {
    if (s_initialized) {
        glslang::FinalizeProcess();
        s_initialized = false;
    }
}

// Convert our stage enum to glslang stage
static EShLanguage toGlslangStage(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:         return EShLangVertex;
        case ShaderStage::Fragment:       return EShLangFragment;
        case ShaderStage::Compute:        return EShLangCompute;
        case ShaderStage::Geometry:       return EShLangGeometry;
        case ShaderStage::TessControl:    return EShLangTessControl;
        case ShaderStage::TessEvaluation: return EShLangTessEvaluation;
        case ShaderStage::Task:           return EShLangTask;
        case ShaderStage::Mesh:           return EShLangMesh;
        default:                          return EShLangVertex;
    }
}

std::optional<CompiledShader> ShaderCompiler::compile(
    const std::string& source,
    ShaderStage stage,
    const ShaderCompileOptions& options,
    const std::string& debugName
) {
    if (!s_initialized) {
        m_lastError = "ShaderCompiler not initialized";
        return std::nullopt;
    }

    EShLanguage glslangStage = toGlslangStage(stage);
    glslang::TShader shader(glslangStage);

    // Set up source
    const char* sourceCStr = source.c_str();
    const char* sourceNames = debugName.empty() ? "shader" : debugName.c_str();
    shader.setStringsWithLengthsAndNames(&sourceCStr, nullptr, &sourceNames, 1);

    // Set version and profile
    int version = options.glslVersion;
    EProfile profile = ECoreProfile;

    // Set up environment
    shader.setEnvInput(glslang::EShSourceGlsl, glslangStage,
                       options.vulkanSemantics ? glslang::EShClientVulkan : glslang::EShClientOpenGL,
                       100);

    if (options.vulkanSemantics) {
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
    } else {
        shader.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
    }

    // Add preprocessor defines
    std::string preamble;
    for (const auto& define : options.defines) {
        preamble += "#define " + define + "\n";
    }
    if (!preamble.empty()) {
        shader.setPreamble(preamble.c_str());
    }

    // Parse
    EShMessages messages = static_cast<EShMessages>(
        EShMsgSpvRules | EShMsgVulkanRules | EShMsgDefault
    );

    if (options.generateDebugInfo) {
        messages = static_cast<EShMessages>(messages | EShMsgDebugInfo);
    }

    const TBuiltInResource* resources = GetDefaultResources();

    if (!shader.parse(resources, version, profile, false, false, messages)) {
        m_lastError = "Shader parse failed:\n";
        m_lastError += shader.getInfoLog();
        m_lastError += shader.getInfoDebugLog();
        std::cerr << "[ShaderCompiler] " << m_lastError << std::endl;
        return std::nullopt;
    }

    // Link
    glslang::TProgram program;
    program.addShader(&shader);

    if (!program.link(messages)) {
        m_lastError = "Shader link failed:\n";
        m_lastError += program.getInfoLog();
        m_lastError += program.getInfoDebugLog();
        std::cerr << "[ShaderCompiler] " << m_lastError << std::endl;
        return std::nullopt;
    }

    // Generate SPIR-V
    std::vector<uint32_t> spirv;
    spv::SpvBuildLogger logger;
    glslang::SpvOptions spvOptions;
    spvOptions.generateDebugInfo = options.generateDebugInfo;
    spvOptions.stripDebugInfo = false;
    spvOptions.disableOptimizer = !options.optimizePerformance;
    spvOptions.optimizeSize = options.optimizeSize;

    glslang::GlslangToSpv(*program.getIntermediate(glslangStage), spirv, &logger, &spvOptions);

    std::string messages_str = logger.getAllMessages();
    if (!messages_str.empty()) {
        std::cout << "[ShaderCompiler] SPIR-V messages: " << messages_str << std::endl;
    }

    if (spirv.empty()) {
        m_lastError = "SPIR-V generation produced no output";
        return std::nullopt;
    }

    // Build result
    CompiledShader result;
    result.spirv = std::move(spirv);
    result.glslSource = source;
    result.stage = stage;
    result.entryPoint = "main";
    result.sourceHash = computeHash(source, options);

    return result;
}

std::optional<CompiledShader> ShaderCompiler::compileFile(
    const std::filesystem::path& path,
    ShaderStage stage,
    const ShaderCompileOptions& options
) {
    auto source = loadSource(path, options.includePaths);
    if (!source) {
        return std::nullopt;
    }

    return compile(*source, stage, options, path.string());
}

std::optional<CompiledShader> ShaderCompiler::loadShader(
    const std::filesystem::path& glslPath,
    ShaderStage stage,
    const ShaderCompileOptions& options
) {
    // Load source first to compute hash
    auto source = loadSource(glslPath, options.includePaths);
    if (!source) {
        return std::nullopt;
    }

    uint64_t hash = computeHash(*source, options);
    auto cachePath = getCachePath(glslPath, hash);

    // Try to load from cache
    if (std::filesystem::exists(cachePath)) {
        auto cachedSpirv = loadCached(cachePath);
        if (cachedSpirv) {
            CompiledShader result;
            result.spirv = std::move(*cachedSpirv);
            result.glslSource = *source;
            result.stage = stage;
            result.entryPoint = "main";
            result.sourceHash = hash;
            return result;
        }
    }

    // Compile fresh
    auto result = compile(*source, stage, options, glslPath.string());
    if (result) {
        // Save to cache
        saveToCache(cachePath, result->spirv);
    }

    return result;
}

void ShaderCompiler::setCacheDirectory(const std::filesystem::path& dir) {
    m_cacheDir = dir;
    std::filesystem::create_directories(m_cacheDir);
}

void ShaderCompiler::clearCache() {
    if (std::filesystem::exists(m_cacheDir)) {
        std::filesystem::remove_all(m_cacheDir);
    }
    std::filesystem::create_directories(m_cacheDir);
}

bool ShaderCompiler::isCached(const std::filesystem::path& glslPath) const {
    // Would need to compute hash to check properly
    // For now, just check if any .spv file exists with similar name
    auto stem = glslPath.stem().string();
    for (const auto& entry : std::filesystem::directory_iterator(m_cacheDir)) {
        if (entry.path().stem().string().find(stem) != std::string::npos &&
            entry.path().extension() == ".spv") {
            return true;
        }
    }
    return false;
}

ShaderStage ShaderCompiler::stageFromExtension(const std::string& ext) {
    if (ext == ".vert" || ext == ".vs" || ext == ".vertex") return ShaderStage::Vertex;
    if (ext == ".frag" || ext == ".fs" || ext == ".fragment") return ShaderStage::Fragment;
    if (ext == ".comp" || ext == ".compute") return ShaderStage::Compute;
    if (ext == ".geom" || ext == ".geometry") return ShaderStage::Geometry;
    if (ext == ".tesc" || ext == ".tesscontrol") return ShaderStage::TessControl;
    if (ext == ".tese" || ext == ".tesseval") return ShaderStage::TessEvaluation;
    if (ext == ".task") return ShaderStage::Task;
    if (ext == ".mesh") return ShaderStage::Mesh;
    return ShaderStage::Vertex; // Default
}

std::string ShaderCompiler::extensionForStage(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:         return ".vert";
        case ShaderStage::Fragment:       return ".frag";
        case ShaderStage::Compute:        return ".comp";
        case ShaderStage::Geometry:       return ".geom";
        case ShaderStage::TessControl:    return ".tesc";
        case ShaderStage::TessEvaluation: return ".tese";
        case ShaderStage::Task:           return ".task";
        case ShaderStage::Mesh:           return ".mesh";
        default:                          return ".glsl";
    }
}

std::optional<std::string> ShaderCompiler::loadSource(
    const std::filesystem::path& path,
    const std::vector<std::string>& includePaths
) {
    std::ifstream file(path);
    if (!file.is_open()) {
        m_lastError = "Failed to open shader file: " + path.string();
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    // Process includes
    std::vector<std::filesystem::path> includedFiles;
    return processIncludes(source, path.parent_path(), includePaths, includedFiles);
}

std::optional<std::string> ShaderCompiler::processIncludes(
    const std::string& source,
    const std::filesystem::path& basePath,
    const std::vector<std::string>& includePaths,
    std::vector<std::filesystem::path>& includedFiles
) {
    std::string result;
    std::istringstream stream(source);
    std::string line;
    std::regex includeRegex(R"(^\s*#include\s+[<"]([^>"]+)[>"])");

    while (std::getline(stream, line)) {
        std::smatch match;
        if (std::regex_match(line, match, includeRegex)) {
            std::string includeName = match[1].str();

            // Search for include file
            std::filesystem::path includePath;
            bool found = false;

            // Check relative to current file
            auto relPath = basePath / includeName;
            if (std::filesystem::exists(relPath)) {
                includePath = relPath;
                found = true;
            }

            // Check include paths
            if (!found) {
                for (const auto& dir : includePaths) {
                    auto path = std::filesystem::path(dir) / includeName;
                    if (std::filesystem::exists(path)) {
                        includePath = path;
                        found = true;
                        break;
                    }
                }
            }

            if (!found) {
                m_lastError = "Include file not found: " + includeName;
                return std::nullopt;
            }

            // Prevent circular includes
            auto canonical = std::filesystem::canonical(includePath);
            for (const auto& included : includedFiles) {
                if (std::filesystem::equivalent(canonical, included)) {
                    continue; // Skip already included files
                }
            }
            includedFiles.push_back(canonical);

            // Load and process included file
            std::ifstream includeFile(includePath);
            if (!includeFile.is_open()) {
                m_lastError = "Failed to open include file: " + includePath.string();
                return std::nullopt;
            }

            std::stringstream includeBuffer;
            includeBuffer << includeFile.rdbuf();

            auto processedInclude = processIncludes(
                includeBuffer.str(),
                includePath.parent_path(),
                includePaths,
                includedFiles
            );

            if (!processedInclude) {
                return std::nullopt;
            }

            result += *processedInclude + "\n";
        } else {
            result += line + "\n";
        }
    }

    return result;
}

uint64_t ShaderCompiler::computeHash(const std::string& source, const ShaderCompileOptions& options) {
    std::hash<std::string> hasher;
    uint64_t hash = hasher(source);

    // Include options in hash
    hash ^= std::hash<int>{}(options.glslVersion) << 1;
    hash ^= std::hash<bool>{}(options.vulkanSemantics) << 2;
    hash ^= std::hash<bool>{}(options.optimizePerformance) << 3;
    hash ^= std::hash<bool>{}(options.optimizeSize) << 4;

    for (const auto& define : options.defines) {
        hash ^= hasher(define) << 5;
    }

    return hash;
}

std::filesystem::path ShaderCompiler::getCachePath(
    const std::filesystem::path& sourcePath,
    uint64_t hash
) const {
    std::string filename = sourcePath.stem().string();
    filename += "_" + std::to_string(hash) + ".spv";
    return m_cacheDir / filename;
}

std::optional<std::vector<uint32_t>> ShaderCompiler::loadCached(
    const std::filesystem::path& cachePath
) const {
    std::ifstream file(cachePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return std::nullopt;
    }

    auto size = file.tellg();
    if (size <= 0 || size % 4 != 0) {
        return std::nullopt;
    }

    file.seekg(0, std::ios::beg);
    std::vector<uint32_t> spirv(size / 4);
    file.read(reinterpret_cast<char*>(spirv.data()), size);

    // Basic validation - SPIR-V magic number
    if (spirv.empty() || spirv[0] != 0x07230203) {
        return std::nullopt;
    }

    return spirv;
}

bool ShaderCompiler::saveToCache(
    const std::filesystem::path& cachePath,
    const std::vector<uint32_t>& spirv
) const {
    std::filesystem::create_directories(cachePath.parent_path());

    std::ofstream file(cachePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    file.write(reinterpret_cast<const char*>(spirv.data()), spirv.size() * sizeof(uint32_t));
    return file.good();
}

// Utility function implementation
std::unordered_map<ShaderStage, CompiledShader> loadShaderProgram(
    ShaderCompiler& compiler,
    const ShaderProgramDesc& desc
) {
    std::unordered_map<ShaderStage, CompiledShader> shaders;

    auto tryLoad = [&](const std::filesystem::path& path, ShaderStage stage) {
        if (!path.empty() && std::filesystem::exists(path)) {
            auto shader = compiler.loadShader(path, stage, desc.options);
            if (shader) {
                shaders[stage] = std::move(*shader);
            } else {
                std::cerr << "[ShaderCompiler] Failed to load " << path << ": "
                          << compiler.getLastError() << std::endl;
            }
        }
    };

    tryLoad(desc.vertexPath, ShaderStage::Vertex);
    tryLoad(desc.fragmentPath, ShaderStage::Fragment);
    tryLoad(desc.geometryPath, ShaderStage::Geometry);
    tryLoad(desc.computePath, ShaderStage::Compute);
    tryLoad(desc.taskPath, ShaderStage::Task);
    tryLoad(desc.meshPath, ShaderStage::Mesh);

    return shaders;
}

} // namespace Render
