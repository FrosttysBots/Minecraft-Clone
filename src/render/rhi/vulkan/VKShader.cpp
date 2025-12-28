#include "VKShader.h"
#include "VKDevice.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>

// glslang for GLSL -> SPIR-V compilation
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>

namespace RHI {

// ============================================================================
// SHADER PREPROCESSOR - Converts OpenGL-style uniforms to Vulkan uniform blocks
// ============================================================================

namespace {

// Preprocess GLSL to convert loose uniforms to uniform blocks for Vulkan
// This allows existing OpenGL shaders to work with minimal modification
std::string preprocessGLSLForVulkan(const std::string& source) {
    std::istringstream stream(source);
    std::ostringstream result;
    std::vector<std::string> uniforms;
    std::string line;
    bool foundVersion = false;

    // Regex to match uniform declarations (but not samplers or images)
    // Matches: uniform mat4 name; uniform vec3 name; etc.
    std::regex uniformRegex(R"(^\s*uniform\s+(bool|int|uint|float|double|[biud]?vec[234]|mat[234](?:x[234])?)\s+(\w+)\s*;)");

    // First pass: collect all loose uniforms
    while (std::getline(stream, line)) {
        std::smatch match;
        if (std::regex_search(line, match, uniformRegex)) {
            // This is a loose uniform (non-sampler) - collect it
            uniforms.push_back(line);
        }
    }

    // If no loose uniforms, return source unchanged
    if (uniforms.empty()) {
        return source;
    }

    // Second pass: rewrite the shader
    stream.clear();
    stream.str(source);

    bool insertedBlock = false;

    while (std::getline(stream, line)) {
        std::smatch match;

        // Check for #version directive
        if (line.find("#version") != std::string::npos) {
            result << line << "\n";
            foundVersion = true;
            continue;
        }

        // Insert uniform block after #version and any #extension directives
        if (foundVersion && !insertedBlock &&
            line.find("#extension") == std::string::npos &&
            line.find("#define") == std::string::npos) {

            // Insert the uniform block
            result << "\n// Auto-generated uniform block for Vulkan compatibility\n";
            result << "layout(set = 0, binding = 0) uniform AutoUniforms {\n";

            for (const auto& u : uniforms) {
                // Extract type and name, add to block
                std::smatch m;
                if (std::regex_search(u, m, uniformRegex)) {
                    result << "    " << m[1].str() << " " << m[2].str() << ";\n";
                }
            }

            result << "} _u;\n\n";

            // Add #defines to redirect uniform access
            for (const auto& u : uniforms) {
                std::smatch m;
                if (std::regex_search(u, m, uniformRegex)) {
                    result << "#define " << m[2].str() << " _u." << m[2].str() << "\n";
                }
            }
            result << "\n";

            insertedBlock = true;
        }

        // Skip original loose uniform declarations
        if (std::regex_search(line, match, uniformRegex)) {
            result << "// (moved to uniform block) " << line << "\n";
            continue;
        }

        result << line << "\n";
    }

    return result.str();
}

} // anonymous namespace

// ============================================================================
// GLSL TO SPIR-V COMPILER
// ============================================================================

namespace {

// Thread-safe glslang initialization
class GlslangInitializer {
public:
    GlslangInitializer() {
        glslang::InitializeProcess();
    }
    ~GlslangInitializer() {
        glslang::FinalizeProcess();
    }
    static GlslangInitializer& instance() {
        static GlslangInitializer init;
        return init;
    }
};

EShLanguage toGlslangStage(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:      return EShLangVertex;
        case ShaderStage::Fragment:    return EShLangFragment;
        case ShaderStage::Geometry:    return EShLangGeometry;
        case ShaderStage::TessControl: return EShLangTessControl;
        case ShaderStage::TessEval:    return EShLangTessEvaluation;
        case ShaderStage::Compute:     return EShLangCompute;
        default: return EShLangVertex;
    }
}

} // anonymous namespace

std::vector<uint32_t> compileGLSLToSPIRV(
    const std::string& source,
    ShaderStage stage,
    const std::string& filename)
{
    // Ensure glslang is initialized
    GlslangInitializer::instance();

    // Preprocess shader to convert OpenGL-style uniforms to Vulkan uniform blocks
    std::string processedSource = preprocessGLSLForVulkan(source);

    EShLanguage glslangStage = toGlslangStage(stage);
    glslang::TShader shader(glslangStage);

    const char* sources[] = { processedSource.c_str() };
    const int lengths[] = { static_cast<int>(processedSource.length()) };
    const char* names[] = { filename.c_str() };
    shader.setStringsWithLengthsAndNames(sources, lengths, names, 1);

    // Target Vulkan 1.2 with SPIR-V 1.5
    shader.setEnvInput(glslang::EShSourceGlsl, glslangStage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);

    // Use default resource limits
    const TBuiltInResource* resources = GetDefaultResources();

    // Parse
    EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(resources, 100, false, messages)) {
        std::cerr << "[VKShader] GLSL parse error in " << filename << ":\n";
        std::cerr << shader.getInfoLog() << std::endl;
        std::cerr << shader.getInfoDebugLog() << std::endl;
        return {};
    }

    // Link
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        std::cerr << "[VKShader] GLSL link error in " << filename << ":\n";
        std::cerr << program.getInfoLog() << std::endl;
        return {};
    }

    // Convert to SPIR-V
    std::vector<uint32_t> spirv;
    spv::SpvBuildLogger logger;
    glslang::SpvOptions spvOptions;
    spvOptions.generateDebugInfo = true;
    spvOptions.disableOptimizer = false;
    spvOptions.optimizeSize = false;

    glslang::GlslangToSpv(*program.getIntermediate(glslangStage), spirv, &logger, &spvOptions);

    if (spirv.empty()) {
        std::cerr << "[VKShader] SPIR-V generation failed for " << filename << std::endl;
        return {};
    }

    std::cout << "[VKShader] Compiled " << filename << " (" << spirv.size() * 4 << " bytes)" << std::endl;
    return spirv;
}

// ============================================================================
// VK SHADER MODULE
// ============================================================================

VKShaderModule::VKShaderModule(VKDevice* device, const ShaderModuleDesc& desc)
    : m_device(device), m_stage(desc.stage), m_entryPoint(desc.entryPoint) {

    // SPIR-V code should be provided as binary data
    if (desc.code.empty()) {
        std::cerr << "[VKShaderModule] No shader code provided" << std::endl;
        return;
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = desc.code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(desc.code.data());

    VkResult result = vkCreateShaderModule(m_device->getDevice(), &createInfo, nullptr, &m_module);

    if (result != VK_SUCCESS) {
        std::cerr << "[VKShaderModule] Failed to create shader module" << std::endl;
        return;
    }

    // Set debug name (only if debug utils extension is available)
    if (!desc.debugName.empty() && vkSetDebugUtilsObjectNameEXT) {
        VkDebugUtilsObjectNameInfoEXT nameInfo{};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_SHADER_MODULE;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_module);
        nameInfo.pObjectName = desc.debugName.c_str();
        vkSetDebugUtilsObjectNameEXT(m_device->getDevice(), &nameInfo);
    }
}

VKShaderModule::~VKShaderModule() {
    if (m_module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(m_device->getDevice(), m_module, nullptr);
    }
}

// ============================================================================
// VK SHADER PROGRAM
// ============================================================================

VKShaderProgram::VKShaderProgram(VKDevice* device, const ShaderProgramDesc& desc)
    : m_device(device) {

    // Create modules from shader sources
    for (const auto& source : desc.stages) {
        if (source.stage == ShaderStage::None) continue;

        ShaderModuleDesc moduleDesc{};
        moduleDesc.stage = source.stage;
        moduleDesc.entryPoint = source.entryPoint.empty() ? "main" : source.entryPoint;
        moduleDesc.debugName = desc.debugName + "_" + std::to_string(static_cast<int>(source.stage));

        // Use SPIR-V bytecode if available, otherwise compile from GLSL source
        if (!source.spirv.empty()) {
            moduleDesc.code = source.spirv;
        } else if (!source.source.empty()) {
            // Compile GLSL to SPIR-V using glslang
            std::vector<uint32_t> spirv = compileGLSLToSPIRV(
                source.source,
                source.stage,
                moduleDesc.debugName
            );
            if (spirv.empty()) {
                std::cerr << "[VKShaderProgram] Failed to compile shader: " << moduleDesc.debugName << std::endl;
                continue;
            }
            // Convert uint32_t vector to byte vector
            moduleDesc.code.resize(spirv.size() * sizeof(uint32_t));
            memcpy(moduleDesc.code.data(), spirv.data(), moduleDesc.code.size());
        } else {
            continue;
        }

        auto module = std::make_unique<VKShaderModule>(m_device, moduleDesc);
        if (module->isValid()) {
            m_stageMap[source.stage] = module.get();
            m_modules.push_back(std::move(module));
        }
    }
}

RHIShaderModule* VKShaderProgram::getModule(ShaderStage stage) const {
    auto it = m_stageMap.find(stage);
    if (it != m_stageMap.end()) {
        return it->second;
    }
    return nullptr;
}

int32_t VKShaderProgram::getUniformBlockBinding(const std::string& name) const {
    // In Vulkan, bindings are specified in the shader via layout qualifiers
    // No runtime query is available; bindings must be known at compile time
    // Return -1 to indicate this isn't available
    (void)name;
    return -1;
}

int32_t VKShaderProgram::getUniformLocation(const std::string& name) const {
    // Vulkan doesn't have uniform locations like OpenGL
    // Uniforms are accessed via descriptor sets
    (void)name;
    return -1;
}

int32_t VKShaderProgram::getStorageBufferBinding(const std::string& name) const {
    // Same as uniform blocks - bindings are compile-time in Vulkan
    (void)name;
    return -1;
}

std::vector<VkPipelineShaderStageCreateInfo> VKShaderProgram::getShaderStages() const {
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    stages.reserve(m_modules.size());

    for (const auto& module : m_modules) {
        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VKDevice::toVkShaderStage(module->getStage());
        stageInfo.module = module->getVkModule();
        stageInfo.pName = module->getEntryPoint().c_str();
        stageInfo.pSpecializationInfo = nullptr;  // Could add specialization constants

        stages.push_back(stageInfo);
    }

    return stages;
}

bool VKShaderProgram::isValid() const {
    // At minimum, need vertex and fragment for graphics, or just compute
    bool hasVertex = m_stageMap.find(ShaderStage::Vertex) != m_stageMap.end();
    bool hasFragment = m_stageMap.find(ShaderStage::Fragment) != m_stageMap.end();
    bool hasCompute = m_stageMap.find(ShaderStage::Compute) != m_stageMap.end();

    return (hasVertex && hasFragment) || hasCompute;
}

} // namespace RHI
