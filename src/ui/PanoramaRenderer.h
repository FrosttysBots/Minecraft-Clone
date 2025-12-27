#pragma once

// Panorama Renderer
// Renders a large 3D rotating voxel world background for the main menu
// 32x32 chunk world (512x512 blocks) with full terrain features

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cmath>
#include <random>
#include <iostream>

// Simplex-like noise for better terrain
class PanoramaNoise {
public:
    int seed;

    PanoramaNoise(int s = 42) : seed(s) {}

    float hash(int x, int z) const {
        int n = x + z * 57 + seed * 131;
        n = (n << 13) ^ n;
        return 1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f;
    }

    float hash3(int x, int y, int z) const {
        int n = x + y * 57 + z * 113 + seed * 131;
        n = (n << 13) ^ n;
        return ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 2147483648.0f;
    }

    float smoothNoise(float x, float z) const {
        int ix = static_cast<int>(std::floor(x));
        int iz = static_cast<int>(std::floor(z));
        float fx = x - ix;
        float fz = z - iz;

        fx = fx * fx * (3.0f - 2.0f * fx);
        fz = fz * fz * (3.0f - 2.0f * fz);

        float v00 = hash(ix, iz);
        float v10 = hash(ix + 1, iz);
        float v01 = hash(ix, iz + 1);
        float v11 = hash(ix + 1, iz + 1);

        float i0 = v00 + fx * (v10 - v00);
        float i1 = v01 + fx * (v11 - v01);

        return i0 + fz * (i1 - i0);
    }

    float fbm(float x, float z, int octaves = 4) const {
        float value = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        float maxValue = 0.0f;

        for (int i = 0; i < octaves; i++) {
            value += smoothNoise(x * frequency, z * frequency) * amplitude;
            maxValue += amplitude;
            amplitude *= 0.5f;
            frequency *= 2.0f;
        }

        return value / maxValue;
    }

    float ridged(float x, float z, int octaves = 4) const {
        float value = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        float maxValue = 0.0f;

        for (int i = 0; i < octaves; i++) {
            float n = smoothNoise(x * frequency, z * frequency);
            n = 1.0f - std::abs(n);
            n = n * n;
            value += n * amplitude;
            maxValue += amplitude;
            amplitude *= 0.5f;
            frequency *= 2.0f;
        }

        return value / maxValue;
    }
};

struct PanoramaVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    float ao;  // Ambient occlusion
};

class PanoramaRenderer {
public:
    GLuint shaderProgram = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    int vertexCount = 0;

    // 32x32 chunks = 512x512 blocks
    static constexpr int WORLD_SIZE = 512;
    static constexpr int MAX_HEIGHT = 128;
    static constexpr int SEA_LEVEL = 62;

    // Height and biome data (allocated dynamically to avoid stack overflow)
    std::vector<std::vector<int>> heightMap;
    std::vector<std::vector<int>> biomeMap;
    std::vector<std::vector<bool>> treeMap;

    // Camera
    float rotationAngle = 0.0f;
    float rotationSpeed = 0.05f;
    float cameraRadius = 280.0f;
    float cameraHeight = 140.0f;
    float lookAtHeight = 70.0f;

    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;

    bool initialized = false;
    int panoramaSeed = 42424242;
    float timeOfDay = 0.38f;

    PanoramaNoise noise;

    void init(int seed = 42424242) {
        std::cout << "Initializing panorama world (512x512 blocks)..." << std::endl;

        panoramaSeed = seed;
        noise = PanoramaNoise(seed);

        // Allocate height/biome maps
        heightMap.resize(WORLD_SIZE, std::vector<int>(WORLD_SIZE));
        biomeMap.resize(WORLD_SIZE, std::vector<int>(WORLD_SIZE));
        treeMap.resize(WORLD_SIZE, std::vector<bool>(WORLD_SIZE, false));

        createShader();
        generateTerrain();
        generateTrees();
        buildMesh();

        initialized = true;
        std::cout << "Panorama ready: " << vertexCount << " vertices" << std::endl;
    }

    void createShader() {
        const char* vertexShaderSrc = R"(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec3 aNormal;
            layout(location = 2) in vec3 aColor;
            layout(location = 3) in float aAO;

            uniform mat4 model;
            uniform mat4 view;
            uniform mat4 projection;

            out vec3 FragPos;
            out vec3 Normal;
            out vec3 Color;
            out float AO;
            out float Height;

            void main() {
                FragPos = vec3(model * vec4(aPos, 1.0));
                Normal = mat3(transpose(inverse(model))) * aNormal;
                Color = aColor;
                AO = aAO;
                Height = aPos.y;
                gl_Position = projection * view * model * vec4(aPos, 1.0);
            }
        )";

        const char* fragmentShaderSrc = R"(
            #version 330 core
            in vec3 FragPos;
            in vec3 Normal;
            in vec3 Color;
            in float AO;
            in float Height;

            uniform vec3 sunDir;
            uniform vec3 viewPos;
            uniform vec3 skyColorTop;
            uniform vec3 skyColorHorizon;
            uniform float fogStart;
            uniform float fogEnd;

            out vec4 FragColor;

            void main() {
                vec3 norm = normalize(Normal);
                vec3 lightDir = normalize(-sunDir);

                // Ambient with AO
                float ambient = 0.35 * AO;

                // Diffuse sunlight
                float diff = max(dot(norm, lightDir), 0.0);
                float sunLight = diff * 0.65;

                // Sky light (from above)
                float skyDiff = max(dot(norm, vec3(0.0, 1.0, 0.0)), 0.0);
                float skyLight = skyDiff * 0.25 * AO;

                // Combine lighting
                vec3 result = Color * (ambient + sunLight + skyLight);

                // Warm sun tint
                result += Color * sunLight * vec3(0.1, 0.05, 0.0);

                // Distance fog
                float dist = length(FragPos - viewPos);
                float fogFactor = clamp((fogEnd - dist) / (fogEnd - fogStart), 0.0, 1.0);

                // Height-based fog color
                float heightBlend = clamp((Height - 40.0) / 80.0, 0.0, 1.0);
                vec3 fogColor = mix(skyColorHorizon, skyColorTop, heightBlend * 0.3);

                result = mix(fogColor, result, fogFactor);

                // Slight color grading for cinematic look
                result = pow(result, vec3(0.95));

                FragColor = vec4(result, 1.0);
            }
        )";

        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexShaderSrc, nullptr);
        glCompileShader(vertexShader);

        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentShaderSrc, nullptr);
        glCompileShader(fragmentShader);

        shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        glLinkProgram(shaderProgram);

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
    }

    void generateTerrain() {
        for (int x = 0; x < WORLD_SIZE; x++) {
            for (int z = 0; z < WORLD_SIZE; z++) {
                float wx = static_cast<float>(x) - WORLD_SIZE / 2.0f;
                float wz = static_cast<float>(z) - WORLD_SIZE / 2.0f;

                // Continental base
                float continent = noise.fbm(wx * 0.003f, wz * 0.003f, 4);

                // Mountain ridges
                float mountains = noise.ridged(wx * 0.008f + 100, wz * 0.008f + 100, 5);

                // Hills
                float hills = noise.fbm(wx * 0.015f + 200, wz * 0.015f + 200, 3);

                // Fine detail
                float detail = noise.fbm(wx * 0.05f + 300, wz * 0.05f + 300, 2);

                // Combine layers
                float height = 64.0f;  // Base at sea level
                height += continent * 25.0f;

                // Mountains only where continent is high
                float mountainMask = glm::smoothstep(0.2f, 0.5f, continent);
                height += mountains * mountainMask * 50.0f;

                // Hills everywhere
                height += hills * 12.0f;

                // Fine detail
                height += detail * 4.0f;

                // River valleys
                float riverNoise = noise.fbm(wx * 0.01f + 500, wz * 0.01f + 500, 2);
                if (std::abs(riverNoise) < 0.08f) {
                    float riverDepth = 1.0f - std::abs(riverNoise) / 0.08f;
                    height -= riverDepth * 15.0f;
                }

                // Ocean around edges
                float distFromCenter = std::sqrt(wx * wx + wz * wz) / (WORLD_SIZE / 2.0f);
                if (distFromCenter > 0.75f) {
                    float oceanFactor = (distFromCenter - 0.75f) / 0.25f;
                    oceanFactor = oceanFactor * oceanFactor;
                    height = glm::mix(height, 45.0f, oceanFactor);
                }

                heightMap[x][z] = static_cast<int>(glm::clamp(height, 1.0f, static_cast<float>(MAX_HEIGHT - 1)));

                // Biome selection
                float temp = noise.fbm(wx * 0.006f + 1000, wz * 0.006f + 1000, 3);
                float humidity = noise.fbm(wx * 0.008f + 2000, wz * 0.008f + 2000, 3);

                int h = heightMap[x][z];
                if (h < SEA_LEVEL + 2) {
                    biomeMap[x][z] = 1;  // Beach/sand
                } else if (h > 95 && temp < 0.0f) {
                    biomeMap[x][z] = 3;  // Snow peaks
                } else if (temp < -0.2f) {
                    biomeMap[x][z] = 4;  // Taiga (cold forest)
                } else if (humidity > 0.3f) {
                    biomeMap[x][z] = 2;  // Forest
                } else if (humidity < -0.3f && temp > 0.2f) {
                    biomeMap[x][z] = 5;  // Desert
                } else {
                    biomeMap[x][z] = 0;  // Plains
                }
            }
        }
    }

    void generateTrees() {
        std::mt19937 rng(panoramaSeed);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        for (int x = 4; x < WORLD_SIZE - 4; x++) {
            for (int z = 4; z < WORLD_SIZE - 4; z++) {
                int biome = biomeMap[x][z];
                int h = heightMap[x][z];

                if (h < SEA_LEVEL + 3) continue;  // No trees in water/beach

                float treeDensity = 0.0f;
                switch (biome) {
                    case 0: treeDensity = 0.01f; break;   // Plains - sparse
                    case 2: treeDensity = 0.06f; break;   // Forest - dense
                    case 4: treeDensity = 0.04f; break;   // Taiga
                    default: treeDensity = 0.0f; break;
                }

                if (dist(rng) < treeDensity) {
                    // Check for flat ground
                    bool flat = true;
                    for (int dx = -1; dx <= 1 && flat; dx++) {
                        for (int dz = -1; dz <= 1 && flat; dz++) {
                            if (std::abs(heightMap[x+dx][z+dz] - h) > 2) flat = false;
                        }
                    }
                    if (flat) {
                        treeMap[x][z] = true;
                    }
                }
            }
        }
    }

    glm::vec3 getBlockColor(int x, int y, int z, bool isTop) {
        int biome = biomeMap[x][z];
        int surfaceHeight = heightMap[x][z];

        // Water
        if (y <= SEA_LEVEL && y > surfaceHeight) {
            float depth = static_cast<float>(SEA_LEVEL - y) / 10.0f;
            return glm::mix(glm::vec3(0.2f, 0.5f, 0.8f), glm::vec3(0.1f, 0.2f, 0.4f), glm::min(depth, 1.0f));
        }

        // Surface blocks
        if (y == surfaceHeight && isTop) {
            switch (biome) {
                case 0: return glm::vec3(0.35f, 0.55f, 0.22f);  // Plains grass
                case 1: return glm::vec3(0.76f, 0.70f, 0.50f);  // Sand
                case 2: return glm::vec3(0.28f, 0.50f, 0.18f);  // Forest grass (darker)
                case 3: return glm::vec3(0.95f, 0.97f, 1.00f);  // Snow
                case 4: return glm::vec3(0.30f, 0.45f, 0.25f);  // Taiga grass
                case 5: return glm::vec3(0.82f, 0.75f, 0.55f);  // Desert sand
            }
        }

        // Dirt layer
        if (y > surfaceHeight - 4) {
            if (biome == 1 || biome == 5) return glm::vec3(0.76f, 0.70f, 0.50f);
            if (biome == 3) return glm::vec3(0.85f, 0.87f, 0.90f);  // Snowy dirt
            return glm::vec3(0.50f, 0.38f, 0.26f);  // Dirt
        }

        // Stone with variation
        float v = noise.hash3(x, y, z) * 0.08f;
        return glm::vec3(0.52f + v, 0.52f + v, 0.55f + v);
    }

    glm::vec3 getTreeColor(int y, int treeHeight, int biome) {
        int trunkHeight = treeHeight - 3;
        if (y < trunkHeight) {
            // Trunk - wood color
            return glm::vec3(0.40f, 0.28f, 0.15f);
        } else {
            // Leaves
            if (biome == 4) {
                return glm::vec3(0.15f, 0.35f, 0.20f);  // Taiga - darker
            }
            return glm::vec3(0.20f, 0.45f, 0.15f);  // Normal leaves
        }
    }

    bool isBlockSolid(int x, int y, int z) {
        if (x < 0 || x >= WORLD_SIZE || z < 0 || z >= WORLD_SIZE) return false;
        if (y < 0) return true;
        if (y > MAX_HEIGHT) return false;

        // Terrain
        if (y <= heightMap[x][z]) return true;

        // Water
        if (y <= SEA_LEVEL && y > heightMap[x][z]) return true;

        // Trees
        if (treeMap[x][z]) {
            int h = heightMap[x][z];
            int treeHeight = 5 + (noise.hash(x, z) > 0 ? 1 : 0);
            if (y > h && y <= h + treeHeight) {
                int trunkHeight = treeHeight - 3;
                if (y <= h + trunkHeight) {
                    return true;  // Trunk
                } else {
                    // Leaves (simple sphere shape)
                    return true;
                }
            }
        }

        return false;
    }

    float calculateAO(int x, int y, int z, const glm::vec3& normal) {
        float ao = 1.0f;
        int checks = 0;
        int blocked = 0;

        // Check neighbors in the direction of the normal
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                for (int dz = -1; dz <= 1; dz++) {
                    if (dx == 0 && dy == 0 && dz == 0) continue;

                    // Only check in hemisphere of normal
                    glm::vec3 dir(dx, dy, dz);
                    if (glm::dot(dir, normal) > 0) {
                        checks++;
                        if (isBlockSolid(x + dx, y + dy, z + dz)) {
                            blocked++;
                        }
                    }
                }
            }
        }

        if (checks > 0) {
            ao = 1.0f - (static_cast<float>(blocked) / checks) * 0.6f;
        }

        return ao;
    }

    void addFace(std::vector<PanoramaVertex>& vertices,
                 const glm::vec3& p0, const glm::vec3& p1,
                 const glm::vec3& p2, const glm::vec3& p3,
                 const glm::vec3& normal, const glm::vec3& color, float ao) {
        vertices.push_back({p0, normal, color, ao});
        vertices.push_back({p1, normal, color, ao});
        vertices.push_back({p2, normal, color, ao});
        vertices.push_back({p0, normal, color, ao});
        vertices.push_back({p2, normal, color, ao});
        vertices.push_back({p3, normal, color, ao});
    }

    void buildMesh() {
        std::vector<PanoramaVertex> vertices;
        vertices.reserve(WORLD_SIZE * WORLD_SIZE * 12);

        float offset = -WORLD_SIZE / 2.0f;

        // Build terrain mesh (optimized - only surface and visible faces)
        for (int x = 0; x < WORLD_SIZE; x++) {
            for (int z = 0; z < WORLD_SIZE; z++) {
                int surfaceY = heightMap[x][z];
                float wx = x + offset;
                float wz = z + offset;

                // Water surface
                if (surfaceY < SEA_LEVEL) {
                    glm::vec3 waterColor = getBlockColor(x, SEA_LEVEL, z, true);
                    addFace(vertices,
                        glm::vec3(wx, SEA_LEVEL, wz),
                        glm::vec3(wx + 1, SEA_LEVEL, wz),
                        glm::vec3(wx + 1, SEA_LEVEL, wz + 1),
                        glm::vec3(wx, SEA_LEVEL, wz + 1),
                        glm::vec3(0, 1, 0), waterColor, 1.0f);
                }

                // Terrain surface - render a few layers for cliffs
                int startY = std::max(1, surfaceY - 8);
                for (int y = startY; y <= surfaceY; y++) {
                    float wy = static_cast<float>(y);
                    bool isTop = (y == surfaceY);
                    glm::vec3 color = getBlockColor(x, y, z, isTop);

                    // Top face
                    if (!isBlockSolid(x, y + 1, z) || y == surfaceY) {
                        if (y == surfaceY || (y <= SEA_LEVEL && surfaceY < SEA_LEVEL)) {
                            float ao = calculateAO(x, y, z, glm::vec3(0, 1, 0));
                            addFace(vertices,
                                glm::vec3(wx, wy + 1, wz),
                                glm::vec3(wx + 1, wy + 1, wz),
                                glm::vec3(wx + 1, wy + 1, wz + 1),
                                glm::vec3(wx, wy + 1, wz + 1),
                                glm::vec3(0, 1, 0), color, ao);
                        }
                    }

                    // Side faces (only if exposed)
                    auto addSide = [&](int nx, int nz, const glm::vec3& n,
                                       const glm::vec3& p0, const glm::vec3& p1,
                                       const glm::vec3& p2, const glm::vec3& p3) {
                        int neighborHeight = (nx >= 0 && nx < WORLD_SIZE && nz >= 0 && nz < WORLD_SIZE)
                            ? heightMap[nx][nz] : 0;
                        if (y > neighborHeight || (y > SEA_LEVEL && neighborHeight < SEA_LEVEL)) {
                            glm::vec3 sideColor = color * 0.8f;
                            float ao = calculateAO(x, y, z, n);
                            addFace(vertices, p0, p1, p2, p3, n, sideColor, ao);
                        }
                    };

                    // Only add side faces for visible blocks
                    if (y >= surfaceY - 4 || y > SEA_LEVEL) {
                        addSide(x, z + 1, glm::vec3(0, 0, 1),
                            glm::vec3(wx, wy, wz + 1), glm::vec3(wx + 1, wy, wz + 1),
                            glm::vec3(wx + 1, wy + 1, wz + 1), glm::vec3(wx, wy + 1, wz + 1));

                        addSide(x, z - 1, glm::vec3(0, 0, -1),
                            glm::vec3(wx + 1, wy, wz), glm::vec3(wx, wy, wz),
                            glm::vec3(wx, wy + 1, wz), glm::vec3(wx + 1, wy + 1, wz));

                        addSide(x + 1, z, glm::vec3(1, 0, 0),
                            glm::vec3(wx + 1, wy, wz + 1), glm::vec3(wx + 1, wy, wz),
                            glm::vec3(wx + 1, wy + 1, wz), glm::vec3(wx + 1, wy + 1, wz + 1));

                        addSide(x - 1, z, glm::vec3(-1, 0, 0),
                            glm::vec3(wx, wy, wz), glm::vec3(wx, wy, wz + 1),
                            glm::vec3(wx, wy + 1, wz + 1), glm::vec3(wx, wy + 1, wz));
                    }
                }

                // Trees
                if (treeMap[x][z]) {
                    int h = heightMap[x][z];
                    int treeHeight = 5 + (noise.hash(x, z) > 0 ? 1 : 0);
                    int trunkHeight = treeHeight - 3;
                    int biome = biomeMap[x][z];

                    // Trunk
                    for (int ty = 1; ty <= trunkHeight; ty++) {
                        float wy = static_cast<float>(h + ty);
                        glm::vec3 trunkColor = getTreeColor(ty, treeHeight, biome);

                        // All 4 sides of trunk
                        addFace(vertices,
                            glm::vec3(wx + 0.4f, wy, wz + 0.4f), glm::vec3(wx + 0.6f, wy, wz + 0.4f),
                            glm::vec3(wx + 0.6f, wy + 1, wz + 0.4f), glm::vec3(wx + 0.4f, wy + 1, wz + 0.4f),
                            glm::vec3(0, 0, -1), trunkColor, 0.85f);
                        addFace(vertices,
                            glm::vec3(wx + 0.6f, wy, wz + 0.6f), glm::vec3(wx + 0.4f, wy, wz + 0.6f),
                            glm::vec3(wx + 0.4f, wy + 1, wz + 0.6f), glm::vec3(wx + 0.6f, wy + 1, wz + 0.6f),
                            glm::vec3(0, 0, 1), trunkColor, 0.85f);
                        addFace(vertices,
                            glm::vec3(wx + 0.6f, wy, wz + 0.4f), glm::vec3(wx + 0.6f, wy, wz + 0.6f),
                            glm::vec3(wx + 0.6f, wy + 1, wz + 0.6f), glm::vec3(wx + 0.6f, wy + 1, wz + 0.4f),
                            glm::vec3(1, 0, 0), trunkColor * 0.9f, 0.85f);
                        addFace(vertices,
                            glm::vec3(wx + 0.4f, wy, wz + 0.6f), glm::vec3(wx + 0.4f, wy, wz + 0.4f),
                            glm::vec3(wx + 0.4f, wy + 1, wz + 0.4f), glm::vec3(wx + 0.4f, wy + 1, wz + 0.6f),
                            glm::vec3(-1, 0, 0), trunkColor * 0.9f, 0.85f);
                    }

                    // Leaves (simple blob)
                    glm::vec3 leafColor = getTreeColor(trunkHeight + 1, treeHeight, biome);
                    float leafY = h + trunkHeight;

                    // Bottom layer of leaves (wider)
                    for (int lx = -1; lx <= 1; lx++) {
                        for (int lz = -1; lz <= 1; lz++) {
                            float lwx = wx + lx;
                            float lwz = wz + lz;
                            // Top of leaf block
                            addFace(vertices,
                                glm::vec3(lwx, leafY + 2, lwz), glm::vec3(lwx + 1, leafY + 2, lwz),
                                glm::vec3(lwx + 1, leafY + 2, lwz + 1), glm::vec3(lwx, leafY + 2, lwz + 1),
                                glm::vec3(0, 1, 0), leafColor, 0.9f);
                            // Sides
                            if (lx == -1) {
                                addFace(vertices,
                                    glm::vec3(lwx, leafY, lwz), glm::vec3(lwx, leafY, lwz + 1),
                                    glm::vec3(lwx, leafY + 2, lwz + 1), glm::vec3(lwx, leafY + 2, lwz),
                                    glm::vec3(-1, 0, 0), leafColor * 0.75f, 0.85f);
                            }
                            if (lx == 1) {
                                addFace(vertices,
                                    glm::vec3(lwx + 1, leafY, lwz + 1), glm::vec3(lwx + 1, leafY, lwz),
                                    glm::vec3(lwx + 1, leafY + 2, lwz), glm::vec3(lwx + 1, leafY + 2, lwz + 1),
                                    glm::vec3(1, 0, 0), leafColor * 0.75f, 0.85f);
                            }
                            if (lz == -1) {
                                addFace(vertices,
                                    glm::vec3(lwx + 1, leafY, lwz), glm::vec3(lwx, leafY, lwz),
                                    glm::vec3(lwx, leafY + 2, lwz), glm::vec3(lwx + 1, leafY + 2, lwz),
                                    glm::vec3(0, 0, -1), leafColor * 0.8f, 0.85f);
                            }
                            if (lz == 1) {
                                addFace(vertices,
                                    glm::vec3(lwx, leafY, lwz + 1), glm::vec3(lwx + 1, leafY, lwz + 1),
                                    glm::vec3(lwx + 1, leafY + 2, lwz + 1), glm::vec3(lwx, leafY + 2, lwz + 1),
                                    glm::vec3(0, 0, 1), leafColor * 0.8f, 0.85f);
                            }
                        }
                    }

                    // Top layer (smaller)
                    float topY = leafY + 2;
                    addFace(vertices,
                        glm::vec3(wx, topY + 1, wz), glm::vec3(wx + 1, topY + 1, wz),
                        glm::vec3(wx + 1, topY + 1, wz + 1), glm::vec3(wx, topY + 1, wz + 1),
                        glm::vec3(0, 1, 0), leafColor * 1.05f, 0.95f);
                }
            }
        }

        vertexCount = static_cast<int>(vertices.size());

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(PanoramaVertex),
                     vertices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PanoramaVertex), (void*)0);
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PanoramaVertex),
                             (void*)offsetof(PanoramaVertex, normal));
        glEnableVertexAttribArray(1);

        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(PanoramaVertex),
                             (void*)offsetof(PanoramaVertex, color));
        glEnableVertexAttribArray(2);

        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(PanoramaVertex),
                             (void*)offsetof(PanoramaVertex, ao));
        glEnableVertexAttribArray(3);

        glBindVertexArray(0);
    }

    void update(float deltaTime) {
        if (!initialized) return;

        rotationAngle += rotationSpeed * deltaTime;
        if (rotationAngle > glm::two_pi<float>()) {
            rotationAngle -= glm::two_pi<float>();
        }

        // Gentle camera movement
        float bob = std::sin(rotationAngle * 1.5f) * 5.0f;
        float radiusVar = std::sin(rotationAngle * 0.7f) * 20.0f;

        float camX = std::cos(rotationAngle) * (cameraRadius + radiusVar);
        float camZ = std::sin(rotationAngle) * (cameraRadius + radiusVar);
        glm::vec3 cameraPos(camX, cameraHeight + bob, camZ);
        glm::vec3 lookAt(0.0f, lookAtHeight, 0.0f);

        viewMatrix = glm::lookAt(cameraPos, lookAt, glm::vec3(0, 1, 0));
    }

    void setProjection(int width, int height) {
        float aspect = static_cast<float>(width) / static_cast<float>(height);
        projectionMatrix = glm::perspective(glm::radians(65.0f), aspect, 1.0f, 800.0f);
    }

    void render() {
        if (!initialized || vertexCount == 0) return;

        // Sky colors
        glm::vec3 skyTop(0.35f, 0.55f, 0.90f);
        glm::vec3 skyHorizon(0.70f, 0.80f, 0.95f);

        // Sun direction
        float sunAngle = timeOfDay * glm::two_pi<float>() - glm::half_pi<float>();
        glm::vec3 sunDir(std::cos(sunAngle) * 0.8f, -std::sin(sunAngle), 0.3f);
        sunDir = glm::normalize(sunDir);

        // Camera position for fog
        float bob = std::sin(rotationAngle * 1.5f) * 5.0f;
        float radiusVar = std::sin(rotationAngle * 0.7f) * 20.0f;
        float camX = std::cos(rotationAngle) * (cameraRadius + radiusVar);
        float camZ = std::sin(rotationAngle) * (cameraRadius + radiusVar);
        glm::vec3 viewPos(camX, cameraHeight + bob, camZ);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        // Clear with sky color
        glClearColor(skyHorizon.r, skyHorizon.g, skyHorizon.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shaderProgram);

        glm::mat4 model(1.0f);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &model[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, &viewMatrix[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, &projectionMatrix[0][0]);

        glUniform3fv(glGetUniformLocation(shaderProgram, "sunDir"), 1, &sunDir[0]);
        glUniform3fv(glGetUniformLocation(shaderProgram, "viewPos"), 1, &viewPos[0]);
        glUniform3fv(glGetUniformLocation(shaderProgram, "skyColorTop"), 1, &skyTop[0]);
        glUniform3fv(glGetUniformLocation(shaderProgram, "skyColorHorizon"), 1, &skyHorizon[0]);
        glUniform1f(glGetUniformLocation(shaderProgram, "fogStart"), 150.0f);
        glUniform1f(glGetUniformLocation(shaderProgram, "fogEnd"), 400.0f);

        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, vertexCount);
        glBindVertexArray(0);

        glDisable(GL_DEPTH_TEST);
    }

    glm::vec3 getCameraPosition() const {
        float bob = std::sin(rotationAngle * 1.5f) * 5.0f;
        float radiusVar = std::sin(rotationAngle * 0.7f) * 20.0f;
        float camX = std::cos(rotationAngle) * (cameraRadius + radiusVar);
        float camZ = std::sin(rotationAngle) * (cameraRadius + radiusVar);
        return glm::vec3(camX, cameraHeight + bob, camZ);
    }

    bool isReady() const { return initialized; }

    void cleanup() {
        if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
        if (vbo) { glDeleteBuffers(1, &vbo); vbo = 0; }
        if (shaderProgram) { glDeleteProgram(shaderProgram); shaderProgram = 0; }
        heightMap.clear();
        biomeMap.clear();
        treeMap.clear();
        initialized = false;
    }
};
