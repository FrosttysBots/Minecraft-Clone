#pragma once

#include <GLFW/glfw3.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <fstream>
#include <sstream>
#include <iostream>

// Action identifiers for keybinds
enum class KeyAction {
    // Movement
    MoveForward,
    MoveBackward,
    MoveLeft,
    MoveRight,
    Jump,
    Sneak,
    Sprint,

    // Gameplay
    Attack,         // Left mouse - break block
    UseItem,        // Right mouse - place block
    PickBlock,      // Middle mouse
    DropItem,
    OpenInventory,

    // Hotbar
    Hotbar1,
    Hotbar2,
    Hotbar3,
    Hotbar4,
    Hotbar5,
    Hotbar6,
    Hotbar7,
    Hotbar8,
    Hotbar9,

    // Interface
    OpenChat,
    OpenCommand,
    TakeScreenshot,
    ToggleDebug,
    ToggleFullscreen,
    Pause,

    // Debug combinations (F3+key)
    DebugReloadChunks,      // F3+A
    DebugChunkBorders,      // F3+G
    DebugHitboxes,          // F3+B
    DebugLightLevels,       // F3+L
    DebugAdvancedInfo,      // F3+H
    DebugIncreaseRenderDist,// F3+F
    DebugDecreaseRenderDist,// F3+Shift+F
    DebugReloadTextures,    // F3+T
    DebugShowHelp,          // F3+Q

    COUNT
};

// Keybind with primary and secondary key
struct Keybind {
    int primary = GLFW_KEY_UNKNOWN;
    int secondary = GLFW_KEY_UNKNOWN;
    std::string displayName;
    std::string category;
    bool isMouseButton = false;  // True if this uses mouse buttons instead of keys

    Keybind() = default;
    Keybind(int p, int s, const std::string& name, const std::string& cat, bool mouse = false)
        : primary(p), secondary(s), displayName(name), category(cat), isMouseButton(mouse) {}
};

class KeybindManager {
public:
    static KeybindManager& getInstance() {
        static KeybindManager instance;
        return instance;
    }

    void init() {
        setupDefaultKeybinds();
    }

    // Check if an action is currently pressed
    bool isPressed(GLFWwindow* window, KeyAction action) const {
        auto it = keybinds.find(action);
        if (it == keybinds.end()) return false;

        const Keybind& kb = it->second;

        if (kb.isMouseButton) {
            if (kb.primary != GLFW_KEY_UNKNOWN &&
                glfwGetMouseButton(window, kb.primary) == GLFW_PRESS) return true;
            if (kb.secondary != GLFW_KEY_UNKNOWN &&
                glfwGetMouseButton(window, kb.secondary) == GLFW_PRESS) return true;
        } else {
            if (kb.primary != GLFW_KEY_UNKNOWN &&
                glfwGetKey(window, kb.primary) == GLFW_PRESS) return true;
            if (kb.secondary != GLFW_KEY_UNKNOWN &&
                glfwGetKey(window, kb.secondary) == GLFW_PRESS) return true;
        }
        return false;
    }

    // Get keybind for an action
    Keybind& getKeybind(KeyAction action) {
        return keybinds[action];
    }

    const Keybind& getKeybind(KeyAction action) const {
        static Keybind empty;
        auto it = keybinds.find(action);
        return it != keybinds.end() ? it->second : empty;
    }

    // Set primary key for an action
    void setPrimaryKey(KeyAction action, int key) {
        keybinds[action].primary = key;
    }

    // Set secondary key for an action
    void setSecondaryKey(KeyAction action, int key) {
        keybinds[action].secondary = key;
    }

    // Reset action to default
    void resetToDefault(KeyAction action) {
        auto it = defaultKeybinds.find(action);
        if (it != defaultKeybinds.end()) {
            keybinds[action] = it->second;
        }
    }

    // Reset all to defaults
    void resetAllToDefaults() {
        keybinds = defaultKeybinds;
    }

    // Get all keybinds for UI display
    const std::unordered_map<KeyAction, Keybind>& getAllKeybinds() const {
        return keybinds;
    }

    // Get keybinds by category
    std::vector<std::pair<KeyAction, Keybind>> getKeybindsByCategory(const std::string& category) const {
        std::vector<std::pair<KeyAction, Keybind>> result;
        for (const auto& [action, kb] : keybinds) {
            if (kb.category == category) {
                result.push_back({action, kb});
            }
        }
        return result;
    }

    // Get all categories
    std::vector<std::string> getCategories() const {
        std::vector<std::string> categories;
        for (const auto& [action, kb] : keybinds) {
            bool found = false;
            for (const auto& cat : categories) {
                if (cat == kb.category) { found = true; break; }
            }
            if (!found) categories.push_back(kb.category);
        }
        return categories;
    }

    // Convert key code to display string
    static std::string keyToString(int key, bool isMouseButton = false) {
        if (key == GLFW_KEY_UNKNOWN) return "None";

        if (isMouseButton) {
            switch (key) {
                case GLFW_MOUSE_BUTTON_LEFT: return "Left Click";
                case GLFW_MOUSE_BUTTON_RIGHT: return "Right Click";
                case GLFW_MOUSE_BUTTON_MIDDLE: return "Middle Click";
                case GLFW_MOUSE_BUTTON_4: return "Mouse 4";
                case GLFW_MOUSE_BUTTON_5: return "Mouse 5";
                default: return "Mouse " + std::to_string(key);
            }
        }

        // Special keys
        switch (key) {
            case GLFW_KEY_SPACE: return "Space";
            case GLFW_KEY_APOSTROPHE: return "'";
            case GLFW_KEY_COMMA: return ",";
            case GLFW_KEY_MINUS: return "-";
            case GLFW_KEY_PERIOD: return ".";
            case GLFW_KEY_SLASH: return "/";
            case GLFW_KEY_SEMICOLON: return ";";
            case GLFW_KEY_EQUAL: return "=";
            case GLFW_KEY_LEFT_BRACKET: return "[";
            case GLFW_KEY_BACKSLASH: return "\\";
            case GLFW_KEY_RIGHT_BRACKET: return "]";
            case GLFW_KEY_GRAVE_ACCENT: return "`";
            case GLFW_KEY_ESCAPE: return "Escape";
            case GLFW_KEY_ENTER: return "Enter";
            case GLFW_KEY_TAB: return "Tab";
            case GLFW_KEY_BACKSPACE: return "Backspace";
            case GLFW_KEY_INSERT: return "Insert";
            case GLFW_KEY_DELETE: return "Delete";
            case GLFW_KEY_RIGHT: return "Right Arrow";
            case GLFW_KEY_LEFT: return "Left Arrow";
            case GLFW_KEY_DOWN: return "Down Arrow";
            case GLFW_KEY_UP: return "Up Arrow";
            case GLFW_KEY_PAGE_UP: return "Page Up";
            case GLFW_KEY_PAGE_DOWN: return "Page Down";
            case GLFW_KEY_HOME: return "Home";
            case GLFW_KEY_END: return "End";
            case GLFW_KEY_CAPS_LOCK: return "Caps Lock";
            case GLFW_KEY_SCROLL_LOCK: return "Scroll Lock";
            case GLFW_KEY_NUM_LOCK: return "Num Lock";
            case GLFW_KEY_PRINT_SCREEN: return "Print Screen";
            case GLFW_KEY_PAUSE: return "Pause";
            case GLFW_KEY_LEFT_SHIFT: return "Left Shift";
            case GLFW_KEY_LEFT_CONTROL: return "Left Ctrl";
            case GLFW_KEY_LEFT_ALT: return "Left Alt";
            case GLFW_KEY_LEFT_SUPER: return "Left Super";
            case GLFW_KEY_RIGHT_SHIFT: return "Right Shift";
            case GLFW_KEY_RIGHT_CONTROL: return "Right Ctrl";
            case GLFW_KEY_RIGHT_ALT: return "Right Alt";
            case GLFW_KEY_RIGHT_SUPER: return "Right Super";
            case GLFW_KEY_MENU: return "Menu";
            // Function keys
            case GLFW_KEY_F1: return "F1";
            case GLFW_KEY_F2: return "F2";
            case GLFW_KEY_F3: return "F3";
            case GLFW_KEY_F4: return "F4";
            case GLFW_KEY_F5: return "F5";
            case GLFW_KEY_F6: return "F6";
            case GLFW_KEY_F7: return "F7";
            case GLFW_KEY_F8: return "F8";
            case GLFW_KEY_F9: return "F9";
            case GLFW_KEY_F10: return "F10";
            case GLFW_KEY_F11: return "F11";
            case GLFW_KEY_F12: return "F12";
            // Keypad
            case GLFW_KEY_KP_0: return "Numpad 0";
            case GLFW_KEY_KP_1: return "Numpad 1";
            case GLFW_KEY_KP_2: return "Numpad 2";
            case GLFW_KEY_KP_3: return "Numpad 3";
            case GLFW_KEY_KP_4: return "Numpad 4";
            case GLFW_KEY_KP_5: return "Numpad 5";
            case GLFW_KEY_KP_6: return "Numpad 6";
            case GLFW_KEY_KP_7: return "Numpad 7";
            case GLFW_KEY_KP_8: return "Numpad 8";
            case GLFW_KEY_KP_9: return "Numpad 9";
            case GLFW_KEY_KP_DECIMAL: return "Numpad .";
            case GLFW_KEY_KP_DIVIDE: return "Numpad /";
            case GLFW_KEY_KP_MULTIPLY: return "Numpad *";
            case GLFW_KEY_KP_SUBTRACT: return "Numpad -";
            case GLFW_KEY_KP_ADD: return "Numpad +";
            case GLFW_KEY_KP_ENTER: return "Numpad Enter";
            case GLFW_KEY_KP_EQUAL: return "Numpad =";
        }

        // Letter and number keys (A-Z, 0-9)
        if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
            return std::string(1, 'A' + (key - GLFW_KEY_A));
        }
        if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
            return std::string(1, '0' + (key - GLFW_KEY_0));
        }

        return "Key " + std::to_string(key);
    }

    // Convert string to key code (for loading from config)
    static int stringToKey(const std::string& str, bool& isMouseButton) {
        isMouseButton = false;

        if (str == "None" || str.empty()) return GLFW_KEY_UNKNOWN;

        // Mouse buttons
        if (str == "Left Click") { isMouseButton = true; return GLFW_MOUSE_BUTTON_LEFT; }
        if (str == "Right Click") { isMouseButton = true; return GLFW_MOUSE_BUTTON_RIGHT; }
        if (str == "Middle Click") { isMouseButton = true; return GLFW_MOUSE_BUTTON_MIDDLE; }
        if (str == "Mouse 4") { isMouseButton = true; return GLFW_MOUSE_BUTTON_4; }
        if (str == "Mouse 5") { isMouseButton = true; return GLFW_MOUSE_BUTTON_5; }

        // Special keys
        if (str == "Space") return GLFW_KEY_SPACE;
        if (str == "Escape") return GLFW_KEY_ESCAPE;
        if (str == "Enter") return GLFW_KEY_ENTER;
        if (str == "Tab") return GLFW_KEY_TAB;
        if (str == "Backspace") return GLFW_KEY_BACKSPACE;
        if (str == "Insert") return GLFW_KEY_INSERT;
        if (str == "Delete") return GLFW_KEY_DELETE;
        if (str == "Right Arrow") return GLFW_KEY_RIGHT;
        if (str == "Left Arrow") return GLFW_KEY_LEFT;
        if (str == "Down Arrow") return GLFW_KEY_DOWN;
        if (str == "Up Arrow") return GLFW_KEY_UP;
        if (str == "Page Up") return GLFW_KEY_PAGE_UP;
        if (str == "Page Down") return GLFW_KEY_PAGE_DOWN;
        if (str == "Home") return GLFW_KEY_HOME;
        if (str == "End") return GLFW_KEY_END;
        if (str == "Caps Lock") return GLFW_KEY_CAPS_LOCK;
        if (str == "Left Shift") return GLFW_KEY_LEFT_SHIFT;
        if (str == "Left Ctrl") return GLFW_KEY_LEFT_CONTROL;
        if (str == "Left Alt") return GLFW_KEY_LEFT_ALT;
        if (str == "Right Shift") return GLFW_KEY_RIGHT_SHIFT;
        if (str == "Right Ctrl") return GLFW_KEY_RIGHT_CONTROL;
        if (str == "Right Alt") return GLFW_KEY_RIGHT_ALT;

        // Function keys
        if (str == "F1") return GLFW_KEY_F1;
        if (str == "F2") return GLFW_KEY_F2;
        if (str == "F3") return GLFW_KEY_F3;
        if (str == "F4") return GLFW_KEY_F4;
        if (str == "F5") return GLFW_KEY_F5;
        if (str == "F6") return GLFW_KEY_F6;
        if (str == "F7") return GLFW_KEY_F7;
        if (str == "F8") return GLFW_KEY_F8;
        if (str == "F9") return GLFW_KEY_F9;
        if (str == "F10") return GLFW_KEY_F10;
        if (str == "F11") return GLFW_KEY_F11;
        if (str == "F12") return GLFW_KEY_F12;

        // Numpad
        if (str == "Numpad 0") return GLFW_KEY_KP_0;
        if (str == "Numpad 1") return GLFW_KEY_KP_1;
        if (str == "Numpad 2") return GLFW_KEY_KP_2;
        if (str == "Numpad 3") return GLFW_KEY_KP_3;
        if (str == "Numpad 4") return GLFW_KEY_KP_4;
        if (str == "Numpad 5") return GLFW_KEY_KP_5;
        if (str == "Numpad 6") return GLFW_KEY_KP_6;
        if (str == "Numpad 7") return GLFW_KEY_KP_7;
        if (str == "Numpad 8") return GLFW_KEY_KP_8;
        if (str == "Numpad 9") return GLFW_KEY_KP_9;
        if (str == "Numpad Enter") return GLFW_KEY_KP_ENTER;

        // Single character (letter or number)
        if (str.length() == 1) {
            char c = str[0];
            if (c >= 'A' && c <= 'Z') return GLFW_KEY_A + (c - 'A');
            if (c >= 'a' && c <= 'z') return GLFW_KEY_A + (c - 'a');
            if (c >= '0' && c <= '9') return GLFW_KEY_0 + (c - '0');
        }

        return GLFW_KEY_UNKNOWN;
    }

    // Save keybinds to config file section
    void saveToConfig(std::ostream& out) const {
        out << "[Keybinds]\n";
        for (const auto& [action, kb] : keybinds) {
            std::string actionName = actionToString(action);
            out << actionName << "_primary=" << keyToString(kb.primary, kb.isMouseButton) << "\n";
            out << actionName << "_secondary=" << keyToString(kb.secondary, kb.isMouseButton) << "\n";
        }
    }

    // Load a single keybind from config
    void loadKeybind(const std::string& key, const std::string& value) {
        // Parse key name to find action and whether it's primary/secondary
        size_t suffixPos = key.rfind("_primary");
        bool isPrimary = true;
        std::string actionName;

        if (suffixPos != std::string::npos) {
            actionName = key.substr(0, suffixPos);
            isPrimary = true;
        } else {
            suffixPos = key.rfind("_secondary");
            if (suffixPos != std::string::npos) {
                actionName = key.substr(0, suffixPos);
                isPrimary = false;
            } else {
                return; // Invalid key format
            }
        }

        KeyAction action = stringToAction(actionName);
        if (action == KeyAction::COUNT) return; // Unknown action

        bool isMouseBtn = false;
        int keyCode = stringToKey(value, isMouseBtn);

        if (isPrimary) {
            keybinds[action].primary = keyCode;
        } else {
            keybinds[action].secondary = keyCode;
        }
    }

    // Check for keybind conflicts
    std::vector<std::pair<KeyAction, KeyAction>> findConflicts() const {
        std::vector<std::pair<KeyAction, KeyAction>> conflicts;

        std::vector<std::pair<KeyAction, int>> allKeys;
        for (const auto& [action, kb] : keybinds) {
            if (kb.primary != GLFW_KEY_UNKNOWN) {
                allKeys.push_back({action, kb.primary});
            }
            if (kb.secondary != GLFW_KEY_UNKNOWN) {
                allKeys.push_back({action, kb.secondary});
            }
        }

        for (size_t i = 0; i < allKeys.size(); i++) {
            for (size_t j = i + 1; j < allKeys.size(); j++) {
                if (allKeys[i].second == allKeys[j].second) {
                    conflicts.push_back({allKeys[i].first, allKeys[j].first});
                }
            }
        }

        return conflicts;
    }

private:
    KeybindManager() = default;

    std::unordered_map<KeyAction, Keybind> keybinds;
    std::unordered_map<KeyAction, Keybind> defaultKeybinds;

    void setupDefaultKeybinds() {
        // Movement
        defaultKeybinds[KeyAction::MoveForward] = {GLFW_KEY_W, GLFW_KEY_UP, "Move Forward", "Movement"};
        defaultKeybinds[KeyAction::MoveBackward] = {GLFW_KEY_S, GLFW_KEY_DOWN, "Move Backward", "Movement"};
        defaultKeybinds[KeyAction::MoveLeft] = {GLFW_KEY_A, GLFW_KEY_UNKNOWN, "Strafe Left", "Movement"};
        defaultKeybinds[KeyAction::MoveRight] = {GLFW_KEY_D, GLFW_KEY_UNKNOWN, "Strafe Right", "Movement"};
        defaultKeybinds[KeyAction::Jump] = {GLFW_KEY_SPACE, GLFW_KEY_UNKNOWN, "Jump", "Movement"};
        defaultKeybinds[KeyAction::Sneak] = {GLFW_KEY_LEFT_SHIFT, GLFW_KEY_UNKNOWN, "Sneak", "Movement"};
        defaultKeybinds[KeyAction::Sprint] = {GLFW_KEY_LEFT_CONTROL, GLFW_KEY_UNKNOWN, "Sprint", "Movement"};

        // Gameplay
        defaultKeybinds[KeyAction::Attack] = {GLFW_MOUSE_BUTTON_LEFT, GLFW_KEY_UNKNOWN, "Attack/Destroy", "Gameplay", true};
        defaultKeybinds[KeyAction::UseItem] = {GLFW_MOUSE_BUTTON_RIGHT, GLFW_KEY_UNKNOWN, "Use Item/Place Block", "Gameplay", true};
        defaultKeybinds[KeyAction::PickBlock] = {GLFW_MOUSE_BUTTON_MIDDLE, GLFW_KEY_UNKNOWN, "Pick Block", "Gameplay", true};
        defaultKeybinds[KeyAction::DropItem] = {GLFW_KEY_Q, GLFW_KEY_UNKNOWN, "Drop Item", "Gameplay"};
        defaultKeybinds[KeyAction::OpenInventory] = {GLFW_KEY_E, GLFW_KEY_UNKNOWN, "Open Inventory", "Gameplay"};

        // Hotbar
        defaultKeybinds[KeyAction::Hotbar1] = {GLFW_KEY_1, GLFW_KEY_UNKNOWN, "Hotbar Slot 1", "Inventory"};
        defaultKeybinds[KeyAction::Hotbar2] = {GLFW_KEY_2, GLFW_KEY_UNKNOWN, "Hotbar Slot 2", "Inventory"};
        defaultKeybinds[KeyAction::Hotbar3] = {GLFW_KEY_3, GLFW_KEY_UNKNOWN, "Hotbar Slot 3", "Inventory"};
        defaultKeybinds[KeyAction::Hotbar4] = {GLFW_KEY_4, GLFW_KEY_UNKNOWN, "Hotbar Slot 4", "Inventory"};
        defaultKeybinds[KeyAction::Hotbar5] = {GLFW_KEY_5, GLFW_KEY_UNKNOWN, "Hotbar Slot 5", "Inventory"};
        defaultKeybinds[KeyAction::Hotbar6] = {GLFW_KEY_6, GLFW_KEY_UNKNOWN, "Hotbar Slot 6", "Inventory"};
        defaultKeybinds[KeyAction::Hotbar7] = {GLFW_KEY_7, GLFW_KEY_UNKNOWN, "Hotbar Slot 7", "Inventory"};
        defaultKeybinds[KeyAction::Hotbar8] = {GLFW_KEY_8, GLFW_KEY_UNKNOWN, "Hotbar Slot 8", "Inventory"};
        defaultKeybinds[KeyAction::Hotbar9] = {GLFW_KEY_9, GLFW_KEY_UNKNOWN, "Hotbar Slot 9", "Inventory"};

        // Interface
        defaultKeybinds[KeyAction::OpenChat] = {GLFW_KEY_T, GLFW_KEY_UNKNOWN, "Open Chat", "Multiplayer"};
        defaultKeybinds[KeyAction::OpenCommand] = {GLFW_KEY_SLASH, GLFW_KEY_UNKNOWN, "Open Command", "Multiplayer"};
        defaultKeybinds[KeyAction::TakeScreenshot] = {GLFW_KEY_F2, GLFW_KEY_UNKNOWN, "Take Screenshot", "Miscellaneous"};
        defaultKeybinds[KeyAction::ToggleDebug] = {GLFW_KEY_F3, GLFW_KEY_UNKNOWN, "Toggle Debug", "Miscellaneous"};
        defaultKeybinds[KeyAction::ToggleFullscreen] = {GLFW_KEY_F11, GLFW_KEY_UNKNOWN, "Toggle Fullscreen", "Miscellaneous"};
        defaultKeybinds[KeyAction::Pause] = {GLFW_KEY_ESCAPE, GLFW_KEY_UNKNOWN, "Pause", "Miscellaneous"};

        // Debug combinations (these show the F3+ combo in UI but are handled specially)
        defaultKeybinds[KeyAction::DebugReloadChunks] = {GLFW_KEY_A, GLFW_KEY_UNKNOWN, "Reload Chunks (F3+)", "Debug"};
        defaultKeybinds[KeyAction::DebugChunkBorders] = {GLFW_KEY_G, GLFW_KEY_UNKNOWN, "Chunk Borders (F3+)", "Debug"};
        defaultKeybinds[KeyAction::DebugHitboxes] = {GLFW_KEY_B, GLFW_KEY_UNKNOWN, "Hitboxes (F3+)", "Debug"};
        defaultKeybinds[KeyAction::DebugLightLevels] = {GLFW_KEY_L, GLFW_KEY_UNKNOWN, "Light Levels (F3+)", "Debug"};
        defaultKeybinds[KeyAction::DebugAdvancedInfo] = {GLFW_KEY_H, GLFW_KEY_UNKNOWN, "Advanced Info (F3+)", "Debug"};
        defaultKeybinds[KeyAction::DebugIncreaseRenderDist] = {GLFW_KEY_F, GLFW_KEY_UNKNOWN, "Increase Render Dist (F3+)", "Debug"};
        defaultKeybinds[KeyAction::DebugDecreaseRenderDist] = {GLFW_KEY_F, GLFW_KEY_UNKNOWN, "Decrease Render Dist (F3+Shift+)", "Debug"};
        defaultKeybinds[KeyAction::DebugReloadTextures] = {GLFW_KEY_T, GLFW_KEY_UNKNOWN, "Reload Textures (F3+)", "Debug"};
        defaultKeybinds[KeyAction::DebugShowHelp] = {GLFW_KEY_Q, GLFW_KEY_UNKNOWN, "Show Debug Help (F3+)", "Debug"};

        // Copy defaults to current keybinds
        keybinds = defaultKeybinds;
    }

    // Convert action enum to string for saving
    static std::string actionToString(KeyAction action) {
        switch (action) {
            case KeyAction::MoveForward: return "MoveForward";
            case KeyAction::MoveBackward: return "MoveBackward";
            case KeyAction::MoveLeft: return "MoveLeft";
            case KeyAction::MoveRight: return "MoveRight";
            case KeyAction::Jump: return "Jump";
            case KeyAction::Sneak: return "Sneak";
            case KeyAction::Sprint: return "Sprint";
            case KeyAction::Attack: return "Attack";
            case KeyAction::UseItem: return "UseItem";
            case KeyAction::PickBlock: return "PickBlock";
            case KeyAction::DropItem: return "DropItem";
            case KeyAction::OpenInventory: return "OpenInventory";
            case KeyAction::Hotbar1: return "Hotbar1";
            case KeyAction::Hotbar2: return "Hotbar2";
            case KeyAction::Hotbar3: return "Hotbar3";
            case KeyAction::Hotbar4: return "Hotbar4";
            case KeyAction::Hotbar5: return "Hotbar5";
            case KeyAction::Hotbar6: return "Hotbar6";
            case KeyAction::Hotbar7: return "Hotbar7";
            case KeyAction::Hotbar8: return "Hotbar8";
            case KeyAction::Hotbar9: return "Hotbar9";
            case KeyAction::OpenChat: return "OpenChat";
            case KeyAction::OpenCommand: return "OpenCommand";
            case KeyAction::TakeScreenshot: return "TakeScreenshot";
            case KeyAction::ToggleDebug: return "ToggleDebug";
            case KeyAction::ToggleFullscreen: return "ToggleFullscreen";
            case KeyAction::Pause: return "Pause";
            case KeyAction::DebugReloadChunks: return "DebugReloadChunks";
            case KeyAction::DebugChunkBorders: return "DebugChunkBorders";
            case KeyAction::DebugHitboxes: return "DebugHitboxes";
            case KeyAction::DebugLightLevels: return "DebugLightLevels";
            case KeyAction::DebugAdvancedInfo: return "DebugAdvancedInfo";
            case KeyAction::DebugIncreaseRenderDist: return "DebugIncreaseRenderDist";
            case KeyAction::DebugDecreaseRenderDist: return "DebugDecreaseRenderDist";
            case KeyAction::DebugReloadTextures: return "DebugReloadTextures";
            case KeyAction::DebugShowHelp: return "DebugShowHelp";
            default: return "Unknown";
        }
    }

    // Convert string to action enum for loading
    static KeyAction stringToAction(const std::string& str) {
        if (str == "MoveForward") return KeyAction::MoveForward;
        if (str == "MoveBackward") return KeyAction::MoveBackward;
        if (str == "MoveLeft") return KeyAction::MoveLeft;
        if (str == "MoveRight") return KeyAction::MoveRight;
        if (str == "Jump") return KeyAction::Jump;
        if (str == "Sneak") return KeyAction::Sneak;
        if (str == "Sprint") return KeyAction::Sprint;
        if (str == "Attack") return KeyAction::Attack;
        if (str == "UseItem") return KeyAction::UseItem;
        if (str == "PickBlock") return KeyAction::PickBlock;
        if (str == "DropItem") return KeyAction::DropItem;
        if (str == "OpenInventory") return KeyAction::OpenInventory;
        if (str == "Hotbar1") return KeyAction::Hotbar1;
        if (str == "Hotbar2") return KeyAction::Hotbar2;
        if (str == "Hotbar3") return KeyAction::Hotbar3;
        if (str == "Hotbar4") return KeyAction::Hotbar4;
        if (str == "Hotbar5") return KeyAction::Hotbar5;
        if (str == "Hotbar6") return KeyAction::Hotbar6;
        if (str == "Hotbar7") return KeyAction::Hotbar7;
        if (str == "Hotbar8") return KeyAction::Hotbar8;
        if (str == "Hotbar9") return KeyAction::Hotbar9;
        if (str == "OpenChat") return KeyAction::OpenChat;
        if (str == "OpenCommand") return KeyAction::OpenCommand;
        if (str == "TakeScreenshot") return KeyAction::TakeScreenshot;
        if (str == "ToggleDebug") return KeyAction::ToggleDebug;
        if (str == "ToggleFullscreen") return KeyAction::ToggleFullscreen;
        if (str == "Pause") return KeyAction::Pause;
        if (str == "DebugReloadChunks") return KeyAction::DebugReloadChunks;
        if (str == "DebugChunkBorders") return KeyAction::DebugChunkBorders;
        if (str == "DebugHitboxes") return KeyAction::DebugHitboxes;
        if (str == "DebugLightLevels") return KeyAction::DebugLightLevels;
        if (str == "DebugAdvancedInfo") return KeyAction::DebugAdvancedInfo;
        if (str == "DebugIncreaseRenderDist") return KeyAction::DebugIncreaseRenderDist;
        if (str == "DebugDecreaseRenderDist") return KeyAction::DebugDecreaseRenderDist;
        if (str == "DebugReloadTextures") return KeyAction::DebugReloadTextures;
        if (str == "DebugShowHelp") return KeyAction::DebugShowHelp;
        return KeyAction::COUNT;
    }
};

// Convenience macro for checking keybinds
#define KEY_PRESSED(action) KeybindManager::getInstance().isPressed(window, action)
