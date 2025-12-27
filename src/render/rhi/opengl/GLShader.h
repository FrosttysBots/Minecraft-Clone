#pragma once

#include "../RHIShader.h"
#include <glad/gl.h>
#include <unordered_map>

namespace RHI {

class GLDevice;

// ============================================================================
// GL SHADER MODULE
// ============================================================================

class GLShaderModule : public RHIShaderModule {
public:
    GLShaderModule(GLDevice* device, const ShaderModuleDesc& desc);
    ~GLShaderModule() override;

    ShaderStage getStage() const override { return m_stage; }
    const std::string& getEntryPoint() const override { return m_entryPoint; }
    void* getNativeHandle() const override { return reinterpret_cast<void*>(static_cast<uintptr_t>(m_shader)); }

    GLuint getGLShader() const { return m_shader; }
    bool isValid() const { return m_shader != 0; }

private:
    ShaderStage m_stage = ShaderStage::None;
    std::string m_entryPoint = "main";
    GLuint m_shader = 0;
};

// ============================================================================
// GL SHADER PROGRAM
// ============================================================================

class GLShaderProgram : public RHIShaderProgram {
public:
    GLShaderProgram(GLDevice* device, const ShaderProgramDesc& desc);
    ~GLShaderProgram() override;

    void* getNativeHandle() const override { return reinterpret_cast<void*>(static_cast<uintptr_t>(m_program)); }
    RHIShaderModule* getModule(ShaderStage stage) const override;

    int32_t getUniformBlockBinding(const std::string& name) const override;
    int32_t getUniformLocation(const std::string& name) const override;
    int32_t getStorageBufferBinding(const std::string& name) const override;

    GLuint getGLProgram() const { return m_program; }
    bool isValid() const { return m_program != 0; }

    // Set uniform by location (for immediate mode)
    void setUniform(int32_t location, int value);
    void setUniform(int32_t location, float value);
    void setUniform(int32_t location, const glm::vec2& value);
    void setUniform(int32_t location, const glm::vec3& value);
    void setUniform(int32_t location, const glm::vec4& value);
    void setUniform(int32_t location, const glm::mat3& value);
    void setUniform(int32_t location, const glm::mat4& value);

private:
    void queryReflection();

    GLDevice* m_device = nullptr;
    GLuint m_program = 0;
    std::vector<std::unique_ptr<GLShaderModule>> m_modules;
    std::unordered_map<ShaderStage, GLShaderModule*> m_stageMap;

    // Reflection data
    mutable std::unordered_map<std::string, int32_t> m_uniformLocations;
    std::unordered_map<std::string, int32_t> m_uniformBlockBindings;
    std::unordered_map<std::string, int32_t> m_storageBufferBindings;
};

} // namespace RHI
