#include "VKShader.h"
#include "VKDevice.h"
#include <iostream>
#include <fstream>

namespace RHI {

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

    // Set debug name
    if (!desc.debugName.empty()) {
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

        // Use SPIR-V bytecode if available, otherwise convert from source
        if (!source.spirv.empty()) {
            moduleDesc.code = source.spirv;
        } else if (!source.source.empty()) {
            // For Vulkan, we expect SPIR-V; GLSL source would need compilation
            std::cerr << "[VKShaderProgram] GLSL source not supported, use SPIR-V" << std::endl;
            continue;
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
