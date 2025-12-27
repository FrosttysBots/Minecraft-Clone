#include "GLDescriptorSet.h"
#include "GLDevice.h"
#include "GLPipeline.h"

namespace RHI {

// ============================================================================
// GL DESCRIPTOR SET
// ============================================================================

GLDescriptorSet::GLDescriptorSet(GLDevice* device, GLDescriptorSetLayout* layout)
    : m_device(device), m_layout(layout) {

    // Pre-allocate bindings based on layout
    const auto& layoutDesc = layout->getDesc();
    m_bindings.resize(layoutDesc.bindings.size());

    for (size_t i = 0; i < layoutDesc.bindings.size(); i++) {
        m_bindings[i].binding = layoutDesc.bindings[i].binding;
        m_bindings[i].type = layoutDesc.bindings[i].type;
    }
}

void GLDescriptorSet::update(const std::vector<DescriptorWrite>& writes) {
    for (const auto& write : writes) {
        // Find matching binding
        for (auto& binding : m_bindings) {
            if (binding.binding == write.binding) {
                binding.type = write.type;
                binding.buffer = static_cast<GLBuffer*>(write.buffer);
                binding.bufferOffset = write.bufferOffset;
                binding.bufferRange = write.bufferRange;
                binding.texture = static_cast<GLTexture*>(write.texture);
                binding.sampler = static_cast<GLSampler*>(write.sampler);
                break;
            }
        }
    }
}

void GLDescriptorSet::updateBuffer(uint32_t binding, RHIBuffer* buffer, size_t offset, size_t range) {
    for (auto& b : m_bindings) {
        if (b.binding == binding) {
            b.buffer = static_cast<GLBuffer*>(buffer);
            b.bufferOffset = offset;
            b.bufferRange = range;
            break;
        }
    }
}

void GLDescriptorSet::updateTexture(uint32_t binding, RHITexture* texture, RHISampler* sampler) {
    for (auto& b : m_bindings) {
        if (b.binding == binding) {
            b.texture = static_cast<GLTexture*>(texture);
            b.sampler = static_cast<GLSampler*>(sampler);
            break;
        }
    }
}

void GLDescriptorSet::bind(uint32_t setIndex) {
    // Apply all bindings to OpenGL state
    // Note: setIndex is used to calculate the actual binding point

    for (const auto& binding : m_bindings) {
        uint32_t actualBinding = binding.binding;  // Could add setIndex offset if needed

        switch (binding.type) {
            case DescriptorType::UniformBuffer:
            case DescriptorType::UniformBufferDynamic:
                if (binding.buffer) {
                    size_t range = binding.bufferRange > 0 ? binding.bufferRange : binding.buffer->getDesc().size;
                    glBindBufferRange(GL_UNIFORM_BUFFER, actualBinding,
                                      binding.buffer->getGLBuffer(),
                                      static_cast<GLintptr>(binding.bufferOffset),
                                      static_cast<GLsizeiptr>(range));
                }
                break;

            case DescriptorType::StorageBuffer:
            case DescriptorType::StorageBufferDynamic:
                if (binding.buffer) {
                    size_t range = binding.bufferRange > 0 ? binding.bufferRange : binding.buffer->getDesc().size;
                    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, actualBinding,
                                      binding.buffer->getGLBuffer(),
                                      static_cast<GLintptr>(binding.bufferOffset),
                                      static_cast<GLsizeiptr>(range));
                }
                break;

            case DescriptorType::SampledTexture:
                if (binding.texture) {
                    glActiveTexture(GL_TEXTURE0 + actualBinding);
                    glBindTexture(binding.texture->getGLTarget(), binding.texture->getGLTexture());
                    if (binding.sampler) {
                        glBindSampler(actualBinding, binding.sampler->getGLSampler());
                    }
                }
                break;

            case DescriptorType::StorageTexture:
                if (binding.texture) {
                    // Bind as image for compute shaders
                    glBindImageTexture(actualBinding, binding.texture->getGLTexture(),
                                       0, GL_TRUE, 0, GL_READ_WRITE,
                                       GLDevice::toGLInternalFormat(binding.texture->getDesc().format));
                }
                break;

            case DescriptorType::Sampler:
                if (binding.sampler) {
                    glBindSampler(actualBinding, binding.sampler->getGLSampler());
                }
                break;

            default:
                break;
        }
    }

    (void)setIndex;
}

// ============================================================================
// GL DESCRIPTOR POOL
// ============================================================================

GLDescriptorPool::GLDescriptorPool(GLDevice* device, const DescriptorPoolDesc& desc)
    : m_device(device), m_desc(desc) {
    // In OpenGL, we don't need a real pool - descriptor sets are lightweight
}

std::unique_ptr<RHIDescriptorSet> GLDescriptorPool::allocate(RHIDescriptorSetLayout* layout) {
    return std::make_unique<GLDescriptorSet>(m_device, static_cast<GLDescriptorSetLayout*>(layout));
}

void GLDescriptorPool::reset() {
    // Nothing to do in OpenGL
}

} // namespace RHI
