#pragma once

// World Save/Load System
// Handles saving and loading world data including chunks, player position, etc.

#include "Chunk.h"
#include "../ui/WorldSelectScreen.h"
#include "../core/Inventory.h"
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

    // Player data version for backward compatibility
    static constexpr int32_t PLAYER_DATA_VERSION = 2;  // Version 2 adds survival stats

    // Save player position and survival stats
    static bool savePlayer(const std::string& worldPath, const glm::vec3& position,
                          float yaw, float pitch, bool isFlying,
                          int health = 20, int hunger = 20, int air = 300,
                          float saturation = 5.0f, const glm::vec3& spawnPoint = glm::vec3(0, 80, 0)) {
        std::string playerPath = worldPath + "/player.dat";

        std::ofstream file(playerPath, std::ios::binary);
        if (!file.is_open()) return false;

        // Version header
        int32_t version = PLAYER_DATA_VERSION;
        file.write(reinterpret_cast<const char*>(&version), sizeof(version));

        // Position and rotation
        file.write(reinterpret_cast<const char*>(&position.x), sizeof(float));
        file.write(reinterpret_cast<const char*>(&position.y), sizeof(float));
        file.write(reinterpret_cast<const char*>(&position.z), sizeof(float));
        file.write(reinterpret_cast<const char*>(&yaw), sizeof(float));
        file.write(reinterpret_cast<const char*>(&pitch), sizeof(float));
        uint8_t flyingFlag = isFlying ? 1 : 0;
        file.write(reinterpret_cast<const char*>(&flyingFlag), sizeof(uint8_t));

        // Survival stats (Version 2+)
        file.write(reinterpret_cast<const char*>(&health), sizeof(int));
        file.write(reinterpret_cast<const char*>(&hunger), sizeof(int));
        file.write(reinterpret_cast<const char*>(&air), sizeof(int));
        file.write(reinterpret_cast<const char*>(&saturation), sizeof(float));
        file.write(reinterpret_cast<const char*>(&spawnPoint.x), sizeof(float));
        file.write(reinterpret_cast<const char*>(&spawnPoint.y), sizeof(float));
        file.write(reinterpret_cast<const char*>(&spawnPoint.z), sizeof(float));

        file.close();
        return true;
    }

    // Load player position and survival stats
    static bool loadPlayer(const std::string& worldPath, glm::vec3& position,
                          float& yaw, float& pitch, bool& isFlying,
                          int& health, int& hunger, int& air,
                          float& saturation, glm::vec3& spawnPoint) {
        std::string playerPath = worldPath + "/player.dat";

        std::ifstream file(playerPath, std::ios::binary);
        if (!file.is_open()) return false;

        // Check file size to detect version
        file.seekg(0, std::ios::end);
        std::streamsize fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        int32_t version = 1;  // Default to version 1 for old saves

        // Version 2+ has version header; version 1 starts with position (float)
        // Detect by file size: v1 = 25 bytes, v2 = 4 + 25 + 28 = 57 bytes
        if (fileSize > 30) {
            // Likely version 2+, read version header
            file.read(reinterpret_cast<char*>(&version), sizeof(version));
        }

        // Position and rotation
        file.read(reinterpret_cast<char*>(&position.x), sizeof(float));
        file.read(reinterpret_cast<char*>(&position.y), sizeof(float));
        file.read(reinterpret_cast<char*>(&position.z), sizeof(float));
        file.read(reinterpret_cast<char*>(&yaw), sizeof(float));
        file.read(reinterpret_cast<char*>(&pitch), sizeof(float));
        uint8_t flyingFlag;
        file.read(reinterpret_cast<char*>(&flyingFlag), sizeof(uint8_t));
        isFlying = flyingFlag != 0;

        // Survival stats (Version 2+)
        if (version >= 2 && file.good()) {
            file.read(reinterpret_cast<char*>(&health), sizeof(int));
            file.read(reinterpret_cast<char*>(&hunger), sizeof(int));
            file.read(reinterpret_cast<char*>(&air), sizeof(int));
            file.read(reinterpret_cast<char*>(&saturation), sizeof(float));
            file.read(reinterpret_cast<char*>(&spawnPoint.x), sizeof(float));
            file.read(reinterpret_cast<char*>(&spawnPoint.y), sizeof(float));
            file.read(reinterpret_cast<char*>(&spawnPoint.z), sizeof(float));
        } else {
            // Default survival stats for old saves
            health = 20;
            hunger = 20;
            air = 300;
            saturation = 5.0f;
            spawnPoint = position;  // Use current position as spawn
        }

        file.close();
        return true;
    }

    // Legacy load function for backward compatibility
    static bool loadPlayer(const std::string& worldPath, glm::vec3& position,
                          float& yaw, float& pitch, bool& isFlying) {
        int health, hunger, air;
        float saturation;
        glm::vec3 spawnPoint;
        return loadPlayer(worldPath, position, yaw, pitch, isFlying,
                         health, hunger, air, saturation, spawnPoint);
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

    // ===== INVENTORY SAVE/LOAD =====
    // Version 1: Old format (BlockType only)
    // Version 2: New format (StackType + Block/Item + Durability)
    static constexpr int32_t INVENTORY_DATA_VERSION = 2;

    // Save inventory to file
    static bool saveInventory(const std::string& worldPath, const std::array<ItemStack, TOTAL_SLOTS>& slots, int selectedSlot) {
        std::string invPath = worldPath + "/inventory.dat";

        std::ofstream file(invPath, std::ios::binary);
        if (!file.is_open()) return false;

        // Version header
        int32_t version = INVENTORY_DATA_VERSION;
        file.write(reinterpret_cast<const char*>(&version), sizeof(version));

        // Slot count
        int32_t slotCount = TOTAL_SLOTS;
        file.write(reinterpret_cast<const char*>(&slotCount), sizeof(slotCount));

        // Each slot: stackType (uint8) + typeId (uint16) + count (int32) + durability (int32)
        for (const auto& slot : slots) {
            uint8_t stackType = static_cast<uint8_t>(slot.stackType);
            uint16_t typeId = 0;
            if (slot.isBlock()) {
                typeId = static_cast<uint16_t>(slot.blockType);
            } else if (slot.isItem()) {
                typeId = static_cast<uint16_t>(slot.itemType);
            }
            int32_t count = slot.count;
            int32_t durability = slot.durability;

            file.write(reinterpret_cast<const char*>(&stackType), sizeof(stackType));
            file.write(reinterpret_cast<const char*>(&typeId), sizeof(typeId));
            file.write(reinterpret_cast<const char*>(&count), sizeof(count));
            file.write(reinterpret_cast<const char*>(&durability), sizeof(durability));
        }

        // Selected slot
        int32_t selected = selectedSlot;
        file.write(reinterpret_cast<const char*>(&selected), sizeof(selected));

        file.close();
        return true;
    }

    // Load inventory from file
    static bool loadInventory(const std::string& worldPath, std::array<ItemStack, TOTAL_SLOTS>& slots, int& selectedSlot) {
        std::string invPath = worldPath + "/inventory.dat";

        std::ifstream file(invPath, std::ios::binary);
        if (!file.is_open()) return false;

        // Version header
        int32_t version;
        file.read(reinterpret_cast<char*>(&version), sizeof(version));

        // Handle version 1 (old format - blocks only)
        if (version == 1) {
            int32_t slotCount;
            file.read(reinterpret_cast<char*>(&slotCount), sizeof(slotCount));

            int slotsToRead = std::min(slotCount, static_cast<int32_t>(TOTAL_SLOTS));
            for (int i = 0; i < slotsToRead; i++) {
                uint8_t type;
                int32_t count;
                file.read(reinterpret_cast<char*>(&type), sizeof(type));
                file.read(reinterpret_cast<char*>(&count), sizeof(count));
                // Convert old format to new: treat as block
                if (count > 0 && type != 0) {
                    slots[i] = ItemStack(static_cast<BlockType>(type), count);
                } else {
                    slots[i].clear();
                }
            }

            // Skip any extra slots in file
            for (int i = slotsToRead; i < slotCount; i++) {
                uint8_t type;
                int32_t count;
                file.read(reinterpret_cast<char*>(&type), sizeof(type));
                file.read(reinterpret_cast<char*>(&count), sizeof(count));
            }

            int32_t selected;
            file.read(reinterpret_cast<char*>(&selected), sizeof(selected));
            selectedSlot = std::clamp(static_cast<int>(selected), 0, HOTBAR_SLOTS - 1);

            file.close();
            return true;
        }

        // Handle version 2 (new format - blocks and items with durability)
        if (version != INVENTORY_DATA_VERSION) {
            file.close();
            return false;
        }

        // Slot count
        int32_t slotCount;
        file.read(reinterpret_cast<char*>(&slotCount), sizeof(slotCount));

        // Read slots (up to what we have space for)
        int slotsToRead = std::min(slotCount, static_cast<int32_t>(TOTAL_SLOTS));
        for (int i = 0; i < slotsToRead; i++) {
            uint8_t stackType;
            uint16_t typeId;
            int32_t count;
            int32_t durability;

            file.read(reinterpret_cast<char*>(&stackType), sizeof(stackType));
            file.read(reinterpret_cast<char*>(&typeId), sizeof(typeId));
            file.read(reinterpret_cast<char*>(&count), sizeof(count));
            file.read(reinterpret_cast<char*>(&durability), sizeof(durability));

            if (count <= 0) {
                slots[i].clear();
            } else if (stackType == static_cast<uint8_t>(StackType::BLOCK)) {
                slots[i] = ItemStack(static_cast<BlockType>(typeId), count);
            } else if (stackType == static_cast<uint8_t>(StackType::ITEM)) {
                slots[i] = ItemStack(static_cast<ItemType>(typeId), count, durability);
            } else {
                slots[i].clear();
            }
        }

        // Skip any extra slots in file
        for (int i = slotsToRead; i < slotCount; i++) {
            uint8_t stackType;
            uint16_t typeId;
            int32_t count;
            int32_t durability;
            file.read(reinterpret_cast<char*>(&stackType), sizeof(stackType));
            file.read(reinterpret_cast<char*>(&typeId), sizeof(typeId));
            file.read(reinterpret_cast<char*>(&count), sizeof(count));
            file.read(reinterpret_cast<char*>(&durability), sizeof(durability));
        }

        // Selected slot
        int32_t selected;
        file.read(reinterpret_cast<char*>(&selected), sizeof(selected));
        selectedSlot = std::clamp(static_cast<int>(selected), 0, HOTBAR_SLOTS - 1);

        file.close();
        return true;
    }
};
