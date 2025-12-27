#pragma once

#include "../RHIShader.h"
#include <volk.h>
#include <unordered_map>

namespace RHI {

class VKDevice;

// ============================================================================
// VK SHADER MODULE
// ============================================================================

class VKShaderModule : public RHIShaderModule {
public:
    VKShaderModule(VKDevice* device, const ShaderModuleDesc& desc);
    ~VKShaderModule() override;

    ShaderStage getStage() const override { return m_stage; }
    const std::string& getEntryPoint() const override { return m_entryPoint; }
    void* getNativeHandle() const override { return m_module; }

    VkShaderModule getVkModule() const { return m_module; }
    bool isValid() const { return m_module != VK_NULL_HANDLE; }

private:
    VKDevice* m_device = nullptr;
    ShaderStage m_stage = ShaderStage::None;
    std::string m_entryPoint = "main";
    VkShaderModule m_module = VK_NULL_HANDLE;
};

// ============================================================================
// VK SHADER PROGRAM
// ============================================================================
// In Vulkan, shader programs are just containers for modules
// Actual linking happens during pipeline creation

class VKShaderProgram : public RHIShaderProgram {
public:
    VKShaderProgram(VKDevice* device, const ShaderProgramDesc& desc);
    ~VKShaderProgram() override = default;

    void* getNativeHandle() const override { return nullptr; }  // No single handle in Vulkan
    RHIShaderModule* getModule(ShaderStage stage) const override;

    int32_t getUniformBlockBinding(const std::string& name) const override;
    int32_t getUniformLocation(const std::string& name) const override;
    int32_t getStorageBufferBinding(const std::string& name) const override;

    // Get all shader stages for pipeline creation
    std::vector<VkPipelineShaderStageCreateInfo> getShaderStages() const;

    bool isValid() const;

private:
    VKDevice* m_device = nullptr;
    std::vector<std::unique_ptr<VKShaderModule>> m_modules;
    std::unordered_map<ShaderStage, VKShaderModule*> m_stageMap;
};

} // namespace RHI
