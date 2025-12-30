#pragma once

#include "Chunk.h"
#include "Block.h"
#include <FastNoiseLite.h>
#include <random>
#include <cmath>

// Biome types - expanded for continentalness-based generation
enum class Biome {
    // Water biomes
    DEEP_OCEAN,
    OCEAN,
    BEACH,
    RIVER,

    // Land biomes
    PLAINS,
    FOREST,
    DARK_FOREST,
    BIRCH_FOREST,
    TAIGA,           // Cold forest with spruce
    DESERT,
    BADLANDS,        // Red/orange mesa terrain
    SAVANNA,
    SWAMP,
    SNOW,
    SNOW_TAIGA,
    MOUNTAINS,
    MOUNTAIN_MEADOW,
    FROZEN_PEAKS,
    JAGGED_PEAKS,
    STONY_PEAKS
};

class TerrainGenerator {
public:
    // World seed
    int seed;

    // Noise generators - Core terrain
    FastNoiseLite continentNoise;    // Large scale continent/ocean distribution
    FastNoiseLite erosionNoise;      // Terrain sharpness (high = flat, low = steep)
    FastNoiseLite peaksValleysNoise; // Peaks and valleys (PV) - ridgeline features
    FastNoiseLite mountainNoise;     // Mountain peaks (ridged fractal)
    FastNoiseLite detailNoise;       // Small terrain details
    FastNoiseLite riverNoise;        // River carving noise

    // Noise generators - Caves
    FastNoiseLite caveNoise;         // 3D cave carving
    FastNoiseLite caveNoise2;        // Secondary cave noise for variety
    FastNoiseLite oreNoise;          // Ore distribution
    FastNoiseLite aquiferNoise;      // Aquifer zones (where water spawns in caves)

    // Noise generators - Biomes
    FastNoiseLite temperatureNoise;  // Biome temperature
    FastNoiseLite humidityNoise;     // Biome humidity
    FastNoiseLite weirdnessNoise;    // Biome weirdness (rare biome variants)
    FastNoiseLite blendNoise;        // Noise for biome blend randomization

    // Biome blending parameters
    static constexpr int BLEND_RADIUS = 4;        // Blend radius in blocks
    static constexpr float BLEND_SCALE = 8.0f;    // Scale for blend sampling

    // Terrain parameters
    int seaLevel = 62;
    int baseHeight = 64;
    int maxHeight = 128;
    int bedrockHeight = 5;

    TerrainGenerator(int worldSeed = 12345) : seed(worldSeed) {
        setupNoiseGenerators();
    }

    void setSeed(int newSeed) {
        seed = newSeed;
        setupNoiseGenerators();
    }

    // Generate terrain for a chunk
    void generateChunk(Chunk& chunk) {
        glm::ivec2 chunkPos = chunk.position;

        // First pass: Generate base terrain with height map and biome data
        for (int x = 0; x < CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                // World coordinates
                int worldX = chunkPos.x * CHUNK_SIZE_X + x;
                int worldZ = chunkPos.y * CHUNK_SIZE_Z + z;

                // Sample biome temperature and humidity for this column
                // Map from noise range (-1 to +1) to uint8_t range (0 to 255)
                float temp = temperatureNoise.GetNoise(static_cast<float>(worldX), static_cast<float>(worldZ));
                float humid = humidityNoise.GetNoise(static_cast<float>(worldX), static_cast<float>(worldZ));
                int colIdx = x + z * CHUNK_SIZE_X;
                chunk.biomeTemperature[colIdx] = static_cast<uint8_t>((temp + 1.0f) * 0.5f * 255.0f);
                chunk.biomeHumidity[colIdx] = static_cast<uint8_t>((humid + 1.0f) * 0.5f * 255.0f);

                // Calculate terrain height at this position
                int terrainHeight = getTerrainHeight(worldX, worldZ);

                // Fill column with blocks
                for (int y = 0; y < CHUNK_SIZE_Y; y++) {
                    BlockType block = getBlockAt(worldX, y, worldZ, terrainHeight);
                    chunk.setBlock(x, y, z, block);
                }
            }
        }

        // Second pass: Carve caves
        carveCaves(chunk);

        // Third pass: Add ores
        generateOres(chunk);

        // Fourth pass: Add trees and decorations
        generateDecorations(chunk);
    }

private:
    void setupNoiseGenerators() {
        // ============================================
        // CONTINENTALNESS - Very large scale, determines ocean vs land
        // Range: -1 (deep ocean) to +1 (inland)
        // ============================================
        continentNoise.SetSeed(seed);
        continentNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        continentNoise.SetFrequency(0.0008f);  // Very low frequency for large continents
        continentNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        continentNoise.SetFractalOctaves(5);
        continentNoise.SetFractalLacunarity(2.0f);
        continentNoise.SetFractalGain(0.5f);

        // ============================================
        // EROSION - Controls terrain sharpness
        // Range: -1 (very steep/sharp) to +1 (flat/eroded)
        // ============================================
        erosionNoise.SetSeed(seed + 1);
        erosionNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        erosionNoise.SetFrequency(0.002f);
        erosionNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        erosionNoise.SetFractalOctaves(3);

        // ============================================
        // PEAKS & VALLEYS (PV) - Creates ridgelines and valleys
        // Range: -1 (deep valley) to +1 (sharp peak)
        // ============================================
        peaksValleysNoise.SetSeed(seed + 2);
        peaksValleysNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        peaksValleysNoise.SetFrequency(0.004f);
        peaksValleysNoise.SetFractalType(FastNoiseLite::FractalType_Ridged);
        peaksValleysNoise.SetFractalOctaves(4);

        // Mountain noise - ridged fractal for sharp peaks
        mountainNoise.SetSeed(seed + 3);
        mountainNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        mountainNoise.SetFrequency(0.006f);
        mountainNoise.SetFractalType(FastNoiseLite::FractalType_Ridged);
        mountainNoise.SetFractalOctaves(4);

        // Detail noise - small bumps and variation
        detailNoise.SetSeed(seed + 4);
        detailNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        detailNoise.SetFrequency(0.02f);
        detailNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        detailNoise.SetFractalOctaves(3);

        // River noise - winding river paths
        riverNoise.SetSeed(seed + 5);
        riverNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        riverNoise.SetFrequency(0.003f);
        riverNoise.SetFractalType(FastNoiseLite::FractalType_Ridged);
        riverNoise.SetFractalOctaves(2);

        // ============================================
        // CAVE NOISE GENERATORS
        // ============================================
        caveNoise.SetSeed(seed + 10);
        caveNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        caveNoise.SetFrequency(0.04f);
        caveNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        caveNoise.SetFractalOctaves(2);

        caveNoise2.SetSeed(seed + 11);
        caveNoise2.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        caveNoise2.SetFrequency(0.05f);

        oreNoise.SetSeed(seed + 12);
        oreNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        oreNoise.SetFrequency(0.1f);

        aquiferNoise.SetSeed(seed + 13);
        aquiferNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        aquiferNoise.SetFrequency(0.02f);

        // ============================================
        // BIOME NOISE GENERATORS
        // ============================================
        // Temperature - affects hot/cold biomes
        temperatureNoise.SetSeed(seed + 20);
        temperatureNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        temperatureNoise.SetFrequency(0.0012f);
        temperatureNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        temperatureNoise.SetFractalOctaves(3);

        // Humidity - affects wet/dry biomes
        humidityNoise.SetSeed(seed + 21);
        humidityNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        humidityNoise.SetFrequency(0.0015f);
        humidityNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        humidityNoise.SetFractalOctaves(3);

        // Weirdness - determines rare/unusual biome variants
        weirdnessNoise.SetSeed(seed + 22);
        weirdnessNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        weirdnessNoise.SetFrequency(0.002f);
        weirdnessNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        weirdnessNoise.SetFractalOctaves(2);

        // Blend noise - adds natural variation to biome boundaries
        blendNoise.SetSeed(seed + 30);
        blendNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        blendNoise.SetFrequency(0.05f);
        blendNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        blendNoise.SetFractalOctaves(2);
    }

    // ============================================
    // TERRAIN DATA STRUCTURE
    // Stores all noise values for a position
    // ============================================
    struct TerrainData {
        float continentalness;  // -1 (ocean) to +1 (inland)
        float erosion;          // -1 (steep) to +1 (flat)
        float peaksValleys;     // -1 (valley) to +1 (peak)
        float temperature;      // -1 (cold) to +1 (hot)
        float humidity;         // -1 (dry) to +1 (wet)
        float weirdness;        // -1 to +1 (biome variants)
        float river;            // River proximity
        int height;             // Final terrain height
        Biome biome;            // Selected biome
    };

    // Get all terrain data for a position (used for blending)
    TerrainData getTerrainData(int worldX, int worldZ) {
        float x = static_cast<float>(worldX);
        float z = static_cast<float>(worldZ);

        TerrainData data;

        // Sample all noise values
        data.continentalness = continentNoise.GetNoise(x, z);
        data.erosion = erosionNoise.GetNoise(x, z);
        data.peaksValleys = peaksValleysNoise.GetNoise(x, z);
        data.temperature = temperatureNoise.GetNoise(x, z);
        data.humidity = humidityNoise.GetNoise(x, z);
        data.weirdness = weirdnessNoise.GetNoise(x, z);
        data.river = riverNoise.GetNoise(x, z);

        // Calculate height based on terrain parameters
        data.height = calculateHeight(data.continentalness, data.erosion,
                                      data.peaksValleys, x, z);

        // Determine biome
        data.biome = selectBiome(data);

        return data;
    }

    // Calculate terrain height using continentalness, erosion, and PV
    int calculateHeight(float continentalness, float erosion, float pv,
                        float x, float z) {
        // ============================================
        // CONTINENTALNESS HEIGHT MAPPING
        // Maps continentalness to base terrain height
        // ============================================
        float baseTerrainHeight;

        if (continentalness < -0.4f) {
            // Deep ocean: -1 to -0.4 → height 20-45
            float t = (continentalness + 1.0f) / 0.6f;  // 0 to 1
            baseTerrainHeight = 20.0f + t * 25.0f;
        }
        else if (continentalness < -0.1f) {
            // Ocean/Coast transition: -0.4 to -0.1 → height 45-60
            float t = (continentalness + 0.4f) / 0.3f;
            baseTerrainHeight = 45.0f + t * 15.0f;
        }
        else if (continentalness < 0.2f) {
            // Coast/Lowlands: -0.1 to 0.2 → height 60-70
            float t = (continentalness + 0.1f) / 0.3f;
            baseTerrainHeight = 60.0f + t * 10.0f;
        }
        else if (continentalness < 0.5f) {
            // Inland plains: 0.2 to 0.5 → height 70-85
            float t = (continentalness - 0.2f) / 0.3f;
            baseTerrainHeight = 70.0f + t * 15.0f;
        }
        else {
            // High inland/Mountains: 0.5 to 1.0 → height 85-120
            float t = (continentalness - 0.5f) / 0.5f;
            baseTerrainHeight = 85.0f + t * 35.0f;
        }

        // ============================================
        // EROSION MODIFIER
        // High erosion = flatter terrain, low erosion = more variation
        // ============================================
        float erosionFactor = 1.0f - (erosion + 1.0f) * 0.3f;  // 0.4 to 1.0
        erosionFactor = std::max(0.3f, erosionFactor);

        // ============================================
        // PEAKS & VALLEYS CONTRIBUTION
        // Adds ridgelines and valley cuts
        // ============================================
        float pvContribution = 0.0f;
        if (continentalness > 0.3f) {  // Only add PV variation inland
            // Transform PV from ridged noise to usable range
            float pvValue = (pv + 1.0f) * 0.5f;  // 0 to 1
            pvValue = pvValue * pvValue;  // Square for sharper peaks

            // Scale by continentalness (more dramatic inland)
            float pvScale = (continentalness - 0.3f) / 0.7f;  // 0 to 1
            pvContribution = pvValue * pvScale * 40.0f * erosionFactor;
        }

        // ============================================
        // MOUNTAIN PEAKS (additional ridged noise)
        // ============================================
        float mountainValue = (mountainNoise.GetNoise(x, z) + 1.0f) * 0.5f;
        mountainValue = mountainValue * mountainValue * mountainValue;  // Cube for sharper peaks

        float mountainContribution = 0.0f;
        if (continentalness > 0.4f && erosion < 0.2f) {
            // Mountains only in high continental areas with low erosion
            float mountainScale = (continentalness - 0.4f) / 0.6f;
            mountainScale *= (0.2f - erosion) / 1.2f;
            mountainContribution = mountainValue * mountainScale * 50.0f;
        }

        // ============================================
        // DETAIL NOISE
        // Small-scale terrain variation
        // ============================================
        float detail = detailNoise.GetNoise(x, z);
        float detailContribution = detail * 3.0f * erosionFactor;

        // ============================================
        // COMBINE ALL CONTRIBUTIONS
        // ============================================
        float finalHeight = baseTerrainHeight + pvContribution +
                           mountainContribution + detailContribution;

        // Clamp to valid range
        int height = static_cast<int>(finalHeight);
        height = std::max(1, std::min(height, maxHeight));

        return height;
    }

    // Select biome based on terrain data
    Biome selectBiome(const TerrainData& data) {
        float c = data.continentalness;
        float e = data.erosion;
        float t = data.temperature;
        float h = data.humidity;
        float w = data.weirdness;
        float pv = data.peaksValleys;

        // ============================================
        // OCEAN BIOMES
        // ============================================
        if (c < -0.4f) {
            return Biome::DEEP_OCEAN;
        }
        if (c < -0.1f) {
            return Biome::OCEAN;
        }

        // ============================================
        // BEACH BIOME (coast with low erosion)
        // ============================================
        if (c < 0.05f && e > 0.0f) {
            return Biome::BEACH;
        }

        // ============================================
        // RIVER CHECK (narrow band where river noise is high)
        // ============================================
        if (c > 0.0f && std::abs(data.river) > 0.85f && data.height > seaLevel) {
            return Biome::RIVER;
        }

        // ============================================
        // MOUNTAIN BIOMES (high continental + low erosion + high PV)
        // ============================================
        if (c > 0.5f && e < 0.0f) {
            if (t < -0.3f) {
                // Cold mountains
                if (pv > 0.5f) return Biome::FROZEN_PEAKS;
                return Biome::SNOW_TAIGA;
            }
            if (pv > 0.6f) {
                // Sharp peaks
                if (t < 0.0f) return Biome::JAGGED_PEAKS;
                return Biome::STONY_PEAKS;
            }
            if (pv > 0.2f) {
                return Biome::MOUNTAINS;
            }
            return Biome::MOUNTAIN_MEADOW;
        }

        // ============================================
        // LAND BIOMES (based on temperature and humidity)
        // ============================================

        // COLD BIOMES (temperature < -0.3)
        if (t < -0.3f) {
            if (h > 0.3f) return Biome::SNOW_TAIGA;
            if (h > 0.0f) return Biome::TAIGA;
            return Biome::SNOW;
        }

        // COOL BIOMES (-0.3 to 0.0)
        if (t < 0.0f) {
            if (h > 0.4f) return Biome::DARK_FOREST;
            if (h > 0.1f) return Biome::TAIGA;
            if (h > -0.2f) return Biome::BIRCH_FOREST;
            return Biome::PLAINS;
        }

        // WARM BIOMES (0.0 to 0.4)
        if (t < 0.4f) {
            if (h > 0.5f) return Biome::SWAMP;
            if (h > 0.2f) return Biome::FOREST;
            if (h > -0.1f) return Biome::PLAINS;
            return Biome::SAVANNA;
        }

        // HOT BIOMES (temperature > 0.4)
        if (h < -0.3f) {
            // Hot and dry - check for badlands (rare)
            if (w > 0.5f) return Biome::BADLANDS;
            return Biome::DESERT;
        }
        if (h < 0.1f) return Biome::SAVANNA;
        if (h > 0.4f) return Biome::SWAMP;
        return Biome::PLAINS;
    }

    // Legacy getBiome function - now uses TerrainData internally
    Biome getBiome(int worldX, int worldZ) {
        TerrainData data = getTerrainData(worldX, worldZ);
        return data.biome;
    }

    // ============================================
    // BIOME BLENDING SYSTEM
    // Smoothly blends terrain heights at biome boundaries
    // ============================================

    // Get raw (unblended) terrain height - used for blending samples
    int getHeightRaw(float x, float z) {
        float continentalness = continentNoise.GetNoise(x, z);
        float erosion = erosionNoise.GetNoise(x, z);
        float pv = peaksValleysNoise.GetNoise(x, z);
        return calculateHeight(continentalness, erosion, pv, x, z);
    }

    // Check if two biomes should blend (same category)
    bool shouldBlendBiomes(Biome a, Biome b) {
        // Don't blend if same biome
        if (a == b) return true;

        // Define biome categories for blending
        auto getCategory = [](Biome biome) -> int {
            switch (biome) {
                // Water biomes - category 0
                case Biome::DEEP_OCEAN:
                case Biome::OCEAN:
                    return 0;

                // Beach/coastal - category 1
                case Biome::BEACH:
                case Biome::RIVER:
                    return 1;

                // Cold biomes - category 2
                case Biome::SNOW:
                case Biome::SNOW_TAIGA:
                case Biome::FROZEN_PEAKS:
                case Biome::TAIGA:
                    return 2;

                // Temperate biomes - category 3
                case Biome::PLAINS:
                case Biome::FOREST:
                case Biome::DARK_FOREST:
                case Biome::BIRCH_FOREST:
                case Biome::SWAMP:
                    return 3;

                // Hot biomes - category 4
                case Biome::DESERT:
                case Biome::BADLANDS:
                case Biome::SAVANNA:
                    return 4;

                // Mountain biomes - category 5
                case Biome::MOUNTAINS:
                case Biome::MOUNTAIN_MEADOW:
                case Biome::JAGGED_PEAKS:
                case Biome::STONY_PEAKS:
                    return 5;

                default:
                    return 3;
            }
        };

        int catA = getCategory(a);
        int catB = getCategory(b);

        // Allow blending within same category or adjacent categories
        return std::abs(catA - catB) <= 1;
    }

    // Get blend weight based on distance (smooth falloff)
    float getBlendWeight(float distance, float maxDist) {
        if (distance >= maxDist) return 0.0f;
        // Smooth cubic falloff for natural transitions
        float t = 1.0f - (distance / maxDist);
        return t * t * (3.0f - 2.0f * t);  // Smoothstep
    }

    // Get terrain height with biome blending
    int getBlendedTerrainHeight(int worldX, int worldZ) {
        float x = static_cast<float>(worldX);
        float z = static_cast<float>(worldZ);

        // Get center point data
        TerrainData centerData = getTerrainData(worldX, worldZ);
        Biome centerBiome = centerData.biome;
        int centerHeight = centerData.height;

        // Add noise-based offset to break up grid patterns
        float noiseOffsetX = blendNoise.GetNoise(x * 0.5f, z * 0.5f) * 2.0f;
        float noiseOffsetZ = blendNoise.GetNoise(x * 0.5f + 100.0f, z * 0.5f + 100.0f) * 2.0f;

        // Sample surrounding points for blending
        float totalWeight = 1.0f;  // Center point weight
        float weightedHeight = static_cast<float>(centerHeight);

        // Sample in a pattern around the center
        const int sampleOffsets[][2] = {
            {-BLEND_RADIUS, 0}, {BLEND_RADIUS, 0},
            {0, -BLEND_RADIUS}, {0, BLEND_RADIUS},
            {-BLEND_RADIUS, -BLEND_RADIUS}, {BLEND_RADIUS, -BLEND_RADIUS},
            {-BLEND_RADIUS, BLEND_RADIUS}, {BLEND_RADIUS, BLEND_RADIUS}
        };

        for (const auto& offset : sampleOffsets) {
            float sampleX = x + offset[0] + noiseOffsetX;
            float sampleZ = z + offset[1] + noiseOffsetZ;

            // Get biome at sample point
            float sampleCont = continentNoise.GetNoise(sampleX, sampleZ);
            float sampleEros = erosionNoise.GetNoise(sampleX, sampleZ);
            float samplePV = peaksValleysNoise.GetNoise(sampleX, sampleZ);
            float sampleTemp = temperatureNoise.GetNoise(sampleX, sampleZ);
            float sampleHum = humidityNoise.GetNoise(sampleX, sampleZ);
            float sampleWeird = weirdnessNoise.GetNoise(sampleX, sampleZ);
            float sampleRiver = riverNoise.GetNoise(sampleX, sampleZ);

            // Build temporary TerrainData for biome selection
            TerrainData sampleData;
            sampleData.continentalness = sampleCont;
            sampleData.erosion = sampleEros;
            sampleData.peaksValleys = samplePV;
            sampleData.temperature = sampleTemp;
            sampleData.humidity = sampleHum;
            sampleData.weirdness = sampleWeird;
            sampleData.river = sampleRiver;
            sampleData.height = calculateHeight(sampleCont, sampleEros, samplePV, sampleX, sampleZ);
            sampleData.biome = selectBiome(sampleData);

            // Check if this sample should contribute to blending
            if (sampleData.biome != centerBiome && shouldBlendBiomes(centerBiome, sampleData.biome)) {
                // Calculate distance-based weight
                float dist = std::sqrt(static_cast<float>(offset[0] * offset[0] + offset[1] * offset[1]));
                float weight = getBlendWeight(dist, BLEND_SCALE) * 0.5f;  // Reduce influence

                weightedHeight += sampleData.height * weight;
                totalWeight += weight;
            }
        }

        // Return weighted average
        return static_cast<int>(weightedHeight / totalWeight);
    }

    // Get terrain height using the new blended system
    int getTerrainHeight(int worldX, int worldZ) {
        return getBlendedTerrainHeight(worldX, worldZ);
    }

    // Determine block type at position
    BlockType getBlockAt(int worldX, int y, int worldZ, int terrainHeight) {
        // Bedrock layer (with some variation)
        if (y == 0) {
            return BlockType::BEDROCK;
        }
        if (y < bedrockHeight) {
            // Random bedrock in bottom layers
            std::hash<int> hasher;
            size_t h = hasher(worldX * 31337 + y * 7919 + worldZ * 104729 + seed);
            if ((h % 100) < static_cast<size_t>(100 - y * 20)) {
                return BlockType::BEDROCK;
            }
        }

        // Above terrain = air (or water if below sea level AND terrain is below sea level)
        // This creates oceans/lakes on the surface, but NOT water in caves
        if (y > terrainHeight) {
            // Only fill with water if this is a surface water body (terrain is below sea level)
            // This prevents caves from being flooded
            if (y <= seaLevel && terrainHeight < seaLevel) {
                return BlockType::WATER;
            }
            return BlockType::AIR;
        }

        // Get biome for surface block decisions
        Biome biome = getBiome(worldX, worldZ);

        // Surface block
        if (y == terrainHeight) {
            // Underwater surfaces
            if (terrainHeight < seaLevel - 3) {
                // Deep underwater
                switch (biome) {
                    case Biome::DEEP_OCEAN:
                    case Biome::OCEAN:
                        return BlockType::GRAVEL;  // Ocean floor is gravel
                    case Biome::RIVER:
                        return BlockType::SAND;    // River beds are sandy
                    default:
                        return BlockType::SAND;
                }
            } else if (terrainHeight < seaLevel + 2) {
                // Beach/shallow water
                return BlockType::SAND;
            } else {
                // Above water - surface block based on biome
                switch (biome) {
                    // Water biomes that got above sea level (rare transition)
                    case Biome::DEEP_OCEAN:
                    case Biome::OCEAN:
                    case Biome::BEACH:
                        return BlockType::SAND;
                    case Biome::RIVER:
                        return BlockType::GRASS;  // River banks

                    // Desert/Hot biomes
                    case Biome::DESERT:
                        return BlockType::SAND;
                    case Biome::BADLANDS:
                        return BlockType::SAND;  // Red sand would be ideal, using sand for now
                    case Biome::SAVANNA:
                        return BlockType::GRASS;  // Dry grass (could use coarse dirt)

                    // Cold biomes
                    case Biome::SNOW:
                    case Biome::FROZEN_PEAKS:
                        return BlockType::SNOW_BLOCK;
                    case Biome::SNOW_TAIGA:
                        return BlockType::SNOW_BLOCK;  // Snowy ground with trees
                    case Biome::TAIGA:
                        return BlockType::GRASS;  // Could use podzol

                    // Forest biomes
                    case Biome::FOREST:
                    case Biome::DARK_FOREST:
                    case Biome::BIRCH_FOREST:
                        return BlockType::GRASS;

                    // Wet biomes
                    case Biome::SWAMP:
                        return BlockType::GRASS;  // Swamp grass

                    // Mountain biomes
                    case Biome::MOUNTAINS:
                    case Biome::JAGGED_PEAKS:
                    case Biome::STONY_PEAKS:
                        if (terrainHeight > 100) {
                            return BlockType::SNOW_BLOCK;  // Snow-capped peaks
                        } else if (terrainHeight > 85) {
                            return BlockType::STONE;  // Rocky upper mountains
                        }
                        return BlockType::GRASS;  // Lower mountain slopes
                    case Biome::MOUNTAIN_MEADOW:
                        return BlockType::GRASS;  // Grassy mountain meadows

                    // Default
                    case Biome::PLAINS:
                    default:
                        return BlockType::GRASS;
                }
            }
        }

        // Just below surface (1-4 blocks depth)
        if (y > terrainHeight - 4) {
            // Underwater layers
            if (terrainHeight < seaLevel) {
                switch (biome) {
                    case Biome::DEEP_OCEAN:
                    case Biome::OCEAN:
                        return BlockType::GRAVEL;
                    default:
                        return BlockType::SAND;
                }
            }

            // Above-water subsurface
            switch (biome) {
                case Biome::DESERT:
                case Biome::BEACH:
                    return BlockType::SAND;
                case Biome::BADLANDS:
                    return BlockType::SAND;  // Terracotta layers would be ideal
                case Biome::SNOW:
                case Biome::SNOW_TAIGA:
                case Biome::FROZEN_PEAKS:
                    return BlockType::DIRT;
                case Biome::MOUNTAINS:
                case Biome::JAGGED_PEAKS:
                case Biome::STONY_PEAKS:
                    return BlockType::STONE;
                case Biome::MOUNTAIN_MEADOW:
                    return BlockType::DIRT;
                case Biome::SWAMP:
                    // Swamps have gravel deposits under the surface
                    if (y == terrainHeight - 1) {
                        return BlockType::DIRT;
                    }
                    return BlockType::GRAVEL;
                default:
                    return BlockType::DIRT;
            }
        }

        // Deep underground = stone
        return BlockType::STONE;
    }

    // Carve cave systems - Minecraft Caves & Cliffs style
    // Creates massive "cheese caves", spaghetti tunnels, and surface openings
    void carveCaves(Chunk& chunk) {
        glm::ivec2 chunkPos = chunk.position;

        for (int x = 0; x < CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                int worldX = chunkPos.x * CHUNK_SIZE_X + x;
                int worldZ = chunkPos.y * CHUNK_SIZE_Z + z;

                // Get terrain height ONCE per column
                int terrainHeight = getTerrainHeight(worldX, worldZ);

                for (int y = 1; y < CHUNK_SIZE_Y - 1; y++) {
                    // Don't carve through bedrock
                    BlockType current = chunk.getBlock(x, y, z);
                    if (current == BlockType::BEDROCK || current == BlockType::AIR ||
                        current == BlockType::WATER) {
                        continue;
                    }

                    // Skip if above terrain
                    if (y > terrainHeight) continue;

                    float fx = static_cast<float>(worldX);
                    float fy = static_cast<float>(y);
                    float fz = static_cast<float>(worldZ);

                    // ============================================
                    // CHEESE CAVES - Large open caverns (like Swiss cheese holes)
                    // Use very low frequency noise for massive blobs
                    // ============================================
                    float cheeseNoise = caveNoise.GetNoise(fx * 0.4f, fy * 0.25f, fz * 0.4f);

                    // Secondary cheese layer for variety
                    float cheese2 = caveNoise2.GetNoise(fx * 0.3f, fy * 0.2f, fz * 0.3f);

                    // Combine for more interesting shapes
                    float cheeseValue = (cheeseNoise + cheese2 * 0.5f) / 1.5f;

                    // Cheese caves carve where noise is high
                    float cheeseThreshold = 0.55f;

                    // Make caves more common at mid-depths
                    if (y > 20 && y < 60) {
                        cheeseThreshold = 0.48f;  // Much more cheese caves here
                    } else if (y < 20) {
                        cheeseThreshold = 0.52f;  // Still good caves deep down
                    }

                    bool isCheeseCave = cheeseValue > cheeseThreshold;

                    // ============================================
                    // SPAGHETTI CAVES - Wide winding tunnels (3x3+ walkable)
                    // Use lower frequency for wider, more organic tunnels
                    // ============================================

                    // Main spaghetti tunnel - lower frequency = wider
                    float spaghetti1 = caveNoise.GetNoise(fx * 0.7f, fy * 0.7f, fz * 0.7f);
                    float spaghetti2 = caveNoise2.GetNoise(fx * 0.7f, fy * 1.2f, fz * 0.7f);
                    float spaghettiValue = std::abs(spaghetti1) + std::abs(spaghetti2);

                    // Large walkable tunnels (3x3+) - very generous threshold
                    float spaghettiThreshold = 0.28f;

                    // Secondary layer for even wider sections
                    float wide1 = caveNoise.GetNoise(fx * 0.5f, fy * 0.4f, fz * 0.5f);
                    float wide2 = caveNoise2.GetNoise(fx * 0.4f, fy * 0.5f, fz * 0.4f);
                    float wideValue = std::abs(wide1) + std::abs(wide2);

                    // Extra wide tunnel sections (4-5 blocks wide)
                    bool isWideTunnel = wideValue < 0.22f;

                    // Smaller connecting tunnels (still 2-3 blocks)
                    float small1 = caveNoise.GetNoise(fx * 1.2f, fy * 1.0f, fz * 1.2f);
                    float small2 = caveNoise2.GetNoise(fx * 1.1f, fy * 1.5f, fz * 1.1f);
                    float smallValue = std::abs(small1) + std::abs(small2);
                    bool isSmallTunnel = smallValue < 0.12f;

                    bool isSpaghettiCave = spaghettiValue < spaghettiThreshold || isWideTunnel || isSmallTunnel;

                    // ============================================
                    // SURFACE OPENINGS - Cave entrances visible from above
                    // Only create entrances above sea level (not underwater)
                    // ============================================
                    int depth = terrainHeight - y;
                    bool isSurfaceOpening = false;

                    // Check for surface cave entrances
                    // IMPORTANT: Only create cave openings if terrain is above sea level + 3
                    // This prevents cave entrances from spawning in or near water bodies
                    if (depth >= 0 && depth < 20 && terrainHeight > seaLevel + 3) {
                        // Use a separate noise for surface openings
                        float entranceNoise = caveNoise.GetNoise(fx * 0.8f, fz * 0.8f);
                        float entranceNoise2 = caveNoise2.GetNoise(fx * 0.5f, fz * 0.5f);

                        // Create circular/oval openings that lead down
                        if (entranceNoise > 0.6f && entranceNoise2 > 0.3f) {
                            // Opening gets wider as you go deeper (funnel shape)
                            float openingStrength = (entranceNoise - 0.6f) * 3.0f;
                            float depthFactor = 1.0f + depth * 0.05f;

                            if (openingStrength * depthFactor > 0.5f) {
                                isSurfaceOpening = true;
                            }
                        }
                    }

                    // ============================================
                    // CARVE THE CAVE
                    // ============================================
                    if (isCheeseCave || isSpaghettiCave || isSurfaceOpening) {
                        // Don't carve grass/snow on surface unless it's an opening
                        if ((current == BlockType::GRASS || current == BlockType::SNOW_BLOCK)
                            && !isSurfaceOpening) {
                            continue;
                        }

                        // Caves are AIR by default - NO water unless very specific conditions
                        BlockType caveBlock = BlockType::AIR;

                        // ============================================
                        // AQUIFER SYSTEM - Small water pools & rare underwater caves
                        // ============================================

                        int distanceToSurface = terrainHeight - y;

                        // First check: Is this an UNDERWATER CAVE BIOME (~5% of caves)?
                        // Uses large-scale 2D noise so entire cave sections are underwater
                        float underwaterBiomeNoise = aquiferNoise.GetNoise(fx * 0.02f, fz * 0.02f);
                        bool isUnderwaterCaveBiome = underwaterBiomeNoise > 0.92f;  // ~3-4% of areas (very rare)

                        if (isUnderwaterCaveBiome && y < 40 && y > 10 && distanceToSurface > 25) {
                            // Entire underwater cave - fill with water
                            caveBlock = BlockType::WATER;
                        }
                        // Second check: Small isolated pools (NOT in underwater biome)
                        // These should be VERY rare - just little puddles on cave floors
                        else if (distanceToSurface > 30 && y < 25 && y > 5) {
                            // Use 3D noise for LOCALIZED pools (not vertical columns)
                            float poolNoise = aquiferNoise.GetNoise(fx * 1.5f, fy * 1.5f, fz * 1.5f);

                            // Extremely strict threshold - only ~1% of locations
                            bool isPoolLocation = poolNoise > 0.94f;

                            if (isPoolLocation) {
                                // Only place water/lava on the FLOOR of the cave
                                BlockType below = chunk.getBlock(x, y - 1, z);
                                bool hasFloor = (below == BlockType::STONE || below == BlockType::DIRT ||
                                                below == BlockType::SAND || below == BlockType::GRAVEL);

                                if (hasFloor) {
                                    // Small lava pools below Y=11, small water pools above
                                    if (y <= 11) {
                                        caveBlock = BlockType::LAVA;
                                    } else {
                                        caveBlock = BlockType::WATER;
                                    }
                                }
                            }
                        }

                        chunk.setBlock(x, y, z, caveBlock);

                        // Add features to caves
                        std::hash<int> hasher;
                        size_t h = hasher(worldX * 31337 + y * 7919 + worldZ * 104729 + seed);

                        // Glowstone clusters on ceilings (only in air caves)
                        if (caveBlock == BlockType::AIR && y < 50 && y > 5) {
                            if (y + 1 < CHUNK_SIZE_Y) {
                                BlockType above = chunk.getBlock(x, y + 1, z);
                                if (above == BlockType::STONE) {
                                    if ((h % 150) < 4) {  // ~2.5% chance
                                        chunk.setBlock(x, y + 1, z, BlockType::GLOWSTONE);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Generate ore deposits
    void generateOres(Chunk& chunk) {
        glm::ivec2 chunkPos = chunk.position;
        std::mt19937 rng(seed + chunkPos.x * 31337 + chunkPos.y * 7919);

        // Ore definitions: type, minY, maxY, veinsPerChunk, veinSize
        struct OreConfig {
            BlockType type;
            int minY;
            int maxY;
            int veinsPerChunk;
            int veinSize;
        };

        OreConfig ores[] = {
            { BlockType::COAL_ORE,    5, 128, 20, 17 },
            { BlockType::IRON_ORE,    1,  64, 15, 9 },
            { BlockType::GOLD_ORE,    1,  32,  4, 9 },
            { BlockType::DIAMOND_ORE, 1,  16,  2, 8 },
        };

        for (const auto& ore : ores) {
            std::uniform_int_distribution<int> xDist(0, CHUNK_SIZE_X - 1);
            std::uniform_int_distribution<int> zDist(0, CHUNK_SIZE_Z - 1);
            std::uniform_int_distribution<int> yDist(ore.minY, ore.maxY);

            for (int v = 0; v < ore.veinsPerChunk; v++) {
                int startX = xDist(rng);
                int startY = yDist(rng);
                int startZ = zDist(rng);

                // Simple blob ore vein
                generateOreVein(chunk, startX, startY, startZ, ore.type, ore.veinSize, rng);
            }
        }
    }

    void generateOreVein(Chunk& chunk, int startX, int startY, int startZ,
                         BlockType oreType, int size, std::mt19937& rng) {
        std::uniform_real_distribution<float> dist(-1.5f, 1.5f);

        float x = static_cast<float>(startX);
        float y = static_cast<float>(startY);
        float z = static_cast<float>(startZ);

        for (int i = 0; i < size; i++) {
            int ix = static_cast<int>(x);
            int iy = static_cast<int>(y);
            int iz = static_cast<int>(z);

            // Place ore if within chunk and currently stone
            if (ix >= 0 && ix < CHUNK_SIZE_X &&
                iy >= 1 && iy < CHUNK_SIZE_Y - 1 &&
                iz >= 0 && iz < CHUNK_SIZE_Z) {

                BlockType current = chunk.getBlock(ix, iy, iz);
                if (current == BlockType::STONE) {
                    chunk.setBlock(ix, iy, iz, oreType);
                }
            }

            // Random walk
            x += dist(rng);
            y += dist(rng) * 0.5f;
            z += dist(rng);
        }
    }

    // Generate trees and other decorations based on biome
    void generateDecorations(Chunk& chunk) {
        glm::ivec2 chunkPos = chunk.position;
        std::mt19937 rng(seed + chunkPos.x * 73856093 + chunkPos.y * 19349663);
        std::uniform_int_distribution<int> chanceDist(0, 100);

        for (int x = 2; x < CHUNK_SIZE_X - 2; x++) {
            for (int z = 2; z < CHUNK_SIZE_Z - 2; z++) {
                int worldX = chunkPos.x * CHUNK_SIZE_X + x;
                int worldZ = chunkPos.y * CHUNK_SIZE_Z + z;
                Biome biome = getBiome(worldX, worldZ);

                // Find surface
                for (int y = CHUNK_SIZE_Y - 10; y > seaLevel; y--) {
                    BlockType surfaceBlock = chunk.getBlock(x, y, z);

                    // Check for valid surface based on biome
                    bool isSurface = (surfaceBlock == BlockType::GRASS) ||
                                     (surfaceBlock == BlockType::SAND &&
                                      (biome == Biome::DESERT || biome == Biome::BEACH || biome == Biome::BADLANDS)) ||
                                     (surfaceBlock == BlockType::SNOW_BLOCK &&
                                      (biome == Biome::SNOW || biome == Biome::SNOW_TAIGA || biome == Biome::FROZEN_PEAKS));

                    if (isSurface) {
                        int chance = chanceDist(rng);

                        switch (biome) {
                            // ============================================
                            // FOREST BIOMES - Dense trees
                            // ============================================
                            case Biome::FOREST:
                                // Dense oak trees (8% chance)
                                if (chance < 8) {
                                    generateTree(chunk, x, y + 1, z, rng);
                                }
                                break;

                            case Biome::DARK_FOREST:
                                // Very dense dark oak trees (12% chance)
                                if (chance < 12) {
                                    generateDarkOakTree(chunk, x, y + 1, z, rng);
                                }
                                break;

                            case Biome::BIRCH_FOREST:
                                // Dense birch trees (8% chance)
                                if (chance < 8) {
                                    generateBirchTree(chunk, x, y + 1, z, rng);
                                }
                                break;

                            case Biome::TAIGA:
                                // Dense spruce trees (10% chance)
                                if (chance < 10) {
                                    generateSpruceTree(chunk, x, y + 1, z, rng);
                                }
                                break;

                            // ============================================
                            // COLD BIOMES - Sparse snowy trees
                            // ============================================
                            case Biome::SNOW:
                                // Sparse snowy trees (1% chance)
                                if (chance < 1) {
                                    generateSpruceTree(chunk, x, y + 1, z, rng);
                                }
                                break;

                            case Biome::SNOW_TAIGA:
                                // Moderate snowy spruce trees (6% chance)
                                if (chance < 6) {
                                    generateSpruceTree(chunk, x, y + 1, z, rng);
                                }
                                break;

                            case Biome::FROZEN_PEAKS:
                                // Very sparse, only at lower elevations
                                if (chance < 1 && y < 90) {
                                    generateSpruceTree(chunk, x, y + 1, z, rng);
                                }
                                break;

                            // ============================================
                            // GRASSLAND BIOMES
                            // ============================================
                            case Biome::PLAINS:
                                // Sparse trees in plains (1% chance)
                                if (chance < 1) {
                                    generateTree(chunk, x, y + 1, z, rng);
                                }
                                break;

                            case Biome::SAVANNA:
                                // Acacia-style trees (sparse, 2% chance)
                                if (chance < 2) {
                                    generateAcaciaTree(chunk, x, y + 1, z, rng);
                                }
                                break;

                            // ============================================
                            // WET BIOMES
                            // ============================================
                            case Biome::SWAMP:
                                // Swamp trees with vines (5% chance)
                                if (chance < 5) {
                                    generateSwampTree(chunk, x, y + 1, z, rng);
                                }
                                break;

                            case Biome::RIVER:
                                // Occasional trees along riverbanks (1% chance)
                                if (chance < 1) {
                                    generateTree(chunk, x, y + 1, z, rng);
                                }
                                break;

                            // ============================================
                            // HOT/DRY BIOMES
                            // ============================================
                            case Biome::DESERT:
                                // Cacti in desert (2% chance)
                                if (chance < 2) {
                                    generateCactus(chunk, x, y + 1, z, rng);
                                }
                                break;

                            case Biome::BADLANDS:
                                // Dead bushes only, no trees (rare cactus 1%)
                                if (chance < 1) {
                                    generateCactus(chunk, x, y + 1, z, rng);
                                }
                                break;

                            // ============================================
                            // MOUNTAIN BIOMES
                            // ============================================
                            case Biome::MOUNTAINS:
                            case Biome::JAGGED_PEAKS:
                            case Biome::STONY_PEAKS:
                                // Very sparse trees on lower mountains
                                if (chance < 1 && y < 85) {
                                    generateSpruceTree(chunk, x, y + 1, z, rng);
                                }
                                break;

                            case Biome::MOUNTAIN_MEADOW:
                                // Moderate trees in mountain meadows (3% chance)
                                if (chance < 3) {
                                    generateTree(chunk, x, y + 1, z, rng);
                                }
                                break;

                            // ============================================
                            // WATER BIOMES - No decorations above water
                            // ============================================
                            case Biome::DEEP_OCEAN:
                            case Biome::OCEAN:
                            case Biome::BEACH:
                                // No trees in water/beach biomes
                                break;

                            default:
                                break;
                        }
                        break;
                    }
                }
            }
        }
    }

    // Generate a spruce tree (tall, narrow, triangular)
    void generateSpruceTree(Chunk& chunk, int x, int baseY, int z, std::mt19937& rng) {
        std::uniform_int_distribution<int> heightDist(6, 10);
        int trunkHeight = heightDist(rng);

        if (baseY + trunkHeight + 2 >= CHUNK_SIZE_Y) return;

        // Trunk
        for (int y = 0; y < trunkHeight; y++) {
            chunk.setBlock(x, baseY + y, z, BlockType::WOOD_LOG);
        }

        // Leaves - triangular shape
        int leafStart = baseY + 2;
        for (int ly = leafStart; ly <= baseY + trunkHeight + 1; ly++) {
            int distFromTop = (baseY + trunkHeight + 1) - ly;
            int radius = std::min(2, distFromTop / 2 + 1);

            // Narrower at top
            if (ly > baseY + trunkHeight - 2) radius = 1;
            if (ly == baseY + trunkHeight + 1) radius = 0;

            for (int lx = -radius; lx <= radius; lx++) {
                for (int lz = -radius; lz <= radius; lz++) {
                    // Diamond shape for spruce
                    if (std::abs(lx) + std::abs(lz) > radius + 1) continue;

                    int px = x + lx;
                    int pz = z + lz;

                    if (px >= 0 && px < CHUNK_SIZE_X && pz >= 0 && pz < CHUNK_SIZE_Z) {
                        BlockType current = chunk.getBlock(px, ly, pz);
                        if (current == BlockType::AIR) {
                            chunk.setBlock(px, ly, pz, BlockType::LEAVES);
                        }
                    }
                }
            }
        }

        // Top leaf
        if (baseY + trunkHeight + 1 < CHUNK_SIZE_Y) {
            chunk.setBlock(x, baseY + trunkHeight + 1, z, BlockType::LEAVES);
        }
    }

    // Generate a birch tree (tall, thin, white bark)
    void generateBirchTree(Chunk& chunk, int x, int baseY, int z, std::mt19937& rng) {
        std::uniform_int_distribution<int> heightDist(5, 7);
        int trunkHeight = heightDist(rng);

        if (baseY + trunkHeight + 3 >= CHUNK_SIZE_Y) return;

        // Trunk (birch uses regular log, could add birch log type later)
        for (int y = 0; y < trunkHeight; y++) {
            chunk.setBlock(x, baseY + y, z, BlockType::WOOD_LOG);
        }

        // Leaves - small, round canopy
        int leafStart = baseY + trunkHeight - 2;
        int leafEnd = baseY + trunkHeight + 1;

        for (int ly = leafStart; ly <= leafEnd; ly++) {
            int radius = (ly < baseY + trunkHeight) ? 2 : 1;
            if (ly == leafEnd) radius = 1;

            for (int lx = -radius; lx <= radius; lx++) {
                for (int lz = -radius; lz <= radius; lz++) {
                    if (std::abs(lx) == radius && std::abs(lz) == radius) continue;

                    int px = x + lx;
                    int pz = z + lz;

                    if (px >= 0 && px < CHUNK_SIZE_X && pz >= 0 && pz < CHUNK_SIZE_Z) {
                        BlockType current = chunk.getBlock(px, ly, pz);
                        if (current == BlockType::AIR) {
                            chunk.setBlock(px, ly, pz, BlockType::LEAVES);
                        }
                    }
                }
            }
        }
    }

    // Generate a dark oak tree (short, thick, wide canopy)
    void generateDarkOakTree(Chunk& chunk, int x, int baseY, int z, std::mt19937& rng) {
        std::uniform_int_distribution<int> heightDist(4, 6);
        int trunkHeight = heightDist(rng);

        if (baseY + trunkHeight + 3 >= CHUNK_SIZE_Y) return;

        // 2x2 thick trunk for dark oak
        for (int y = 0; y < trunkHeight; y++) {
            chunk.setBlock(x, baseY + y, z, BlockType::WOOD_LOG);
            if (x + 1 < CHUNK_SIZE_X)
                chunk.setBlock(x + 1, baseY + y, z, BlockType::WOOD_LOG);
            if (z + 1 < CHUNK_SIZE_Z)
                chunk.setBlock(x, baseY + y, z + 1, BlockType::WOOD_LOG);
            if (x + 1 < CHUNK_SIZE_X && z + 1 < CHUNK_SIZE_Z)
                chunk.setBlock(x + 1, baseY + y, z + 1, BlockType::WOOD_LOG);
        }

        // Wide canopy
        int leafStart = baseY + trunkHeight - 1;
        int leafEnd = baseY + trunkHeight + 2;

        for (int ly = leafStart; ly <= leafEnd; ly++) {
            int radius = 3;
            if (ly == leafEnd) radius = 2;

            for (int lx = -radius; lx <= radius + 1; lx++) {
                for (int lz = -radius; lz <= radius + 1; lz++) {
                    // Skip outer corners
                    if (std::abs(lx) >= radius && std::abs(lz) >= radius) continue;

                    int px = x + lx;
                    int pz = z + lz;

                    if (px >= 0 && px < CHUNK_SIZE_X && pz >= 0 && pz < CHUNK_SIZE_Z) {
                        BlockType current = chunk.getBlock(px, ly, pz);
                        if (current == BlockType::AIR) {
                            chunk.setBlock(px, ly, pz, BlockType::LEAVES);
                        }
                    }
                }
            }
        }
    }

    // Generate an acacia tree (diagonal trunk, flat canopy)
    void generateAcaciaTree(Chunk& chunk, int x, int baseY, int z, std::mt19937& rng) {
        std::uniform_int_distribution<int> heightDist(4, 6);
        std::uniform_int_distribution<int> dirDist(0, 3);
        int trunkHeight = heightDist(rng);
        int dir = dirDist(rng);

        if (baseY + trunkHeight + 3 >= CHUNK_SIZE_Y) return;

        // Direction offsets for diagonal trunk
        int dx = (dir == 0 || dir == 1) ? 1 : -1;
        int dz = (dir == 0 || dir == 2) ? 1 : -1;

        // Trunk with diagonal bend
        int cx = x, cz = z;
        for (int y = 0; y < trunkHeight; y++) {
            if (cx >= 0 && cx < CHUNK_SIZE_X && cz >= 0 && cz < CHUNK_SIZE_Z) {
                chunk.setBlock(cx, baseY + y, cz, BlockType::WOOD_LOG);
            }
            // Bend trunk after halfway
            if (y == trunkHeight / 2) {
                cx += dx;
                cz += dz;
            }
        }

        // Flat, wide canopy
        int leafY = baseY + trunkHeight;
        for (int lx = -3; lx <= 3; lx++) {
            for (int lz = -3; lz <= 3; lz++) {
                // Circular-ish shape
                if (lx * lx + lz * lz > 10) continue;

                int px = cx + lx;
                int pz = cz + lz;

                if (px >= 0 && px < CHUNK_SIZE_X && pz >= 0 && pz < CHUNK_SIZE_Z) {
                    BlockType current = chunk.getBlock(px, leafY, pz);
                    if (current == BlockType::AIR) {
                        chunk.setBlock(px, leafY, pz, BlockType::LEAVES);
                    }
                    // Thin second layer
                    if (lx * lx + lz * lz < 5 && leafY + 1 < CHUNK_SIZE_Y) {
                        current = chunk.getBlock(px, leafY + 1, pz);
                        if (current == BlockType::AIR) {
                            chunk.setBlock(px, leafY + 1, pz, BlockType::LEAVES);
                        }
                    }
                }
            }
        }
    }

    // Generate a swamp tree (drooping leaves, exposed roots)
    void generateSwampTree(Chunk& chunk, int x, int baseY, int z, std::mt19937& rng) {
        std::uniform_int_distribution<int> heightDist(4, 7);
        int trunkHeight = heightDist(rng);

        if (baseY + trunkHeight + 3 >= CHUNK_SIZE_Y) return;

        // Trunk
        for (int y = 0; y < trunkHeight; y++) {
            chunk.setBlock(x, baseY + y, z, BlockType::WOOD_LOG);
        }

        // Leaves - similar to regular tree but wider
        int leafStart = baseY + trunkHeight - 2;
        int leafEnd = baseY + trunkHeight + 2;

        for (int ly = leafStart; ly <= leafEnd; ly++) {
            int radius = (ly < baseY + trunkHeight) ? 3 : 2;
            if (ly == leafEnd) radius = 1;

            for (int lx = -radius; lx <= radius; lx++) {
                for (int lz = -radius; lz <= radius; lz++) {
                    if (std::abs(lx) == radius && std::abs(lz) == radius) continue;

                    int px = x + lx;
                    int pz = z + lz;

                    if (px >= 0 && px < CHUNK_SIZE_X && pz >= 0 && pz < CHUNK_SIZE_Z) {
                        BlockType current = chunk.getBlock(px, ly, pz);
                        if (current == BlockType::AIR) {
                            chunk.setBlock(px, ly, pz, BlockType::LEAVES);
                        }
                    }
                }
            }
        }

        // Top leaf
        if (baseY + trunkHeight + 2 < CHUNK_SIZE_Y) {
            chunk.setBlock(x, baseY + trunkHeight + 2, z, BlockType::LEAVES);
        }
    }

    // Generate a cactus
    void generateCactus(Chunk& chunk, int x, int baseY, int z, std::mt19937& rng) {
        std::uniform_int_distribution<int> heightDist(2, 4);
        int height = heightDist(rng);

        // Check if there's room
        if (baseY + height >= CHUNK_SIZE_Y) return;

        // Generate cactus column
        for (int y = 0; y < height; y++) {
            chunk.setBlock(x, baseY + y, z, BlockType::CACTUS);
        }
    }

    void generateTree(Chunk& chunk, int x, int baseY, int z, std::mt19937& rng) {
        std::uniform_int_distribution<int> heightDist(4, 6);
        int trunkHeight = heightDist(rng);

        // Check if there's room for the tree
        if (baseY + trunkHeight + 3 >= CHUNK_SIZE_Y) return;

        // Trunk
        for (int y = 0; y < trunkHeight; y++) {
            chunk.setBlock(x, baseY + y, z, BlockType::WOOD_LOG);
        }

        // Leaves (simple sphere-ish shape)
        int leafStart = baseY + trunkHeight - 2;
        int leafEnd = baseY + trunkHeight + 2;

        for (int ly = leafStart; ly <= leafEnd; ly++) {
            int radius = (ly < baseY + trunkHeight) ? 2 : 1;
            if (ly == leafEnd) radius = 0;

            for (int lx = -radius; lx <= radius; lx++) {
                for (int lz = -radius; lz <= radius; lz++) {
                    // Skip corners for rounder shape
                    if (std::abs(lx) == radius && std::abs(lz) == radius) continue;

                    int px = x + lx;
                    int pz = z + lz;

                    if (px >= 0 && px < CHUNK_SIZE_X && pz >= 0 && pz < CHUNK_SIZE_Z) {
                        BlockType current = chunk.getBlock(px, ly, pz);
                        if (current == BlockType::AIR) {
                            chunk.setBlock(px, ly, pz, BlockType::LEAVES);
                        }
                    }
                }
            }
        }

        // Top leaf
        if (baseY + trunkHeight + 2 < CHUNK_SIZE_Y) {
            chunk.setBlock(x, baseY + trunkHeight + 2, z, BlockType::LEAVES);
        }
    }
};
