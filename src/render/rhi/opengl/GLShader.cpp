#include "GLShader.h"
#include "GLDevice.h"
#include <iostream>

namespace RHI {

// ============================================================================
// GL SHADER MODULE
// ============================================================================

GLShaderModule::GLShaderModule(GLDevice* device, const ShaderModuleDesc& desc)
    : m_stage(desc.stage), m_entryPoint(desc.entryPoint) {
    (void)device;

    GLenum glStage = GLDevice::toGLShaderStage(desc.stage);
    if (glStage == 0) {
        std::cerr << "[GLShaderModule] Invalid shader stage" << std::endl;
        return;
    }

    m_shader = glCreateShader(glStage);
    if (m_shader == 0) {
        std::cerr << "[GLShaderModule] Failed to create shader" << std::endl;
        return;
    }

    // Convert code to string (assuming GLSL source)
    std::string source(reinterpret_cast<const char*>(desc.code.data()), desc.code.size());
    const char* sourcePtr = source.c_str();
    glShaderSource(m_shader, 1, &sourcePtr, nullptr);
    glCompileShader(m_shader);

    // Check compilation
    GLint success;
    glGetShaderiv(m_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetShaderInfoLog(m_shader, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "[GLShaderModule] Compilation failed:\n" << infoLog << std::endl;
        glDeleteShader(m_shader);
        m_shader = 0;
        return;
    }

    if (!desc.debugName.empty()) {
        glObjectLabel(GL_SHADER, m_shader, -1, desc.debugName.c_str());
    }
}

GLShaderModule::~GLShaderModule() {
    if (m_shader != 0) {
        glDeleteShader(m_shader);
    }
}

// ============================================================================
// GL SHADER PROGRAM
// ============================================================================

GLShaderProgram::GLShaderProgram(GLDevice* device, const ShaderProgramDesc& desc)
    : m_device(device) {

    m_program = glCreateProgram();
    if (m_program == 0) {
        std::cerr << "[GLShaderProgram] Failed to create program" << std::endl;
        return;
    }

    // Compile and attach each stage
    for (const auto& stage : desc.stages) {
        ShaderModuleDesc moduleDesc;
        moduleDesc.stage = stage.stage;
        moduleDesc.entryPoint = stage.entryPoint;

        if (stage.type == ShaderSourceType::GLSL) {
            moduleDesc.code = std::vector<uint8_t>(stage.source.begin(), stage.source.end());
        } else {
            // TODO: SPIR-V support via GL_ARB_gl_spirv
            std::cerr << "[GLShaderProgram] SPIR-V not yet supported for OpenGL" << std::endl;
            continue;
        }

        auto module = std::make_unique<GLShaderModule>(device, moduleDesc);
        if (!module->isValid()) {
            std::cerr << "[GLShaderProgram] Failed to compile shader stage" << std::endl;
            glDeleteProgram(m_program);
            m_program = 0;
            return;
        }

        glAttachShader(m_program, module->getGLShader());
        m_stageMap[stage.stage] = module.get();
        m_modules.push_back(std::move(module));
    }

    // Link program
    glLinkProgram(m_program);

    GLint success;
    glGetProgramiv(m_program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetProgramInfoLog(m_program, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "[GLShaderProgram] Linking failed:\n" << infoLog << std::endl;
        glDeleteProgram(m_program);
        m_program = 0;
        return;
    }

    // Query reflection data
    queryReflection();

    if (!desc.debugName.empty()) {
        glObjectLabel(GL_PROGRAM, m_program, -1, desc.debugName.c_str());
    }
}

GLShaderProgram::~GLShaderProgram() {
    if (m_program != 0) {
        glDeleteProgram(m_program);
    }
}

RHIShaderModule* GLShaderProgram::getModule(ShaderStage stage) const {
    auto it = m_stageMap.find(stage);
    return it != m_stageMap.end() ? it->second : nullptr;
}

void GLShaderProgram::queryReflection() {
    // Query uniform blocks
    GLint numBlocks;
    glGetProgramiv(m_program, GL_ACTIVE_UNIFORM_BLOCKS, &numBlocks);
    for (GLint i = 0; i < numBlocks; i++) {
        char name[256];
        glGetActiveUniformBlockName(m_program, i, sizeof(name), nullptr, name);
        GLint binding;
        glGetActiveUniformBlockiv(m_program, i, GL_UNIFORM_BLOCK_BINDING, &binding);
        m_uniformBlockBindings[name] = binding;
    }

    // Query shader storage blocks (if available)
    GLint numSSBOs;
    glGetProgramInterfaceiv(m_program, GL_SHADER_STORAGE_BLOCK, GL_ACTIVE_RESOURCES, &numSSBOs);
    for (GLint i = 0; i < numSSBOs; i++) {
        char name[256];
        glGetProgramResourceName(m_program, GL_SHADER_STORAGE_BLOCK, i, sizeof(name), nullptr, name);
        const GLenum props[] = {GL_BUFFER_BINDING};
        GLint binding;
        glGetProgramResourceiv(m_program, GL_SHADER_STORAGE_BLOCK, i, 1, props, 1, nullptr, &binding);
        m_storageBufferBindings[name] = binding;
    }
}

int32_t GLShaderProgram::getUniformBlockBinding(const std::string& name) const {
    auto it = m_uniformBlockBindings.find(name);
    return it != m_uniformBlockBindings.end() ? it->second : -1;
}

int32_t GLShaderProgram::getUniformLocation(const std::string& name) const {
    auto it = m_uniformLocations.find(name);
    if (it != m_uniformLocations.end()) {
        return it->second;
    }
    int32_t location = glGetUniformLocation(m_program, name.c_str());
    m_uniformLocations[name] = location;
    return location;
}

int32_t GLShaderProgram::getStorageBufferBinding(const std::string& name) const {
    auto it = m_storageBufferBindings.find(name);
    return it != m_storageBufferBindings.end() ? it->second : -1;
}

// Uniform setters
void GLShaderProgram::setUniform(int32_t location, int value) {
    glProgramUniform1i(m_program, location, value);
}

void GLShaderProgram::setUniform(int32_t location, float value) {
    glProgramUniform1f(m_program, location, value);
}

void GLShaderProgram::setUniform(int32_t location, const glm::vec2& value) {
    glProgramUniform2fv(m_program, location, 1, &value[0]);
}

void GLShaderProgram::setUniform(int32_t location, const glm::vec3& value) {
    glProgramUniform3fv(m_program, location, 1, &value[0]);
}

void GLShaderProgram::setUniform(int32_t location, const glm::vec4& value) {
    glProgramUniform4fv(m_program, location, 1, &value[0]);
}

void GLShaderProgram::setUniform(int32_t location, const glm::mat3& value) {
    glProgramUniformMatrix3fv(m_program, location, 1, GL_FALSE, &value[0][0]);
}

void GLShaderProgram::setUniform(int32_t location, const glm::mat4& value) {
    glProgramUniformMatrix4fv(m_program, location, 1, GL_FALSE, &value[0][0]);
}

} // namespace RHI
