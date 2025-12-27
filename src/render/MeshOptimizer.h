#pragma once

#include <meshoptimizer.h>
#include <vector>
#include <cstdint>
#include <algorithm>

// ============================================================================
// MESH OPTIMIZER INTEGRATION
// ============================================================================
// Uses meshoptimizer library for GPU-friendly mesh optimization:
// - Vertex cache optimization (better GPU cache usage)
// - Overdraw optimization (reduce pixel overdraw)
// - Vertex fetch optimization (better memory access patterns)
//
// Typical gains: 10-30% improvement in GPU rendering efficiency
// ============================================================================

namespace MeshOpt {

// Optimize vertex order for GPU vertex cache (post-transform cache)
// This reorders vertices to maximize cache hits during rendering
template<typename Vertex>
void optimizeVertexCache(std::vector<Vertex>& vertices) {
    if (vertices.size() < 6) return;  // Need at least 2 triangles

    size_t vertexCount = vertices.size();
    size_t triangleCount = vertexCount / 3;

    // Generate identity indices (we use non-indexed rendering)
    std::vector<uint32_t> indices(vertexCount);
    for (size_t i = 0; i < vertexCount; i++) {
        indices[i] = static_cast<uint32_t>(i);
    }

    // Optimize for vertex cache
    std::vector<uint32_t> optimizedIndices(vertexCount);
    meshopt_optimizeVertexCache(optimizedIndices.data(), indices.data(),
                                 vertexCount, vertexCount);

    // Reorder vertices according to optimized indices
    std::vector<Vertex> optimizedVertices(vertexCount);
    for (size_t i = 0; i < vertexCount; i++) {
        optimizedVertices[i] = vertices[optimizedIndices[i]];
    }

    vertices = std::move(optimizedVertices);
}

// Optimize for overdraw (reduce pixel shader invocations)
// Reorders triangles to minimize overdraw based on vertex positions
template<typename Vertex>
void optimizeOverdraw(std::vector<Vertex>& vertices, float threshold = 1.05f) {
    if (vertices.size() < 6) return;

    size_t vertexCount = vertices.size();

    // Generate indices
    std::vector<uint32_t> indices(vertexCount);
    for (size_t i = 0; i < vertexCount; i++) {
        indices[i] = static_cast<uint32_t>(i);
    }

    // Extract positions for overdraw analysis
    // Assume first 3 floats of vertex are position (x, y, z)
    std::vector<float> positions(vertexCount * 3);
    const float* vertexData = reinterpret_cast<const float*>(vertices.data());
    size_t vertexStride = sizeof(Vertex) / sizeof(float);

    for (size_t i = 0; i < vertexCount; i++) {
        positions[i * 3 + 0] = vertexData[i * vertexStride + 0];
        positions[i * 3 + 1] = vertexData[i * vertexStride + 1];
        positions[i * 3 + 2] = vertexData[i * vertexStride + 2];
    }

    // Optimize for overdraw
    std::vector<uint32_t> optimizedIndices(vertexCount);
    meshopt_optimizeOverdraw(optimizedIndices.data(), indices.data(),
                              vertexCount, positions.data(), vertexCount,
                              sizeof(float) * 3, threshold);

    // Reorder vertices
    std::vector<Vertex> optimizedVertices(vertexCount);
    for (size_t i = 0; i < vertexCount; i++) {
        optimizedVertices[i] = vertices[optimizedIndices[i]];
    }

    vertices = std::move(optimizedVertices);
}

// Optimize vertex fetch (improve memory access patterns)
// Reorders vertex buffer to match access order
template<typename Vertex>
void optimizeVertexFetch(std::vector<Vertex>& vertices) {
    if (vertices.size() < 3) return;

    size_t vertexCount = vertices.size();

    // Generate indices
    std::vector<uint32_t> indices(vertexCount);
    for (size_t i = 0; i < vertexCount; i++) {
        indices[i] = static_cast<uint32_t>(i);
    }

    // Remap vertices for optimal fetch
    std::vector<uint32_t> remap(vertexCount);
    size_t uniqueVertices = meshopt_optimizeVertexFetchRemap(
        remap.data(), indices.data(), vertexCount, vertexCount);

    // Apply remapping
    std::vector<Vertex> optimizedVertices(uniqueVertices);
    meshopt_remapVertexBuffer(optimizedVertices.data(), vertices.data(),
                               vertexCount, sizeof(Vertex), remap.data());

    vertices = std::move(optimizedVertices);
}

// Full optimization pipeline for chunk meshes
// Applies all optimizations in the correct order
template<typename Vertex>
void optimizeChunkMesh(std::vector<Vertex>& vertices) {
    if (vertices.size() < 6) return;

    // 1. Vertex cache optimization (most important for voxel meshes)
    optimizeVertexCache(vertices);

    // 2. Overdraw optimization (helps with complex terrain)
    // Skip for now - requires position extraction which varies by vertex format

    // 3. Vertex fetch optimization
    optimizeVertexFetch(vertices);
}

// Lightweight optimization for real-time mesh generation
// Only does vertex cache optimization (fastest)
template<typename Vertex>
void optimizeFast(std::vector<Vertex>& vertices) {
    if (vertices.size() < 6) return;
    optimizeVertexCache(vertices);
}

// Statistics for debugging
struct OptimizationStats {
    float acmr_before;  // Average Cache Miss Ratio before
    float acmr_after;   // Average Cache Miss Ratio after
    float improvement;  // Percentage improvement
};

template<typename Vertex>
OptimizationStats analyzeOptimization(const std::vector<Vertex>& vertices) {
    OptimizationStats stats = {0, 0, 0};
    if (vertices.size() < 6) return stats;

    size_t vertexCount = vertices.size();

    // Generate indices
    std::vector<uint32_t> indices(vertexCount);
    for (size_t i = 0; i < vertexCount; i++) {
        indices[i] = static_cast<uint32_t>(i);
    }

    // Analyze cache efficiency
    meshopt_VertexCacheStatistics beforeStats =
        meshopt_analyzeVertexCache(indices.data(), vertexCount, vertexCount, 16, 0, 0);

    // Optimize
    std::vector<uint32_t> optimizedIndices(vertexCount);
    meshopt_optimizeVertexCache(optimizedIndices.data(), indices.data(),
                                 vertexCount, vertexCount);

    meshopt_VertexCacheStatistics afterStats =
        meshopt_analyzeVertexCache(optimizedIndices.data(), vertexCount, vertexCount, 16, 0, 0);

    stats.acmr_before = beforeStats.acmr;
    stats.acmr_after = afterStats.acmr;
    stats.improvement = (1.0f - afterStats.acmr / beforeStats.acmr) * 100.0f;

    return stats;
}

} // namespace MeshOpt
