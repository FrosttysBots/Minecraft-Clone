#pragma once

// World Save/Load System
// Handles saving and loading world data including chunks, player position, etc.

#include "Chunk.h"
#include "../ui/WorldSelectScreen.h"
#include <string>
#include <fstream>
#include <filesystem>
#include <ctime>
#include <iostream>

// Forward declaration (World.h includes this file)
class World;

class WorldSaveLoad {
public:
    // Current world info
    std::string currentWorldPath;
    std::string currentWorldName;
    bool hasLoadedWorld = false;

    // Save world metadata
    static bool saveWorldMeta(const std::string& worldPath, const std::string& worldName,
                              int seed, int generationType, int maxHeight) {
        std::string metaPath = worldPath + "/world.meta";

        std::ofstream metaFile(metaPath);
        if (!metaFile.is_open()) {
            std::cerr << "Failed to create world.meta at: " << metaPath << std::endl;
            return false;
        }

        metaFile << "name=" << worldName << "\n";
        metaFile << "seed=" << seed << "\n";
        metaFile << "generationType=" << generationType << "\n";
        metaFile << "maxHeight=" << maxHeight << "\n";
        metaFile << "lastPlayed=" << std::time(nullptr) << "\n";

        metaFile.close();
        return true;
    }

    // Update last played time
    static bool updateLastPlayed(const std::string& worldPath) {
        std::string metaPath = worldPath + "/world.meta";

        // Read existing metadata
        std::ifstream inFile(metaPath);
        if (!inFile.is_open()) return false;

        std::vector<std::pair<std::string, std::string>> entries;
        std::string line;
        while (std::getline(inFile, line)) {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                std::string value = line.substr(eq + 1);
                if (key != "lastPlayed") {
                    entries.push_back({key, value});
                }
            }
        }
        inFile.close();

        // Write back with updated lastPlayed
        std::ofstream outFile(metaPath);
        if (!outFile.is_open()) return false;

        for (const auto& [key, value] : entries) {
            outFile << key << "=" << value << "\n";
        }
        outFile << "lastPlayed=" << std::time(nullptr) << "\n";
        outFile.close();

        return true;
    }

    // Create a new world folder
    static std::string createWorldFolder(const std::string& worldName) {
        // Create saves directory if it doesn't exist
        std::string savesPath = "saves";
        if (!std::filesystem::exists(savesPath)) {
            std::filesystem::create_directories(savesPath);
        }

        // Sanitize world name for folder
        std::string folderName = worldName;
        for (char& c : folderName) {
            if (!isalnum(c) && c != '-' && c != '_' && c != ' ') {
                c = '_';
            }
        }

        // Make unique folder name if needed
        std::string basePath = savesPath + "/" + folderName;
        std::string worldPath = basePath;
        int counter = 1;
        while (std::filesystem::exists(worldPath)) {
            worldPath = basePath + "_" + std::to_string(counter++);
        }

        // Create the folder
        std::filesystem::create_directories(worldPath);
        std::filesystem::create_directories(worldPath + "/region");

        return worldPath;
    }

    // Save a single chunk to disk (simple format)
    static bool saveChunk(const std::string& worldPath, const Chunk& chunk) {
        glm::ivec2 pos = chunk.position;

        // Region file path (each region is 32x32 chunks, like Minecraft)
        int regionX = pos.x >> 5;  // Divide by 32
        int regionZ = pos.y >> 5;
        std::string regionPath = worldPath + "/region/r." + std::to_string(regionX) +
                                "." + std::to_string(regionZ) + ".dat";

        // For simplicity, we'll save each chunk as a separate file for now
        // A more efficient approach would be region files
        std::string chunkPath = worldPath + "/region/c." + std::to_string(pos.x) +
                               "." + std::to_string(pos.y) + ".chunk";

        std::ofstream file(chunkPath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to save chunk at: " << chunkPath << std::endl;
            return false;
        }

        // Write chunk header
        int32_t magic = 0x43484E4B;  // "CHNK"
        int32_t version = 1;
        file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        file.write(reinterpret_cast<const char*>(&version), sizeof(version));
        file.write(reinterpret_cast<const char*>(&pos.x), sizeof(pos.x));
        file.write(reinterpret_cast<const char*>(&pos.y), sizeof(pos.y));

        // Write block data (simple RLE compression)
        // For MVP, just write raw block data
        for (int x = 0; x < CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                for (int y = 0; y < CHUNK_SIZE_Y; y++) {
                    uint8_t block = static_cast<uint8_t>(chunk.getBlock(x, y, z));
                    file.write(reinterpret_cast<const char*>(&block), sizeof(block));
                }
            }
        }

        file.close();
        return true;
    }

    // Load a single chunk from disk
    static bool loadChunk(const std::string& worldPath, Chunk& chunk, glm::ivec2 pos) {
        std::string chunkPath = worldPath + "/region/c." + std::to_string(pos.x) +
                               "." + std::to_string(pos.y) + ".chunk";

        std::ifstream file(chunkPath, std::ios::binary);
        if (!file.is_open()) {
            return false;  // Chunk doesn't exist, needs generation
        }

        // Read chunk header
        int32_t magic, version;
        int32_t fileX, fileZ;
        file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        file.read(reinterpret_cast<char*>(&fileX), sizeof(fileX));
        file.read(reinterpret_cast<char*>(&fileZ), sizeof(fileZ));

        if (magic != 0x43484E4B) {
            std::cerr << "Invalid chunk file magic: " << chunkPath << std::endl;
            return false;
        }

        if (fileX != pos.x || fileZ != pos.y) {
            std::cerr << "Chunk position mismatch in file: " << chunkPath << std::endl;
            return false;
        }

        chunk.position = pos;

        // Read block data
        for (int x = 0; x < CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                for (int y = 0; y < CHUNK_SIZE_Y; y++) {
                    uint8_t block;
                    file.read(reinterpret_cast<char*>(&block), sizeof(block));
                    chunk.setBlock(x, y, z, static_cast<BlockType>(block));
                }
            }
        }

        file.close();
        chunk.isDirty = true;  // Need to rebuild mesh
        return true;
    }

    // Check if a chunk file exists
    static bool chunkExists(const std::string& worldPath, glm::ivec2 pos) {
        std::string chunkPath = worldPath + "/region/c." + std::to_string(pos.x) +
                               "." + std::to_string(pos.y) + ".chunk";
        return std::filesystem::exists(chunkPath);
    }

    // Save all modified chunks in the world
    // Defined in World.h after World class is complete
    static int saveAllChunks(const std::string& worldPath, World& world);

    // Save player position
    static bool savePlayer(const std::string& worldPath, const glm::vec3& position,
                          float yaw, float pitch, bool isFlying) {
        std::string playerPath = worldPath + "/player.dat";

        std::ofstream file(playerPath, std::ios::binary);
        if (!file.is_open()) return false;

        file.write(reinterpret_cast<const char*>(&position.x), sizeof(float));
        file.write(reinterpret_cast<const char*>(&position.y), sizeof(float));
        file.write(reinterpret_cast<const char*>(&position.z), sizeof(float));
        file.write(reinterpret_cast<const char*>(&yaw), sizeof(float));
        file.write(reinterpret_cast<const char*>(&pitch), sizeof(float));
        uint8_t flyingFlag = isFlying ? 1 : 0;
        file.write(reinterpret_cast<const char*>(&flyingFlag), sizeof(uint8_t));

        file.close();
        return true;
    }

    // Load player position
    static bool loadPlayer(const std::string& worldPath, glm::vec3& position,
                          float& yaw, float& pitch, bool& isFlying) {
        std::string playerPath = worldPath + "/player.dat";

        std::ifstream file(playerPath, std::ios::binary);
        if (!file.is_open()) return false;

        file.read(reinterpret_cast<char*>(&position.x), sizeof(float));
        file.read(reinterpret_cast<char*>(&position.y), sizeof(float));
        file.read(reinterpret_cast<char*>(&position.z), sizeof(float));
        file.read(reinterpret_cast<char*>(&yaw), sizeof(float));
        file.read(reinterpret_cast<char*>(&pitch), sizeof(float));
        uint8_t flyingFlag;
        file.read(reinterpret_cast<char*>(&flyingFlag), sizeof(uint8_t));
        isFlying = flyingFlag != 0;

        file.close();
        return true;
    }

    // Get list of chunk positions that exist in save
    static std::vector<glm::ivec2> getSavedChunkPositions(const std::string& worldPath) {
        std::vector<glm::ivec2> positions;

        std::string regionPath = worldPath + "/region";
        if (!std::filesystem::exists(regionPath)) {
            return positions;
        }

        for (const auto& entry : std::filesystem::directory_iterator(regionPath)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (filename.substr(0, 2) == "c." && filename.size() > 6) {
                    // Parse c.X.Z.chunk
                    size_t firstDot = 1;  // After 'c'
                    size_t secondDot = filename.find('.', firstDot + 1);
                    size_t thirdDot = filename.find('.', secondDot + 1);

                    if (secondDot != std::string::npos && thirdDot != std::string::npos) {
                        try {
                            int x = std::stoi(filename.substr(firstDot + 1, secondDot - firstDot - 1));
                            int z = std::stoi(filename.substr(secondDot + 1, thirdDot - secondDot - 1));
                            positions.push_back(glm::ivec2(x, z));
                        } catch (...) {
                            // Ignore malformed filenames
                        }
                    }
                }
            }
        }

        return positions;
    }
};
