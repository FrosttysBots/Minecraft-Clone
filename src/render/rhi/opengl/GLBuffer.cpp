#include "GLBuffer.h"
#include "GLDevice.h"
#include <cstring>
#include <iostream>

namespace RHI {

GLBuffer::GLBuffer(GLDevice* device, const BufferDesc& desc)
    : m_device(device), m_desc(desc) {

    // Determine GL target based on usage
    if (hasFlag(desc.usage, BufferUsage::Vertex)) {
        m_target = GL_ARRAY_BUFFER;
    } else if (hasFlag(desc.usage, BufferUsage::Index)) {
        m_target = GL_ELEMENT_ARRAY_BUFFER;
    } else if (hasFlag(desc.usage, BufferUsage::Uniform)) {
        m_target = GL_UNIFORM_BUFFER;
    } else if (hasFlag(desc.usage, BufferUsage::Storage)) {
        m_target = GL_SHADER_STORAGE_BUFFER;
    } else if (hasFlag(desc.usage, BufferUsage::Indirect)) {
        m_target = GL_DRAW_INDIRECT_BUFFER;
    } else {
        m_target = GL_ARRAY_BUFFER;
    }

    glGenBuffers(1, &m_buffer);
    glBindBuffer(m_target, m_buffer);

    m_isPersistent = desc.persistentMap || desc.memory == MemoryUsage::Persistent;

    if (m_isPersistent) {
        // Use persistent mapping for low-latency CPU->GPU updates
        GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        glBufferStorage(m_target, static_cast<GLsizeiptr>(desc.size), nullptr, flags);
        m_persistentPtr = glMapBufferRange(m_target, 0, static_cast<GLsizeiptr>(desc.size), flags);

        if (!m_persistentPtr) {
            std::cerr << "[GLBuffer] Failed to create persistent mapping for buffer" << std::endl;
        }
    } else {
        // Standard buffer allocation
        GLenum usage = GLDevice::toGLBufferUsage(desc.usage, desc.memory);
        glBufferData(m_target, static_cast<GLsizeiptr>(desc.size), nullptr, usage);
    }

    glBindBuffer(m_target, 0);

    if (!desc.debugName.empty()) {
        glObjectLabel(GL_BUFFER, m_buffer, -1, desc.debugName.c_str());
    }
}

GLBuffer::~GLBuffer() {
    if (m_buffer != 0) {
        if (m_isPersistent && m_persistentPtr) {
            glBindBuffer(m_target, m_buffer);
            glUnmapBuffer(m_target);
            glBindBuffer(m_target, 0);
        }
        glDeleteBuffers(1, &m_buffer);
    }
}

void* GLBuffer::map() {
    if (m_isPersistent) {
        return m_persistentPtr;
    }

    if (m_mappedPtr) {
        return m_mappedPtr;  // Already mapped
    }

    glBindBuffer(m_target, m_buffer);
    m_mappedPtr = glMapBuffer(m_target, GL_WRITE_ONLY);
    glBindBuffer(m_target, 0);
    return m_mappedPtr;
}

void* GLBuffer::mapRange(size_t offset, size_t size) {
    if (m_isPersistent) {
        return static_cast<uint8_t*>(m_persistentPtr) + offset;
    }

    if (m_mappedPtr) {
        return static_cast<uint8_t*>(m_mappedPtr) + offset;
    }

    glBindBuffer(m_target, m_buffer);
    GLbitfield access = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT;
    m_mappedPtr = glMapBufferRange(m_target, static_cast<GLintptr>(offset),
                                    static_cast<GLsizeiptr>(size), access);
    glBindBuffer(m_target, 0);
    return m_mappedPtr;
}

void GLBuffer::unmap() {
    if (m_isPersistent) {
        return;  // Persistent buffers stay mapped
    }

    if (m_mappedPtr) {
        glBindBuffer(m_target, m_buffer);
        glUnmapBuffer(m_target);
        glBindBuffer(m_target, 0);
        m_mappedPtr = nullptr;
    }
}

void GLBuffer::uploadData(const void* data, size_t size, size_t offset) {
    if (m_isPersistent && m_persistentPtr) {
        // Direct copy to persistent mapping
        std::memcpy(static_cast<uint8_t*>(m_persistentPtr) + offset, data, size);
    } else {
        // Use glBufferSubData
        glBindBuffer(m_target, m_buffer);
        glBufferSubData(m_target, static_cast<GLintptr>(offset),
                        static_cast<GLsizeiptr>(size), data);
        glBindBuffer(m_target, 0);
    }
}

void GLBuffer::flush(size_t offset, size_t size) {
    // For coherent persistent mapping, no flush needed
    // For non-coherent, we would use glFlushMappedBufferRange
    if (!m_isPersistent && m_mappedPtr) {
        glBindBuffer(m_target, m_buffer);
        glFlushMappedBufferRange(m_target, static_cast<GLintptr>(offset),
                                  static_cast<GLsizeiptr>(size == 0 ? m_desc.size : size));
        glBindBuffer(m_target, 0);
    }
}

void GLBuffer::invalidate(size_t offset, size_t size) {
    // For reading data back from GPU
    glBindBuffer(m_target, m_buffer);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    glBindBuffer(m_target, 0);
    (void)offset;
    (void)size;
}

} // namespace RHI
