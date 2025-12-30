#include "GLDevice.h"
#include "GLBuffer.h"
#include "GLTexture.h"
#include "GLShader.h"
#include "GLPipeline.h"
#include "GLFramebuffer.h"
#include "GLDescriptorSet.h"
#include "GLCommandBuffer.h"

// WIP: Vulkan device for factory method (disabled while focusing on OpenGL)
#ifndef DISABLE_VULKAN
#include "../vulkan/VKDevice.h"
#endif

#include <iostream>
#include <sstream>

namespace RHI {

// ============================================================================
// RHI DEVICE FACTORY
// ============================================================================

std::unique_ptr<RHIDevice> RHIDevice::create(Backend backend, void* window) {
    switch (backend) {
        case Backend::OpenGL:
            std::cout << "[RHI] Creating OpenGL device" << std::endl;
            return std::make_unique<GLDevice>(static_cast<GLFWwindow*>(window));
#ifndef DISABLE_VULKAN
        case Backend::Vulkan:
            std::cout << "[RHI] Creating Vulkan device" << std::endl;
            return std::make_unique<VKDevice>(static_cast<GLFWwindow*>(window));
#endif
        default:
            std::cerr << "[RHI] Unknown backend" << std::endl;
            return nullptr;
    }
}

bool RHIDevice::isBackendSupported(Backend backend) {
    switch (backend) {
        case Backend::OpenGL:
            return true;
#ifndef DISABLE_VULKAN
        case Backend::Vulkan:
            // Check if Vulkan is available
            return glfwVulkanSupported() == GLFW_TRUE;
#endif
        default:
            return false;
    }
}

// ============================================================================
// GL DEVICE IMPLEMENTATION
// ============================================================================

GLDevice::GLDevice(GLFWwindow* window) : m_window(window) {
    queryDeviceInfo();

    m_graphicsQueue = std::make_unique<GLQueue>(this);
    m_immediateCommandBuffer = createCommandBuffer(CommandBufferLevel::Primary);

    std::cout << "[RHI] OpenGL device created: " << m_info.deviceName << std::endl;
}

GLDevice::~GLDevice() {
    waitIdle();
}

void GLDevice::queryDeviceInfo() {
    m_info.backend = Backend::OpenGL;

    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));

    m_info.deviceName = renderer ? renderer : "Unknown";
    m_info.vendorName = vendor ? vendor : "Unknown";
    m_info.apiVersion = version ? version : "Unknown";

    // Query limits
    GLint maxTextureSize, max3DTextureSize, maxCubeMapSize;
    GLint maxArrayLayers, maxColorAttachments;
    GLint maxComputeWorkGroupCount[3], maxComputeWorkGroupSize[3];
    GLint maxComputeWorkGroupInvocations;
    GLfloat maxAnisotropy;

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &max3DTextureSize);
    glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &maxCubeMapSize);
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxArrayLayers);
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &maxColorAttachments);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &maxComputeWorkGroupCount[0]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &maxComputeWorkGroupCount[1]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &maxComputeWorkGroupCount[2]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &maxComputeWorkGroupSize[0]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1, &maxComputeWorkGroupSize[1]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2, &maxComputeWorkGroupSize[2]);
    glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &maxComputeWorkGroupInvocations);
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAnisotropy);

    m_info.limits.maxTexture2DSize = maxTextureSize;
    m_info.limits.maxTexture3DSize = max3DTextureSize;
    m_info.limits.maxTextureCubeSize = maxCubeMapSize;
    m_info.limits.maxTextureArrayLayers = maxArrayLayers;
    m_info.limits.maxColorAttachments = maxColorAttachments;
    m_info.limits.maxComputeWorkGroupCount[0] = maxComputeWorkGroupCount[0];
    m_info.limits.maxComputeWorkGroupCount[1] = maxComputeWorkGroupCount[1];
    m_info.limits.maxComputeWorkGroupCount[2] = maxComputeWorkGroupCount[2];
    m_info.limits.maxComputeWorkGroupSize[0] = maxComputeWorkGroupSize[0];
    m_info.limits.maxComputeWorkGroupSize[1] = maxComputeWorkGroupSize[1];
    m_info.limits.maxComputeWorkGroupSize[2] = maxComputeWorkGroupSize[2];
    m_info.limits.maxComputeWorkGroupInvocations = maxComputeWorkGroupInvocations;
    m_info.limits.maxAnisotropy = maxAnisotropy;

    // Check for extensions
    m_info.limits.supportsComputeShaders = true;  // OpenGL 4.3+
    m_info.limits.supportsGeometryShaders = true;
    m_info.limits.supportsTessellation = true;
    m_info.limits.supportsMultiDrawIndirect = true;
    m_info.limits.supportsIndirectFirstInstance = true;
    m_info.limits.supportsPersistentMapping = true;

    // Check for mesh shader support
    m_info.limits.supportsMeshShaders = glfwExtensionSupported("GL_NV_mesh_shader") == GLFW_TRUE;
}

void GLDevice::waitIdle() {
    glFinish();
}

// ============================================================================
// RESOURCE CREATION
// ============================================================================

std::unique_ptr<RHIBuffer> GLDevice::createBuffer(const BufferDesc& desc) {
    return std::make_unique<GLBuffer>(this, desc);
}

std::unique_ptr<RHITexture> GLDevice::createTexture(const TextureDesc& desc) {
    return std::make_unique<GLTexture>(this, desc);
}

std::unique_ptr<RHISampler> GLDevice::createSampler(const SamplerDesc& desc) {
    return std::make_unique<GLSampler>(this, desc);
}

std::unique_ptr<RHIShaderModule> GLDevice::createShaderModule(const ShaderModuleDesc& desc) {
    return std::make_unique<GLShaderModule>(this, desc);
}

std::unique_ptr<RHIShaderProgram> GLDevice::createShaderProgram(const ShaderProgramDesc& desc) {
    return std::make_unique<GLShaderProgram>(this, desc);
}

std::unique_ptr<RHIDescriptorSetLayout> GLDevice::createDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) {
    return std::make_unique<GLDescriptorSetLayout>(desc);
}

std::unique_ptr<RHIPipelineLayout> GLDevice::createPipelineLayout(const PipelineLayoutDesc& desc) {
    return std::make_unique<GLPipelineLayout>(desc);
}

std::unique_ptr<RHIGraphicsPipeline> GLDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    return std::make_unique<GLGraphicsPipeline>(this, desc);
}

std::unique_ptr<RHIComputePipeline> GLDevice::createComputePipeline(const ComputePipelineDesc& desc) {
    return std::make_unique<GLComputePipeline>(this, desc);
}

std::unique_ptr<RHIRenderPass> GLDevice::createRenderPass(const RenderPassDesc& desc) {
    return std::make_unique<GLRenderPass>(desc);
}

std::unique_ptr<RHIFramebuffer> GLDevice::createFramebuffer(const FramebufferDesc& desc) {
    return std::make_unique<GLFramebuffer>(this, desc);
}

std::unique_ptr<RHISwapchain> GLDevice::createSwapchain(const SwapchainDesc& desc) {
    return std::make_unique<GLSwapchain>(this, desc);
}

std::unique_ptr<RHIDescriptorPool> GLDevice::createDescriptorPool(const DescriptorPoolDesc& desc) {
    return std::make_unique<GLDescriptorPool>(this, desc);
}

std::unique_ptr<RHICommandBuffer> GLDevice::createCommandBuffer(CommandBufferLevel level) {
    return std::make_unique<GLCommandBuffer>(this, level);
}

std::unique_ptr<RHIFence> GLDevice::createFence(bool signaled) {
    return std::make_unique<GLFence>(signaled);
}

std::unique_ptr<RHISemaphore> GLDevice::createSemaphore() {
    return std::make_unique<GLSemaphore>();
}

void GLDevice::executeImmediate(std::function<void(RHICommandBuffer*)> recordFunc) {
    m_immediateCommandBuffer->begin();
    recordFunc(m_immediateCommandBuffer.get());
    m_immediateCommandBuffer->end();
    m_graphicsQueue->submit({m_immediateCommandBuffer.get()});
    m_immediateCommandBuffer->reset();
}

// ============================================================================
// GL FORMAT CONVERSION
// ============================================================================

GLenum GLDevice::toGLFormat(Format format) {
    switch (format) {
        case Format::R8_UNORM:
        case Format::R8_SNORM:
        case Format::R8_UINT:
        case Format::R8_SINT:
            return GL_RED;
        case Format::RG8_UNORM:
        case Format::RG8_SNORM:
            return GL_RG;
        case Format::RGBA8_UNORM:
        case Format::RGBA8_SRGB:
            return GL_RGBA;
        case Format::BGRA8_UNORM:
        case Format::BGRA8_SRGB:
            return GL_BGRA;
        case Format::D16_UNORM:
        case Format::D32_FLOAT:
            return GL_DEPTH_COMPONENT;
        case Format::D24_UNORM_S8_UINT:
        case Format::D32_FLOAT_S8_UINT:
            return GL_DEPTH_STENCIL;
        default:
            return GL_RGBA;
    }
}

GLenum GLDevice::toGLInternalFormat(Format format) {
    switch (format) {
        case Format::R8_UNORM:     return GL_R8;
        case Format::R8_SNORM:     return GL_R8_SNORM;
        case Format::R8_UINT:      return GL_R8UI;
        case Format::R8_SINT:      return GL_R8I;
        case Format::R16_FLOAT:    return GL_R16F;
        case Format::R16_UINT:     return GL_R16UI;
        case Format::R16_SINT:     return GL_R16I;
        case Format::RG8_UNORM:    return GL_RG8;
        case Format::RG8_SNORM:    return GL_RG8_SNORM;
        case Format::R32_FLOAT:    return GL_R32F;
        case Format::R32_UINT:     return GL_R32UI;
        case Format::R32_SINT:     return GL_R32I;
        case Format::RG16_FLOAT:   return GL_RG16F;
        case Format::RGBA8_UNORM:  return GL_RGBA8;
        case Format::RGBA8_SRGB:   return GL_SRGB8_ALPHA8;
        case Format::BGRA8_UNORM:  return GL_RGBA8;  // GL handles BGRA internally
        case Format::BGRA8_SRGB:   return GL_SRGB8_ALPHA8;
        case Format::RGB10A2_UNORM: return GL_RGB10_A2;
        case Format::RG11B10_FLOAT: return GL_R11F_G11F_B10F;
        case Format::RG32_FLOAT:   return GL_RG32F;
        case Format::RGBA16_FLOAT: return GL_RGBA16F;
        case Format::RGBA32_FLOAT: return GL_RGBA32F;
        case Format::D16_UNORM:    return GL_DEPTH_COMPONENT16;
        case Format::D24_UNORM_S8_UINT: return GL_DEPTH24_STENCIL8;
        case Format::D32_FLOAT:    return GL_DEPTH_COMPONENT32F;
        case Format::D32_FLOAT_S8_UINT: return GL_DEPTH32F_STENCIL8;
        case Format::BC1_UNORM:    return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
        case Format::BC1_SRGB:     return GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT;
        case Format::BC3_UNORM:    return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        case Format::BC3_SRGB:     return GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT;
        case Format::BC5_UNORM:    return GL_COMPRESSED_RG_RGTC2;
        case Format::BC7_UNORM:    return GL_COMPRESSED_RGBA_BPTC_UNORM;
        case Format::BC7_SRGB:     return GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM;
        default: return GL_RGBA8;
    }
}

GLenum GLDevice::toGLType(Format format) {
    switch (format) {
        case Format::R8_UNORM:
        case Format::R8_UINT:
        case Format::RG8_UNORM:
        case Format::RGBA8_UNORM:
        case Format::RGBA8_SRGB:
        case Format::BGRA8_UNORM:
        case Format::BGRA8_SRGB:
            return GL_UNSIGNED_BYTE;
        case Format::R8_SNORM:
        case Format::R8_SINT:
        case Format::RG8_SNORM:
            return GL_BYTE;
        case Format::R16_FLOAT:
        case Format::RG16_FLOAT:
        case Format::RGBA16_FLOAT:
            return GL_HALF_FLOAT;
        case Format::R16_UINT:
            return GL_UNSIGNED_SHORT;
        case Format::R16_SINT:
            return GL_SHORT;
        case Format::R32_FLOAT:
        case Format::RG32_FLOAT:
        case Format::RGBA32_FLOAT:
        case Format::D32_FLOAT:
            return GL_FLOAT;
        case Format::R32_UINT:
            return GL_UNSIGNED_INT;
        case Format::R32_SINT:
            return GL_INT;
        case Format::RGB10A2_UNORM:
            return GL_UNSIGNED_INT_2_10_10_10_REV;
        case Format::RG11B10_FLOAT:
            return GL_UNSIGNED_INT_10F_11F_11F_REV;
        case Format::D16_UNORM:
            return GL_UNSIGNED_SHORT;
        case Format::D24_UNORM_S8_UINT:
            return GL_UNSIGNED_INT_24_8;
        case Format::D32_FLOAT_S8_UINT:
            return GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
        default:
            return GL_UNSIGNED_BYTE;
    }
}

GLenum GLDevice::toGLBufferUsage(BufferUsage usage, MemoryUsage memory) {
    // Determine access pattern based on memory usage
    switch (memory) {
        case MemoryUsage::GpuOnly:
            return GL_STATIC_DRAW;
        case MemoryUsage::CpuToGpu:
            return GL_DYNAMIC_DRAW;
        case MemoryUsage::GpuToCpu:
            return GL_STREAM_READ;
        case MemoryUsage::CpuOnly:
            return GL_STREAM_DRAW;
        case MemoryUsage::Persistent:
            return GL_DYNAMIC_DRAW;  // Will use persistent mapping
        default:
            return GL_STATIC_DRAW;
    }
}

GLenum GLDevice::toGLShaderStage(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:      return GL_VERTEX_SHADER;
        case ShaderStage::Fragment:    return GL_FRAGMENT_SHADER;
        case ShaderStage::Geometry:    return GL_GEOMETRY_SHADER;
        case ShaderStage::TessControl: return GL_TESS_CONTROL_SHADER;
        case ShaderStage::TessEval:    return GL_TESS_EVALUATION_SHADER;
        case ShaderStage::Compute:     return GL_COMPUTE_SHADER;
        default: return 0;
    }
}

GLenum GLDevice::toGLPrimitiveTopology(PrimitiveTopology topology) {
    switch (topology) {
        case PrimitiveTopology::PointList:     return GL_POINTS;
        case PrimitiveTopology::LineList:      return GL_LINES;
        case PrimitiveTopology::LineStrip:     return GL_LINE_STRIP;
        case PrimitiveTopology::TriangleList:  return GL_TRIANGLES;
        case PrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
        case PrimitiveTopology::TriangleFan:   return GL_TRIANGLE_FAN;
        case PrimitiveTopology::PatchList:     return GL_PATCHES;
        default: return GL_TRIANGLES;
    }
}

GLenum GLDevice::toGLBlendFactor(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::Zero:                  return GL_ZERO;
        case BlendFactor::One:                   return GL_ONE;
        case BlendFactor::SrcColor:              return GL_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor:      return GL_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor:              return GL_DST_COLOR;
        case BlendFactor::OneMinusDstColor:      return GL_ONE_MINUS_DST_COLOR;
        case BlendFactor::SrcAlpha:              return GL_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:      return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha:              return GL_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:      return GL_ONE_MINUS_DST_ALPHA;
        case BlendFactor::ConstantColor:         return GL_CONSTANT_COLOR;
        case BlendFactor::OneMinusConstantColor: return GL_ONE_MINUS_CONSTANT_COLOR;
        case BlendFactor::ConstantAlpha:         return GL_CONSTANT_ALPHA;
        case BlendFactor::OneMinusConstantAlpha: return GL_ONE_MINUS_CONSTANT_ALPHA;
        case BlendFactor::SrcAlphaSaturate:      return GL_SRC_ALPHA_SATURATE;
        default: return GL_ONE;
    }
}

GLenum GLDevice::toGLBlendOp(BlendOp op) {
    switch (op) {
        case BlendOp::Add:             return GL_FUNC_ADD;
        case BlendOp::Subtract:        return GL_FUNC_SUBTRACT;
        case BlendOp::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
        case BlendOp::Min:             return GL_MIN;
        case BlendOp::Max:             return GL_MAX;
        default: return GL_FUNC_ADD;
    }
}

GLenum GLDevice::toGLCompareOp(CompareOp op) {
    switch (op) {
        case CompareOp::Never:          return GL_NEVER;
        case CompareOp::Less:           return GL_LESS;
        case CompareOp::Equal:          return GL_EQUAL;
        case CompareOp::LessOrEqual:    return GL_LEQUAL;
        case CompareOp::Greater:        return GL_GREATER;
        case CompareOp::NotEqual:       return GL_NOTEQUAL;
        case CompareOp::GreaterOrEqual: return GL_GEQUAL;
        case CompareOp::Always:         return GL_ALWAYS;
        default: return GL_LESS;
    }
}

GLenum GLDevice::toGLCullMode(CullMode mode) {
    switch (mode) {
        case CullMode::None:         return GL_NONE;
        case CullMode::Front:        return GL_FRONT;
        case CullMode::Back:         return GL_BACK;
        case CullMode::FrontAndBack: return GL_FRONT_AND_BACK;
        default: return GL_BACK;
    }
}

GLenum GLDevice::toGLPolygonMode(PolygonMode mode) {
    switch (mode) {
        case PolygonMode::Fill:  return GL_FILL;
        case PolygonMode::Line:  return GL_LINE;
        case PolygonMode::Point: return GL_POINT;
        default: return GL_FILL;
    }
}

GLenum GLDevice::toGLFilter(Filter filter) {
    switch (filter) {
        case Filter::Nearest: return GL_NEAREST;
        case Filter::Linear:  return GL_LINEAR;
        default: return GL_LINEAR;
    }
}

GLenum GLDevice::toGLAddressMode(AddressMode mode) {
    switch (mode) {
        case AddressMode::Repeat:            return GL_REPEAT;
        case AddressMode::MirroredRepeat:    return GL_MIRRORED_REPEAT;
        case AddressMode::ClampToEdge:       return GL_CLAMP_TO_EDGE;
        case AddressMode::ClampToBorder:     return GL_CLAMP_TO_BORDER;
        case AddressMode::MirrorClampToEdge: return GL_MIRROR_CLAMP_TO_EDGE;
        default: return GL_REPEAT;
    }
}

GLenum GLDevice::toGLTextureTarget(TextureType type, uint32_t samples) {
    if (samples > 1) {
        return type == TextureType::Texture2DArray ? GL_TEXTURE_2D_MULTISAMPLE_ARRAY : GL_TEXTURE_2D_MULTISAMPLE;
    }
    switch (type) {
        case TextureType::Texture1D:        return GL_TEXTURE_1D;
        case TextureType::Texture2D:        return GL_TEXTURE_2D;
        case TextureType::Texture3D:        return GL_TEXTURE_3D;
        case TextureType::TextureCube:      return GL_TEXTURE_CUBE_MAP;
        case TextureType::Texture2DArray:   return GL_TEXTURE_2D_ARRAY;
        case TextureType::TextureCubeArray: return GL_TEXTURE_CUBE_MAP_ARRAY;
        default: return GL_TEXTURE_2D;
    }
}

// ============================================================================
// GL QUEUE
// ============================================================================

void GLQueue::submit(const std::vector<RHICommandBuffer*>& commandBuffers) {
    // In OpenGL, commands execute immediately during recording
    // Nothing to do here - commands have already executed
    (void)commandBuffers;
}

void GLQueue::waitIdle() {
    glFinish();
}

// ============================================================================
// GL FENCE
// ============================================================================

GLFence::GLFence(bool signaled) : m_signaled(signaled) {
}

GLFence::~GLFence() {
    if (m_sync) {
        glDeleteSync(m_sync);
    }
}

void GLFence::reset() {
    if (m_sync) {
        glDeleteSync(m_sync);
        m_sync = nullptr;
    }
    m_signaled = false;
}

void GLFence::wait(uint64_t timeout) {
    if (m_sync) {
        GLenum result = glClientWaitSync(m_sync, GL_SYNC_FLUSH_COMMANDS_BIT, timeout);
        if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED) {
            m_signaled = true;
        }
    }
}

void GLFence::setSync(GLsync sync) {
    if (m_sync) {
        glDeleteSync(m_sync);
    }
    m_sync = sync;
    m_signaled = false;
}

} // namespace RHI
