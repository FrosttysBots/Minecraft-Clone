#pragma once

#include "RHITypes.h"
#include <unordered_map>

namespace RHI {

// ============================================================================
// RHI SHADER MODULE
// ============================================================================
// Represents a single shader stage (vertex, fragment, compute, etc.)
// For OpenGL: Compiled GLSL shader
// For Vulkan: SPIR-V module

class RHIShaderModule {
public:
    virtual ~RHIShaderModule() = default;

    virtual ShaderStage getStage() const = 0;
    virtual const std::string& getEntryPoint() const = 0;
    virtual void* getNativeHandle() const = 0;

protected:
    RHIShaderModule() = default;
};

// ============================================================================
// RHI SHADER PROGRAM
// ============================================================================
// A linked collection of shader modules
// For OpenGL: Linked program object
// For Vulkan: Just a container (linking happens in pipeline creation)

class RHIShaderProgram {
public:
    virtual ~RHIShaderProgram() = default;

    // Get native handle (GLuint program for OpenGL, nullptr for Vulkan)
    virtual void* getNativeHandle() const = 0;

    // Get shader modules
    virtual RHIShaderModule* getModule(ShaderStage stage) const = 0;

    // ========================================================================
    // REFLECTION (Optional, may not be available for all backends)
    // ========================================================================

    // Get uniform block binding by name
    virtual int32_t getUniformBlockBinding(const std::string& name) const = 0;

    // Get uniform location by name (OpenGL-specific)
    virtual int32_t getUniformLocation(const std::string& name) const = 0;

    // Get storage buffer binding by name
    virtual int32_t getStorageBufferBinding(const std::string& name) const = 0;

protected:
    RHIShaderProgram() = default;
};

// ============================================================================
// SHADER COMPILATION
// ============================================================================

enum class ShaderSourceType {
    GLSL,       // Raw GLSL source
    SPIRV,      // Pre-compiled SPIR-V bytecode
    SPIRVPath   // Path to SPIR-V file
};

struct ShaderSource {
    ShaderStage stage = ShaderStage::None;
    ShaderSourceType type = ShaderSourceType::GLSL;
    std::string source;     // GLSL source or file path
    std::vector<uint8_t> spirv;  // SPIR-V bytecode
    std::string entryPoint = "main";

    // Helper constructors
    static ShaderSource fromGLSL(ShaderStage s, const std::string& glsl) {
        ShaderSource src;
        src.stage = s;
        src.type = ShaderSourceType::GLSL;
        src.source = glsl;
        return src;
    }

    static ShaderSource fromSPIRV(ShaderStage s, const std::vector<uint8_t>& code, const std::string& entry = "main") {
        ShaderSource src;
        src.stage = s;
        src.type = ShaderSourceType::SPIRV;
        src.spirv = code;
        src.entryPoint = entry;
        return src;
    }
};

struct ShaderProgramDesc {
    std::vector<ShaderSource> stages;
    std::string debugName;
};

} // namespace RHI
