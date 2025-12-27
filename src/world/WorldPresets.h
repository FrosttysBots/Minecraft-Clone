#pragma once

// World Generation Presets System
// Defines generation types, world settings, and preset management

#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdint>
#include <algorithm>
#include <random>

// ============================================
// GENERATION TYPES
// ============================================
enum class GenerationType {
    DEFAULT = 0,        // Standard terrain
    AMPLIFIED,          // 2x height variation
    SUPERFLAT,          // Flat at Y=64
    MOUNTAINS,          // Always mountain biome
    ISLANDS,            // Ocean with scattered islands
    CAVES,              // Massive caves, thin surface
    CUSTOM_EQUATION,    // User-defined equation
    COUNT
};

inline const char* getGenerationTypeName(GenerationType type) {
    switch (type) {
        case GenerationType::DEFAULT:         return "Default";
        case GenerationType::AMPLIFIED:       return "Amplified";
        case GenerationType::SUPERFLAT:       return "Superflat";
        case GenerationType::MOUNTAINS:       return "Mountains";
        case GenerationType::ISLANDS:         return "Islands";
        case GenerationType::CAVES:           return "Caves";
        case GenerationType::CUSTOM_EQUATION: return "Custom Equation";
        default:                              return "Unknown";
    }
}

inline std::vector<std::string> getGenerationTypeNames() {
    return {
        "Default",
        "Amplified",
        "Superflat",
        "Mountains",
        "Islands",
        "Caves",
        "Custom Equation"
    };
}

// ============================================
// WORLD SETTINGS
// ============================================
struct WorldSettings {
    std::string worldName = "New World";
    std::string seed = "";  // Empty = random
    int64_t seedValue = 0;  // Computed from seed string

    GenerationType generationType = GenerationType::DEFAULT;

    // Height parameters
    int maxYHeight = 256;       // 64-512
    int baseHeight = 64;
    int seaLevel = 62;

    // Biome parameters
    int minBiomeSize = 4;       // chunks
    int maxBiomeSize = 8;       // chunks

    // Scale parameters
    float continentScale = 20.0f;
    float mountainScale = 30.0f;
    float detailScale = 4.0f;

    // Custom equation
    std::string customEquation = "baseHeight + continent*20 + mountain*30 + detail*4";

    // Pre-generation settings
    int pregenerationRadius = 0;  // 0-64 chunks, 0 = disabled

    void computeSeed() {
        if (seed.empty()) {
            std::random_device rd;
            seedValue = rd();
        } else {
            // Try to parse as number first
            try {
                seedValue = std::stoll(seed);
            } catch (...) {
                // Hash the string
                std::hash<std::string> hasher;
                seedValue = static_cast<int64_t>(hasher(seed));
            }
        }
    }
};

// ============================================
// GENERATION PRESET
// ============================================
struct GenerationPreset {
    std::string name;
    std::string description;
    GenerationType type = GenerationType::DEFAULT;

    // Height parameters
    int baseHeight = 64;
    int seaLevel = 62;
    int maxHeight = 256;

    // Scale multipliers
    float continentScale = 20.0f;
    float mountainScale = 30.0f;
    float detailScale = 4.0f;

    // Biome size
    int minBiomeChunks = 4;
    int maxBiomeChunks = 8;

    // Custom equation
    std::string customEquation = "";

    // Custom variables for equation
    std::unordered_map<std::string, double> customVariables;

    // Apply this preset to world settings
    void applyToSettings(WorldSettings& settings) const {
        settings.generationType = type;
        settings.baseHeight = baseHeight;
        settings.seaLevel = seaLevel;
        settings.maxYHeight = maxHeight;
        settings.continentScale = continentScale;
        settings.mountainScale = mountainScale;
        settings.detailScale = detailScale;
        settings.minBiomeSize = minBiomeChunks;
        settings.maxBiomeSize = maxBiomeChunks;
        if (!customEquation.empty()) {
            settings.customEquation = customEquation;
        }
    }
};

// ============================================
// PRESET MANAGER (Simple JSON without external lib)
// ============================================
class PresetManager {
public:
    static std::string presetsDirectory;

    static void init(const std::string& assetsPath) {
        presetsDirectory = assetsPath + "/presets";
        // Create directory if it doesn't exist
        std::filesystem::create_directories(presetsDirectory);
    }

    static std::vector<std::string> listPresets() {
        std::vector<std::string> presets;

        if (!std::filesystem::exists(presetsDirectory)) {
            return presets;
        }

        for (const auto& entry : std::filesystem::directory_iterator(presetsDirectory)) {
            if (entry.path().extension() == ".json") {
                presets.push_back(entry.path().stem().string());
            }
        }

        std::sort(presets.begin(), presets.end());
        return presets;
    }

    // Simple JSON parser (minimal, for presets only)
    static GenerationPreset loadFromFile(const std::string& name) {
        GenerationPreset preset;
        preset.name = name;

        std::string path = presetsDirectory + "/" + name + ".json";
        std::ifstream file(path);
        if (!file.is_open()) {
            return getDefaultPreset(name);
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        file.close();

        // Parse JSON manually (simple key-value extraction)
        preset.description = extractString(content, "description");
        preset.baseHeight = extractInt(content, "baseHeight", 64);
        preset.seaLevel = extractInt(content, "seaLevel", 62);
        preset.maxHeight = extractInt(content, "maxHeight", 256);
        preset.continentScale = extractFloat(content, "continentScale", 20.0f);
        preset.mountainScale = extractFloat(content, "mountainScale", 30.0f);
        preset.detailScale = extractFloat(content, "detailScale", 4.0f);
        preset.minBiomeChunks = extractInt(content, "minBiomeChunks", 4);
        preset.maxBiomeChunks = extractInt(content, "maxBiomeChunks", 8);
        preset.customEquation = extractString(content, "customEquation");

        std::string typeStr = extractString(content, "type");
        preset.type = parseGenerationType(typeStr);

        return preset;
    }

    static bool saveToFile(const GenerationPreset& preset) {
        std::string path = presetsDirectory + "/" + preset.name + ".json";
        std::ofstream file(path);
        if (!file.is_open()) {
            return false;
        }

        file << "{\n";
        file << "    \"name\": \"" << escapeJson(preset.name) << "\",\n";
        file << "    \"description\": \"" << escapeJson(preset.description) << "\",\n";
        file << "    \"type\": \"" << getGenerationTypeName(preset.type) << "\",\n";
        file << "    \"baseHeight\": " << preset.baseHeight << ",\n";
        file << "    \"seaLevel\": " << preset.seaLevel << ",\n";
        file << "    \"maxHeight\": " << preset.maxHeight << ",\n";
        file << "    \"continentScale\": " << preset.continentScale << ",\n";
        file << "    \"mountainScale\": " << preset.mountainScale << ",\n";
        file << "    \"detailScale\": " << preset.detailScale << ",\n";
        file << "    \"minBiomeChunks\": " << preset.minBiomeChunks << ",\n";
        file << "    \"maxBiomeChunks\": " << preset.maxBiomeChunks << ",\n";
        file << "    \"customEquation\": \"" << escapeJson(preset.customEquation) << "\"\n";
        file << "}\n";

        file.close();
        return true;
    }

    // Built-in presets
    static GenerationPreset getDefaultPreset(const std::string& name) {
        GenerationPreset preset;
        preset.name = name;

        if (name == "default" || name == "Default") {
            preset.description = "Standard terrain generation";
            preset.type = GenerationType::DEFAULT;
            preset.baseHeight = 64;
            preset.seaLevel = 62;
            preset.maxHeight = 256;
            preset.continentScale = 20.0f;
            preset.mountainScale = 30.0f;
            preset.detailScale = 4.0f;
        }
        else if (name == "amplified" || name == "Amplified") {
            preset.description = "Extreme height variation";
            preset.type = GenerationType::AMPLIFIED;
            preset.baseHeight = 64;
            preset.seaLevel = 62;
            preset.maxHeight = 384;
            preset.continentScale = 40.0f;
            preset.mountainScale = 80.0f;
            preset.detailScale = 8.0f;
        }
        else if (name == "superflat" || name == "Superflat") {
            preset.description = "Completely flat world";
            preset.type = GenerationType::SUPERFLAT;
            preset.baseHeight = 64;
            preset.seaLevel = 0;
            preset.maxHeight = 64;
            preset.continentScale = 0.0f;
            preset.mountainScale = 0.0f;
            preset.detailScale = 0.0f;
            preset.customEquation = "64";
        }
        else if (name == "mountains" || name == "Mountains") {
            preset.description = "Towering mountain peaks";
            preset.type = GenerationType::MOUNTAINS;
            preset.baseHeight = 80;
            preset.seaLevel = 62;
            preset.maxHeight = 320;
            preset.continentScale = 30.0f;
            preset.mountainScale = 100.0f;
            preset.detailScale = 6.0f;
            preset.customEquation = "80 + abs(ridge(x*0.005, z*0.005)) * 100";
        }
        else if (name == "islands" || name == "Islands") {
            preset.description = "Ocean with scattered islands";
            preset.type = GenerationType::ISLANDS;
            preset.baseHeight = 40;
            preset.seaLevel = 62;
            preset.maxHeight = 128;
            preset.continentScale = 35.0f;
            preset.mountainScale = 20.0f;
            preset.detailScale = 3.0f;
            preset.customEquation = "max(seaLevel - 10, baseHeight + continent*35)";
        }
        else if (name == "caves" || name == "Caves") {
            preset.description = "Massive underground cave systems";
            preset.type = GenerationType::CAVES;
            preset.baseHeight = 80;
            preset.seaLevel = 62;
            preset.maxHeight = 128;
            preset.continentScale = 15.0f;
            preset.mountainScale = 10.0f;
            preset.detailScale = 5.0f;
        }
        else {
            // Default fallback
            preset.description = "Custom preset";
            preset.type = GenerationType::DEFAULT;
        }

        return preset;
    }

    static std::vector<GenerationPreset> getBuiltInPresets() {
        return {
            getDefaultPreset("default"),
            getDefaultPreset("amplified"),
            getDefaultPreset("superflat"),
            getDefaultPreset("mountains"),
            getDefaultPreset("islands"),
            getDefaultPreset("caves")
        };
    }

    static void createDefaultPresetFiles() {
        auto presets = getBuiltInPresets();
        for (const auto& preset : presets) {
            std::string path = presetsDirectory + "/" + preset.name + ".json";
            if (!std::filesystem::exists(path)) {
                saveToFile(preset);
            }
        }
    }

private:
    static std::string extractString(const std::string& json, const std::string& key) {
        std::string searchKey = "\"" + key + "\":";
        size_t pos = json.find(searchKey);
        if (pos == std::string::npos) return "";

        pos = json.find("\"", pos + searchKey.length());
        if (pos == std::string::npos) return "";

        size_t endPos = json.find("\"", pos + 1);
        if (endPos == std::string::npos) return "";

        return json.substr(pos + 1, endPos - pos - 1);
    }

    static int extractInt(const std::string& json, const std::string& key, int defaultVal) {
        std::string searchKey = "\"" + key + "\":";
        size_t pos = json.find(searchKey);
        if (pos == std::string::npos) return defaultVal;

        pos += searchKey.length();
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

        std::string numStr;
        while (pos < json.length() && (isdigit(json[pos]) || json[pos] == '-')) {
            numStr += json[pos++];
        }

        try {
            return std::stoi(numStr);
        } catch (...) {
            return defaultVal;
        }
    }

    static float extractFloat(const std::string& json, const std::string& key, float defaultVal) {
        std::string searchKey = "\"" + key + "\":";
        size_t pos = json.find(searchKey);
        if (pos == std::string::npos) return defaultVal;

        pos += searchKey.length();
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

        std::string numStr;
        while (pos < json.length() && (isdigit(json[pos]) || json[pos] == '.' || json[pos] == '-')) {
            numStr += json[pos++];
        }

        try {
            return std::stof(numStr);
        } catch (...) {
            return defaultVal;
        }
    }

    static GenerationType parseGenerationType(const std::string& str) {
        if (str == "Default" || str == "DEFAULT") return GenerationType::DEFAULT;
        if (str == "Amplified" || str == "AMPLIFIED") return GenerationType::AMPLIFIED;
        if (str == "Superflat" || str == "SUPERFLAT") return GenerationType::SUPERFLAT;
        if (str == "Mountains" || str == "MOUNTAINS") return GenerationType::MOUNTAINS;
        if (str == "Islands" || str == "ISLANDS") return GenerationType::ISLANDS;
        if (str == "Caves" || str == "CAVES") return GenerationType::CAVES;
        if (str == "Custom Equation" || str == "CUSTOM_EQUATION") return GenerationType::CUSTOM_EQUATION;
        return GenerationType::DEFAULT;
    }

    static std::string escapeJson(const std::string& str) {
        std::string result;
        for (char c : str) {
            switch (c) {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:   result += c; break;
            }
        }
        return result;
    }
};

// Initialize static member
inline std::string PresetManager::presetsDirectory = "assets/presets";
