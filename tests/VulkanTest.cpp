// Minimal Vulkan test to verify SDK/runtime setup works
// Uses volk for dynamic loading - no SDK installation required

#define VOLK_IMPLEMENTATION
#include <volk.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <cstring>

// Validation layer for debugging (optional)
const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
constexpr bool enableValidation = false;
#else
constexpr bool enableValidation = true;
#endif

bool checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers) {
        bool found = false;
        for (const auto& layer : availableLayers) {
            if (strcmp(layerName, layer.layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

int main() {
    std::cout << "=== Vulkan Test ===" << std::endl;

    // Step 1: Initialize volk (dynamic Vulkan loading)
    std::cout << "[1/6] Initializing volk loader... ";
    VkResult result = volkInitialize();
    if (result != VK_SUCCESS) {
        std::cerr << "FAILED (code " << result << ")" << std::endl;
        std::cerr << "Make sure Vulkan drivers are installed." << std::endl;
        return 1;
    }
    std::cout << "OK" << std::endl;

    // Step 2: Initialize GLFW
    std::cout << "[2/6] Initializing GLFW... ";
    if (!glfwInit()) {
        std::cerr << "FAILED" << std::endl;
        return 1;
    }

    if (!glfwVulkanSupported()) {
        std::cerr << "FAILED - Vulkan not supported by GLFW" << std::endl;
        glfwTerminate();
        return 1;
    }
    std::cout << "OK" << std::endl;

    // Step 3: Create Vulkan instance
    std::cout << "[3/6] Creating Vulkan instance... ";

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Vulkan Test";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "VoxelEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;  // Target Vulkan 1.3

    // Get required GLFW extensions
    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> extensions(glfwExts, glfwExts + glfwExtCount);

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    // Enable validation layers if available
    bool validationEnabled = false;
    if (enableValidation && checkValidationLayerSupport()) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
        validationEnabled = true;
    } else {
        createInfo.enabledLayerCount = 0;
    }

    VkInstance instance;
    result = vkCreateInstance(&createInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        std::cerr << "FAILED (code " << result << ")" << std::endl;
        glfwTerminate();
        return 1;
    }
    std::cout << "OK" << std::endl;

    // Load instance-level functions
    volkLoadInstance(instance);

    if (validationEnabled) {
        std::cout << "       Validation layers: ENABLED" << std::endl;
    }

    // Step 4: Enumerate physical devices
    std::cout << "[4/6] Enumerating GPUs... " << std::endl;

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        std::cerr << "       No Vulkan-capable GPUs found!" << std::endl;
        vkDestroyInstance(instance, nullptr);
        glfwTerminate();
        return 1;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    std::cout << "       Found " << deviceCount << " GPU(s):" << std::endl;

    VkPhysicalDevice selectedDevice = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < deviceCount; i++) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);

        const char* typeStr = "Unknown";
        switch (props.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   typeStr = "Discrete GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: typeStr = "Integrated GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    typeStr = "Virtual GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:            typeStr = "CPU"; break;
            default: break;
        }

        uint32_t apiMajor = VK_VERSION_MAJOR(props.apiVersion);
        uint32_t apiMinor = VK_VERSION_MINOR(props.apiVersion);
        uint32_t apiPatch = VK_VERSION_PATCH(props.apiVersion);

        std::cout << "       [" << i << "] " << props.deviceName
                  << " (" << typeStr << ") - Vulkan "
                  << apiMajor << "." << apiMinor << "." << apiPatch << std::endl;

        // Prefer discrete GPU
        if (selectedDevice == VK_NULL_HANDLE ||
            props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            selectedDevice = devices[i];
        }
    }

    // Step 5: Create test window with Vulkan surface
    std::cout << "[5/6] Creating window + Vulkan surface... ";

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // No OpenGL!
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "Vulkan Test", nullptr, nullptr);
    if (!window) {
        std::cerr << "FAILED to create window" << std::endl;
        vkDestroyInstance(instance, nullptr);
        glfwTerminate();
        return 1;
    }

    VkSurfaceKHR surface;
    result = glfwCreateWindowSurface(instance, window, nullptr, &surface);
    if (result != VK_SUCCESS) {
        std::cerr << "FAILED to create surface (code " << result << ")" << std::endl;
        glfwDestroyWindow(window);
        vkDestroyInstance(instance, nullptr);
        glfwTerminate();
        return 1;
    }
    std::cout << "OK" << std::endl;

    // Step 6: Check queue families
    std::cout << "[6/6] Checking queue families... ";

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(selectedDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(selectedDevice, &queueFamilyCount, queueFamilies.data());

    int graphicsFamily = -1;
    int presentFamily = -1;

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(selectedDevice, i, surface, &presentSupport);
        if (presentSupport) {
            presentFamily = i;
        }

        if (graphicsFamily >= 0 && presentFamily >= 0) break;
    }

    if (graphicsFamily < 0 || presentFamily < 0) {
        std::cerr << "FAILED - missing queue families" << std::endl;
    } else {
        std::cout << "OK (graphics=" << graphicsFamily << ", present=" << presentFamily << ")" << std::endl;
    }

    // Display window briefly
    std::cout << std::endl;
    std::cout << "=== SUCCESS ===" << std::endl;
    std::cout << "Vulkan is fully functional!" << std::endl;
    std::cout << "Window will close in 2 seconds..." << std::endl;

    // Show window for 2 seconds
    double startTime = glfwGetTime();
    while (!glfwWindowShouldClose(window) && (glfwGetTime() - startTime) < 2.0) {
        glfwPollEvents();
    }

    // Cleanup
    vkDestroySurfaceKHR(instance, surface, nullptr);
    glfwDestroyWindow(window);
    vkDestroyInstance(instance, nullptr);
    glfwTerminate();

    std::cout << "Cleanup complete." << std::endl;
    return 0;
}
