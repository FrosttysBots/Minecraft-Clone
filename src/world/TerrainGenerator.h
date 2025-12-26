#pragma once

#include "Chunk.h"
#include "Block.h"
#include <FastNoiseLite.h>
#include <random>
#include <cmath>

// Biome types
enum class Biome {
    PLAINS,
    FOREST,
    DESERT,
    SNOW,
    MOUNTAINS
};

class TerrainGenerator {
public:
    // World seed
    int seed;

    // Noise generators
    FastNoiseLite continentNoise;    // Large scale terrain shape
    FastNoiseLite mountainNoise;     // Mountain peaks
    FastNoiseLite detailNoise;       // Small terrain details
    FastNoiseLite caveNoise;         // 3D cave carving
    FastNoiseLite caveNoise2;        // Secondary cave noise for variety
    FastNoiseLite oreNoise;          // Ore distribution
    FastNoiseLite temperatureNoise;  // Biome temperature
    FastNoiseLite humidityNoise;     // Biome humidity
    FastNoiseLite aquiferNoise;      // Aquifer zones (where water spawns in caves)

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

        // First pass: Generate base terrain with height map
        for (int x = 0; x < CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                // World coordinates
                int worldX = chunkPos.x * CHUNK_SIZE_X + x;
                int worldZ = chunkPos.y * CHUNK_SIZE_Z + z;

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
        // Continent/base terrain noise - very large scale
        continentNoise.SetSeed(seed);
        continentNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        continentNoise.SetFrequency(0.002f);
        continentNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        continentNoise.SetFractalOctaves(4);

        // Mountain noise - medium scale, adds peaks
        mountainNoise.SetSeed(seed + 1);
        mountainNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        mountainNoise.SetFrequency(0.008f);
        mountainNoise.SetFractalType(FastNoiseLite::FractalType_Ridged);
        mountainNoise.SetFractalOctaves(3);

        // Detail noise - small bumps and variation
        detailNoise.SetSeed(seed + 2);
        detailNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        detailNoise.SetFrequency(0.03f);
        detailNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        detailNoise.SetFractalOctaves(2);

        // Cave noise - 3D for cave systems
        caveNoise.SetSeed(seed + 3);
        caveNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        caveNoise.SetFrequency(0.04f);
        caveNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        caveNoise.SetFractalOctaves(2);

        // Secondary cave noise for worm-like tunnels
        caveNoise2.SetSeed(seed + 4);
        caveNoise2.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        caveNoise2.SetFrequency(0.05f);

        // Ore noise
        oreNoise.SetSeed(seed + 5);
        oreNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        oreNoise.SetFrequency(0.1f);

        // Temperature noise for biomes - very large scale
        temperatureNoise.SetSeed(seed + 6);
        temperatureNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        temperatureNoise.SetFrequency(0.001f);
        temperatureNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        temperatureNoise.SetFractalOctaves(2);

        // Humidity noise for biomes - large scale, different from temperature
        humidityNoise.SetSeed(seed + 7);
        humidityNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        humidityNoise.SetFrequency(0.0015f);
        humidityNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        humidityNoise.SetFractalOctaves(2);

        // Aquifer noise - determines where water spawns in caves (Minecraft-style)
        // Only specific zones get water, not the entire underground
        aquiferNoise.SetSeed(seed + 8);
        aquiferNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        aquiferNoise.SetFrequency(0.02f);  // Medium scale aquifer zones
    }

    // Determine biome at world position based on temperature and humidity
    Biome getBiome(int worldX, int worldZ) {
        float x = static_cast<float>(worldX);
        float z = static_cast<float>(worldZ);

        // Get temperature (-1 to 1, higher = hotter)
        float temperature = temperatureNoise.GetNoise(x, z);

        // Get humidity (-1 to 1, higher = wetter)
        float humidity = humidityNoise.GetNoise(x, z);

        // Also check elevation for mountains
        float mountain = mountainNoise.GetNoise(x, z);

        // Determine biome based on temperature and humidity
        if (mountain > 0.5f) {
            return Biome::MOUNTAINS;  // High elevation = mountains
        }
        if (temperature < -0.3f) {
            return Biome::SNOW;       // Cold = snow biome
        }
        if (temperature > 0.4f && humidity < 0.0f) {
            return Biome::DESERT;     // Hot and dry = desert
        }
        if (humidity > 0.2f) {
            return Biome::FOREST;     // Wet = forest
        }
        return Biome::PLAINS;         // Default = plains
    }

    // Calculate terrain height at world position
    int getTerrainHeight(int worldX, int worldZ) {
        float x = static_cast<float>(worldX);
        float z = static_cast<float>(worldZ);

        // Base continent shape (-1 to 1)
        float continent = continentNoise.GetNoise(x, z);

        // Mountain factor (0 to 1, using ridged noise)
        float mountain = (mountainNoise.GetNoise(x, z) + 1.0f) * 0.5f;
        mountain = mountain * mountain; // Square for sharper peaks

        // Small detail variation
        float detail = detailNoise.GetNoise(x, z);

        // Combine noise layers
        // Base height + continent variation + mountains + detail
        float heightValue = static_cast<float>(baseHeight);
        heightValue += continent * 20.0f;           // +/- 20 blocks from continent
        heightValue += mountain * 30.0f;            // Up to 30 extra for mountains
        heightValue += detail * 4.0f;               // +/- 4 blocks detail

        // Clamp to valid range
        int height = static_cast<int>(heightValue);
        height = std::max(1, std::min(height, maxHeight));

        return height;
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
            if (terrainHeight < seaLevel - 1) {
                // Underwater = sand/gravel
                return BlockType::SAND;
            } else if (terrainHeight < seaLevel + 2) {
                // Beach
                return BlockType::SAND;
            } else {
                // Surface block based on biome
                switch (biome) {
                    case Biome::DESERT:
                        return BlockType::SAND;
                    case Biome::SNOW:
                        return BlockType::SNOW_BLOCK;
                    case Biome::MOUNTAINS:
                        if (terrainHeight > 90) {
                            return BlockType::SNOW_BLOCK;  // Snow-capped peaks
                        }
                        return BlockType::STONE;  // Rocky mountains
                    case Biome::FOREST:
                    case Biome::PLAINS:
                    default:
                        return BlockType::GRASS;
                }
            }
        }

        // Just below surface (1-3 blocks)
        if (y > terrainHeight - 4) {
            if (terrainHeight < seaLevel + 2) {
                return BlockType::SAND;
            }
            switch (biome) {
                case Biome::DESERT:
                    return BlockType::SAND;
                case Biome::SNOW:
                    return BlockType::DIRT;  // Frozen dirt under snow
                case Biome::MOUNTAINS:
                    return BlockType::STONE;
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
                                     (surfaceBlock == BlockType::SAND && biome == Biome::DESERT) ||
                                     (surfaceBlock == BlockType::SNOW_BLOCK && biome == Biome::SNOW);

                    if (isSurface) {
                        int chance = chanceDist(rng);

                        switch (biome) {
                            case Biome::FOREST:
                                // Dense trees in forest (8% chance)
                                if (chance < 8) {
                                    generateTree(chunk, x, y + 1, z, rng);
                                }
                                break;

                            case Biome::PLAINS:
                                // Sparse trees in plains (1% chance)
                                if (chance < 1) {
                                    generateTree(chunk, x, y + 1, z, rng);
                                }
                                break;

                            case Biome::DESERT:
                                // Cacti in desert (2% chance)
                                if (chance < 2) {
                                    generateCactus(chunk, x, y + 1, z, rng);
                                }
                                break;

                            case Biome::SNOW:
                                // Sparse snowy trees (1% chance)
                                if (chance < 1) {
                                    generateTree(chunk, x, y + 1, z, rng);
                                }
                                break;

                            case Biome::MOUNTAINS:
                                // Very sparse trees on lower mountains (0.5% chance)
                                if (chance < 1 && y < 85) {
                                    generateTree(chunk, x, y + 1, z, rng);
                                }
                                break;
                        }
                        break;
                    }
                }
            }
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
