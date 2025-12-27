#pragma once

#include "rhi/RHI.h"
#include "../core/Config.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <string>
#include <memory>

namespace Render {

// Handles backend selection and window creation for OpenGL/Vulkan
class BackendSelector {
public:
    // Check if a backend is available on this system
    static bool isBackendAvailable(RendererType renderer);

    // Get list of available backends
    static std::vector<RendererType> getAvailableBackends();

    // Get backend name as string
    static std::string getBackendName(RendererType renderer);

    // Configure GLFW for the selected backend (call before glfwCreateWindow)
    // Returns false if backend is not available
    static bool configureGLFW(RendererType renderer);

    // Create window with appropriate settings for backend
    // Returns nullptr on failure
    static GLFWwindow* createWindow(RendererType renderer, int width, int height,
                                    const char* title, bool fullscreen = false);

    // Initialize the graphics API after window creation
    // For OpenGL: loads GLAD
    // For Vulkan: volk is loaded by VKDevice
    // Returns false on failure
    static bool initializeGraphicsAPI(RendererType renderer, GLFWwindow* window);

    // Create RHI device for the selected backend
    static std::unique_ptr<RHI::RHIDevice> createDevice(RendererType renderer, GLFWwindow* window);

    // Convert between RendererType (from Config) and RHI::Backend
    static RHI::Backend toRHIBackend(RendererType renderer);
    static RendererType fromRHIBackend(RHI::Backend backend);

    // Print available backends and capabilities
    static void printBackendInfo();
};

} // namespace Render
