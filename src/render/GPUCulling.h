#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <iostream>

// ============================================================================
// GPU-DRIVEN FRUSTUM CULLING
// ============================================================================
// Uses compute shaders to perform frustum culling on the GPU.
// This eliminates CPU-GPU synchronization for visibility testing.
//
// Benefits:
// - No CPU-side frustum checks (saves CPU time)
// - Better parallelism (GPU tests thousands of chunks simultaneously)
// - Reduced draw call overhead (atomic append to indirect buffer)
// ============================================================================

namespace GPUCulling {

// Sub-chunk data for GPU culling
struct alignas(16) SubChunkData {
    glm::vec4 boundingSphere;  // xyz = center, w = radius
    glm::vec4 chunkOffset;     // xyz = world offset, w = subChunkIndex
    uint32_t baseVertex;       // Starting vertex in vertex pool
    uint32_t vertexCount;      // Number of vertices
    uint32_t lodLevel;         // Current LOD level
    uint32_t padding;          // Alignment padding
};

// Draw command for indirect rendering (matches GL_DRAW_ARRAYS_INDIRECT_BUFFER format)
struct DrawArraysIndirectCommand {
    uint32_t count;          // Vertex count
    uint32_t instanceCount;  // Always 1
    uint32_t first;          // Base vertex
    uint32_t baseInstance;   // Used to index into per-draw data
};

class GPUCuller {
public:
    GPUCuller() = default;
    ~GPUCuller() { cleanup(); }

    bool init(size_t maxSubChunks = 16384) {
        this->maxSubChunks = maxSubChunks;

        // Compile compute shader
        if (!compileComputeShader()) {
            std::cerr << "[GPUCulling] Failed to compile compute shader" << std::endl;
            return false;
        }

        // Create buffers
        // Input: Sub-chunk data (bounding spheres, offsets, vertex info)
        glGenBuffers(1, &subChunkDataSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, subChunkDataSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, maxSubChunks * sizeof(SubChunkData),
                     nullptr, GL_DYNAMIC_DRAW);

        // Output: Indirect draw commands (populated by compute shader)
        glGenBuffers(1, &indirectDrawBuffer);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectDrawBuffer);
        glBufferData(GL_DRAW_INDIRECT_BUFFER, maxSubChunks * sizeof(DrawArraysIndirectCommand),
                     nullptr, GL_DYNAMIC_DRAW);

        // Output: Visible chunk offsets for vertex shader
        glGenBuffers(1, &visibleOffsetsSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, visibleOffsetsSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, maxSubChunks * sizeof(glm::vec4),
                     nullptr, GL_DYNAMIC_DRAW);

        // Atomic counter for number of visible sub-chunks
        glGenBuffers(1, &atomicCounterBuffer);
        glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, atomicCounterBuffer);
        glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(uint32_t), nullptr, GL_DYNAMIC_DRAW);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
        glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, 0);

        std::cout << "[GPUCulling] Initialized (max " << maxSubChunks << " sub-chunks)" << std::endl;
        return true;
    }

    void cleanup() {
        if (computeProgram != 0) glDeleteProgram(computeProgram);
        if (subChunkDataSSBO != 0) glDeleteBuffers(1, &subChunkDataSSBO);
        if (indirectDrawBuffer != 0) glDeleteBuffers(1, &indirectDrawBuffer);
        if (visibleOffsetsSSBO != 0) glDeleteBuffers(1, &visibleOffsetsSSBO);
        if (atomicCounterBuffer != 0) glDeleteBuffers(1, &atomicCounterBuffer);
        computeProgram = 0;
        subChunkDataSSBO = 0;
        indirectDrawBuffer = 0;
        visibleOffsetsSSBO = 0;
        atomicCounterBuffer = 0;
    }

    // Upload sub-chunk data for culling
    void uploadSubChunkData(const std::vector<SubChunkData>& subChunks) {
        if (subChunks.empty()) return;

        numSubChunks = std::min(subChunks.size(), maxSubChunks);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, subChunkDataSSBO);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                        numSubChunks * sizeof(SubChunkData), subChunks.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    // Run GPU frustum culling
    // Returns number of visible sub-chunks
    uint32_t cull(const glm::mat4& viewProj) {
        if (computeProgram == 0 || numSubChunks == 0) return 0;

        // Reset atomic counter
        uint32_t zero = 0;
        glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, atomicCounterBuffer);
        glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(uint32_t), &zero);

        // Bind compute shader
        glUseProgram(computeProgram);

        // Set uniforms
        glUniformMatrix4fv(viewProjLoc, 1, GL_FALSE, glm::value_ptr(viewProj));
        glUniform1ui(numSubChunksLoc, static_cast<GLuint>(numSubChunks));

        // Bind SSBOs
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, subChunkDataSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, indirectDrawBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, visibleOffsetsSSBO);
        glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicCounterBuffer);

        // Dispatch compute shader (64 threads per workgroup)
        GLuint workGroups = (static_cast<GLuint>(numSubChunks) + 63) / 64;
        glDispatchCompute(workGroups, 1, 1);

        // Memory barrier to ensure compute shader writes are visible
        glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);

        // Read back visible count
        glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, atomicCounterBuffer);
        uint32_t* countPtr = (uint32_t*)glMapBufferRange(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(uint32_t), GL_MAP_READ_BIT);
        uint32_t visibleCount = countPtr ? *countPtr : 0;
        glUnmapBuffer(GL_ATOMIC_COUNTER_BUFFER);

        glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, 0);
        glUseProgram(0);

        return visibleCount;
    }

    // Get indirect draw buffer for glMultiDrawArraysIndirect
    GLuint getIndirectBuffer() const { return indirectDrawBuffer; }

    // Get visible offsets SSBO for vertex shader
    GLuint getVisibleOffsetsSSBO() const { return visibleOffsetsSSBO; }

    bool isInitialized() const { return computeProgram != 0; }

private:
    GLuint computeProgram = 0;
    GLuint subChunkDataSSBO = 0;
    GLuint indirectDrawBuffer = 0;
    GLuint visibleOffsetsSSBO = 0;
    GLuint atomicCounterBuffer = 0;

    GLint viewProjLoc = -1;
    GLint numSubChunksLoc = -1;

    size_t maxSubChunks = 0;
    size_t numSubChunks = 0;

    bool compileComputeShader() {
        const char* computeSource = R"(
#version 430 core

layout(local_size_x = 64) in;

// Input: Sub-chunk data
struct SubChunkData {
    vec4 boundingSphere;  // xyz = center, w = radius
    vec4 chunkOffset;     // xyz = world offset, w = subChunkIndex
    uint baseVertex;
    uint vertexCount;
    uint lodLevel;
    uint padding;
};

// Output: Indirect draw command
struct DrawCommand {
    uint count;
    uint instanceCount;
    uint first;
    uint baseInstance;
};

layout(std430, binding = 0) readonly buffer SubChunkBuffer {
    SubChunkData subChunks[];
};

layout(std430, binding = 1) writeonly buffer DrawCommandBuffer {
    DrawCommand drawCommands[];
};

layout(std430, binding = 2) writeonly buffer VisibleOffsetsBuffer {
    vec4 visibleOffsets[];
};

layout(binding = 0) uniform atomic_uint visibleCount;

uniform mat4 viewProj;
uniform uint numSubChunks;

// Extract frustum planes from view-projection matrix
void extractFrustumPlanes(mat4 vp, out vec4 planes[6]) {
    // Left
    planes[0] = vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]);
    // Right
    planes[1] = vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]);
    // Bottom
    planes[2] = vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]);
    // Top
    planes[3] = vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]);
    // Near
    planes[4] = vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2], vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]);
    // Far
    planes[5] = vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]);

    // Normalize planes
    for (int i = 0; i < 6; i++) {
        float len = length(planes[i].xyz);
        planes[i] /= len;
    }
}

// Test sphere against frustum
bool sphereInFrustum(vec3 center, float radius, vec4 planes[6]) {
    for (int i = 0; i < 6; i++) {
        float dist = dot(planes[i].xyz, center) + planes[i].w;
        if (dist < -radius) {
            return false;  // Completely outside this plane
        }
    }
    return true;  // Inside or intersecting all planes
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= numSubChunks) return;

    SubChunkData sub = subChunks[idx];

    // Skip empty sub-chunks
    if (sub.vertexCount == 0) return;

    // Extract frustum planes
    vec4 frustumPlanes[6];
    extractFrustumPlanes(viewProj, frustumPlanes);

    // Frustum cull using bounding sphere
    vec3 center = sub.boundingSphere.xyz + sub.chunkOffset.xyz;
    float radius = sub.boundingSphere.w;

    if (!sphereInFrustum(center, radius, frustumPlanes)) {
        return;  // Culled
    }

    // Visible! Atomically append to output
    uint slot = atomicCounterIncrement(visibleCount);

    // Write draw command
    drawCommands[slot].count = sub.vertexCount;
    drawCommands[slot].instanceCount = 1;
    drawCommands[slot].first = sub.baseVertex;
    drawCommands[slot].baseInstance = idx;  // Original sub-chunk index for CPU readback

    // Write chunk offset for vertex shader
    visibleOffsets[slot] = sub.chunkOffset;
}
)";

        GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
        glShaderSource(shader, 1, &computeSource, nullptr);
        glCompileShader(shader);

        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetShaderInfoLog(shader, 512, nullptr, infoLog);
            std::cerr << "[GPUCulling] Compute shader compilation failed:\n" << infoLog << std::endl;
            glDeleteShader(shader);
            return false;
        }

        computeProgram = glCreateProgram();
        glAttachShader(computeProgram, shader);
        glLinkProgram(computeProgram);
        glDeleteShader(shader);

        glGetProgramiv(computeProgram, GL_LINK_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetProgramInfoLog(computeProgram, 512, nullptr, infoLog);
            std::cerr << "[GPUCulling] Compute program linking failed:\n" << infoLog << std::endl;
            glDeleteProgram(computeProgram);
            computeProgram = 0;
            return false;
        }

        // Get uniform locations
        viewProjLoc = glGetUniformLocation(computeProgram, "viewProj");
        numSubChunksLoc = glGetUniformLocation(computeProgram, "numSubChunks");

        std::cout << "[GPUCulling] Compute shader compiled successfully" << std::endl;
        return true;
    }
};

} // namespace GPUCulling
