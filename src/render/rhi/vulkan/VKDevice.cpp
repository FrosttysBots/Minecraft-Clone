#define VOLK_IMPLEMENTATION
#include "VKDevice.h"
#include "VKBuffer.h"
#include "VKTexture.h"
#include "VKShader.h"
#include "VKPipeline.h"
#include "VKFramebuffer.h"
#include "VKDescriptorSet.h"
#include "VKCommandBuffer.h"
#include <iostream>
#include <set>
#include <algorithm>

// VMA implementation
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

namespace RHI {

// ============================================================================
// DEBUG CALLBACK
// ============================================================================

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    (void)messageType;
    (void)pUserData;

    const char* severity = "";
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        severity = "ERROR";
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        severity = "WARNING";
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        severity = "INFO";
    } else {
        severity = "VERBOSE";
    }

    std::cerr << "[Vulkan " << severity << "] " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

// ============================================================================
// VK DEVICE IMPLEMENTATION
// ============================================================================

VKDevice::VKDevice(GLFWwindow* window) : m_window(window) {
    // Initialize volk
    if (volkInitialize() != VK_SUCCESS) {
        throw std::runtime_error("[VKDevice] Failed to initialize volk");
    }

    if (!createInstance()) {
        throw std::runtime_error("[VKDevice] Failed to create Vulkan instance");
    }

    // Create surface
    if (glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface) != VK_SUCCESS) {
        throw std::runtime_error("[VKDevice] Failed to create window surface");
    }

    if (!selectPhysicalDevice()) {
        throw std::runtime_error("[VKDevice] Failed to find suitable GPU");
    }

    if (!createLogicalDevice()) {
        throw std::runtime_error("[VKDevice] Failed to create logical device");
    }

    if (!createAllocator()) {
        throw std::runtime_error("[VKDevice] Failed to create memory allocator");
    }

    if (!createCommandPool()) {
        throw std::runtime_error("[VKDevice] Failed to create command pool");
    }

    queryDeviceInfo();

    std::cout << "[VKDevice] Vulkan device created: " << m_info.deviceName << std::endl;
}

VKDevice::~VKDevice() {
    waitIdle();

    m_immediateCommandBuffer.reset();
    m_graphicsQueue.reset();
    m_computeQueue.reset();
    m_transferQueue.reset();

    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    }

    if (m_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_allocator);
    }

    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
    }

    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }

    if (m_debugMessenger != VK_NULL_HANDLE) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            m_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func) {
            func(m_instance, m_debugMessenger, nullptr);
        }
    }

    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
    }
}

bool VKDevice::createInstance() {
    // Check validation layer support
    if (m_enableValidation) {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> layers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, layers.data());

        for (const char* layerName : m_validationLayers) {
            bool found = false;
            for (const auto& layer : layers) {
                if (strcmp(layerName, layer.layerName) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::cerr << "[VKDevice] Validation layer not found: " << layerName << std::endl;
            }
        }
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "ForgeBound";
    appInfo.applicationVersion = VK_MAKE_VERSION(2, 0, 0);
    appInfo.pEngineName = "ForgeBound";
    appInfo.engineVersion = VK_MAKE_VERSION(2, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    // Get required extensions
    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> extensions(glfwExts, glfwExts + glfwExtCount);

    if (m_enableValidation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (m_enableValidation) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();

        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = debugCallback;
        createInfo.pNext = &debugCreateInfo;
    }

    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
        return false;
    }

    // Load instance functions
    volkLoadInstance(m_instance);

    // Create debug messenger
    if (m_enableValidation) {
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            m_instance, "vkCreateDebugUtilsMessengerEXT");
        if (func) {
            func(m_instance, &debugCreateInfo, nullptr, &m_debugMessenger);
        }
    }

    return true;
}

bool VKDevice::selectPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        std::cerr << "[VKDevice] No Vulkan-capable GPUs found" << std::endl;
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    // Rate and select the best device
    int bestScore = 0;
    for (const auto& device : devices) {
        int score = rateDeviceSuitability(device);
        if (score > bestScore) {
            m_physicalDevice = device;
            bestScore = score;
        }
    }

    if (m_physicalDevice == VK_NULL_HANDLE) {
        std::cerr << "[VKDevice] No suitable GPU found" << std::endl;
        return false;
    }

    m_queueFamilies = findQueueFamilies(m_physicalDevice);
    return true;
}

int VKDevice::rateDeviceSuitability(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceProperties(device, &props);
    vkGetPhysicalDeviceFeatures(device, &features);

    int score = 0;

    // Discrete GPUs are preferred
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 10000;
    }

    // Maximum texture size is a good indicator of capability
    score += props.limits.maxImageDimension2D;

    // Check required features
    if (!features.samplerAnisotropy) {
        return 0;  // Required feature
    }

    // Check queue families
    QueueFamilyIndices indices = findQueueFamilies(device);
    if (!indices.isComplete()) {
        return 0;
    }

    // Check extension support
    if (!checkDeviceExtensionSupport(device)) {
        return 0;
    }

    // Check swapchain support
    SwapchainSupportDetails swapchainSupport = VKSwapchain::querySwapchainSupport(device, m_surface);
    if (swapchainSupport.formats.empty() || swapchainSupport.presentModes.empty()) {
        return 0;
    }

    return score;
}

QueueFamilyIndices VKDevice::findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        const auto& queueFamily = queueFamilies[i];

        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
            // Prefer dedicated compute queue
            if (!indices.computeFamily.has_value() ||
                !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                indices.computeFamily = i;
            }
        }

        if (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) {
            // Prefer dedicated transfer queue
            if (!indices.transferFamily.has_value() ||
                !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                indices.transferFamily = i;
            }
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }

        if (indices.isComplete()) break;
    }

    return indices;
}

bool VKDevice::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> available(extCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, available.data());

    std::set<std::string> required(m_deviceExtensions.begin(), m_deviceExtensions.end());
    for (const auto& ext : available) {
        required.erase(ext.extensionName);
    }
    return required.empty();
}

bool VKDevice::createLogicalDevice() {
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        m_queueFamilies.graphicsFamily.value(),
        m_queueFamilies.presentFamily.value()
    };

    if (m_queueFamilies.computeFamily.has_value()) {
        uniqueQueueFamilies.insert(m_queueFamilies.computeFamily.value());
    }
    if (m_queueFamilies.transferFamily.has_value()) {
        uniqueQueueFamilies.insert(m_queueFamilies.transferFamily.value());
    }

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // Device features
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.fillModeNonSolid = VK_TRUE;
    deviceFeatures.wideLines = VK_TRUE;
    deviceFeatures.multiDrawIndirect = VK_TRUE;
    deviceFeatures.drawIndirectFirstInstance = VK_TRUE;

    // Vulkan 1.2 features
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.descriptorIndexing = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.descriptorBindingPartiallyBound = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    features12.bufferDeviceAddress = VK_TRUE;

    // Vulkan 1.3 features
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.pNext = &features12;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features13;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(m_deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();

    if (m_enableValidation) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();
    }

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        return false;
    }

    // Load device functions
    volkLoadDevice(m_device);

    // Get queue handles
    VkQueue graphicsQueue;
    vkGetDeviceQueue(m_device, m_queueFamilies.graphicsFamily.value(), 0, &graphicsQueue);
    m_graphicsQueue = std::make_unique<VKQueue>(this, graphicsQueue,
        m_queueFamilies.graphicsFamily.value(), QueueType::Graphics);

    if (m_queueFamilies.computeFamily.has_value() &&
        m_queueFamilies.computeFamily.value() != m_queueFamilies.graphicsFamily.value()) {
        VkQueue computeQueue;
        vkGetDeviceQueue(m_device, m_queueFamilies.computeFamily.value(), 0, &computeQueue);
        m_computeQueue = std::make_unique<VKQueue>(this, computeQueue,
            m_queueFamilies.computeFamily.value(), QueueType::Compute);
    }

    if (m_queueFamilies.transferFamily.has_value() &&
        m_queueFamilies.transferFamily.value() != m_queueFamilies.graphicsFamily.value()) {
        VkQueue transferQueue;
        vkGetDeviceQueue(m_device, m_queueFamilies.transferFamily.value(), 0, &transferQueue);
        m_transferQueue = std::make_unique<VKQueue>(this, transferQueue,
            m_queueFamilies.transferFamily.value(), QueueType::Transfer);
    }

    return true;
}

bool VKDevice::createAllocator() {
    VmaVulkanFunctions vulkanFunctions{};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_device;
    allocatorInfo.instance = m_instance;
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;

    return vmaCreateAllocator(&allocatorInfo, &m_allocator) == VK_SUCCESS;
}

bool VKDevice::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_queueFamilies.graphicsFamily.value();

    return vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) == VK_SUCCESS;
}

void VKDevice::queryDeviceInfo() {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);

    m_info.backend = Backend::Vulkan;
    m_info.deviceName = props.deviceName;
    m_info.apiVersion = std::to_string(VK_VERSION_MAJOR(props.apiVersion)) + "." +
                        std::to_string(VK_VERSION_MINOR(props.apiVersion)) + "." +
                        std::to_string(VK_VERSION_PATCH(props.apiVersion));

    // Query limits
    m_info.limits.maxTexture2DSize = props.limits.maxImageDimension2D;
    m_info.limits.maxTexture3DSize = props.limits.maxImageDimension3D;
    m_info.limits.maxTextureCubeSize = props.limits.maxImageDimensionCube;
    m_info.limits.maxTextureArrayLayers = props.limits.maxImageArrayLayers;
    m_info.limits.maxColorAttachments = props.limits.maxColorAttachments;
    m_info.limits.maxComputeWorkGroupCount[0] = props.limits.maxComputeWorkGroupCount[0];
    m_info.limits.maxComputeWorkGroupCount[1] = props.limits.maxComputeWorkGroupCount[1];
    m_info.limits.maxComputeWorkGroupCount[2] = props.limits.maxComputeWorkGroupCount[2];
    m_info.limits.maxComputeWorkGroupSize[0] = props.limits.maxComputeWorkGroupSize[0];
    m_info.limits.maxComputeWorkGroupSize[1] = props.limits.maxComputeWorkGroupSize[1];
    m_info.limits.maxComputeWorkGroupSize[2] = props.limits.maxComputeWorkGroupSize[2];
    m_info.limits.maxComputeWorkGroupInvocations = props.limits.maxComputeWorkGroupInvocations;
    m_info.limits.maxAnisotropy = props.limits.maxSamplerAnisotropy;
    m_info.limits.minUniformBufferOffsetAlignment = props.limits.minUniformBufferOffsetAlignment;
    m_info.limits.minStorageBufferOffsetAlignment = props.limits.minStorageBufferOffsetAlignment;
    m_info.limits.maxPushConstantSize = props.limits.maxPushConstantsSize;

    m_info.limits.supportsComputeShaders = true;
    m_info.limits.supportsGeometryShaders = true;
    m_info.limits.supportsTessellation = true;
    m_info.limits.supportsMultiDrawIndirect = true;
    m_info.limits.supportsIndirectFirstInstance = true;
    m_info.limits.supportsPersistentMapping = true;
}

void VKDevice::waitIdle() {
    vkDeviceWaitIdle(m_device);
}

// ============================================================================
// RESOURCE CREATION
// ============================================================================

std::unique_ptr<RHIBuffer> VKDevice::createBuffer(const BufferDesc& desc) {
    return std::make_unique<VKBuffer>(this, desc);
}

std::unique_ptr<RHITexture> VKDevice::createTexture(const TextureDesc& desc) {
    return std::make_unique<VKTexture>(this, desc);
}

std::unique_ptr<RHISampler> VKDevice::createSampler(const SamplerDesc& desc) {
    return std::make_unique<VKSampler>(this, desc);
}

std::unique_ptr<RHIShaderModule> VKDevice::createShaderModule(const ShaderModuleDesc& desc) {
    return std::make_unique<VKShaderModule>(this, desc);
}

std::unique_ptr<RHIShaderProgram> VKDevice::createShaderProgram(const ShaderProgramDesc& desc) {
    return std::make_unique<VKShaderProgram>(this, desc);
}

std::unique_ptr<RHIDescriptorSetLayout> VKDevice::createDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) {
    return std::make_unique<VKDescriptorSetLayout>(this, desc);
}

std::unique_ptr<RHIPipelineLayout> VKDevice::createPipelineLayout(const PipelineLayoutDesc& desc) {
    return std::make_unique<VKPipelineLayout>(this, desc);
}

std::unique_ptr<RHIGraphicsPipeline> VKDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    return std::make_unique<VKGraphicsPipeline>(this, desc);
}

std::unique_ptr<RHIComputePipeline> VKDevice::createComputePipeline(const ComputePipelineDesc& desc) {
    return std::make_unique<VKComputePipeline>(this, desc);
}

std::unique_ptr<RHIRenderPass> VKDevice::createRenderPass(const RenderPassDesc& desc) {
    return std::make_unique<VKRenderPass>(this, desc);
}

std::unique_ptr<RHIFramebuffer> VKDevice::createFramebuffer(const FramebufferDesc& desc) {
    return std::make_unique<VKFramebuffer>(this, desc);
}

std::unique_ptr<RHISwapchain> VKDevice::createSwapchain(const SwapchainDesc& desc) {
    return std::make_unique<VKSwapchain>(this, desc);
}

std::unique_ptr<RHIDescriptorPool> VKDevice::createDescriptorPool(const DescriptorPoolDesc& desc) {
    return std::make_unique<VKDescriptorPool>(this, desc);
}

std::unique_ptr<RHICommandBuffer> VKDevice::createCommandBuffer(CommandBufferLevel level) {
    return std::make_unique<VKCommandBuffer>(this, level);
}

std::unique_ptr<RHIFence> VKDevice::createFence(bool signaled) {
    return std::make_unique<VKFence>(this, signaled);
}

std::unique_ptr<RHISemaphore> VKDevice::createSemaphore() {
    return std::make_unique<VKSemaphore>(this);
}

void VKDevice::executeImmediate(std::function<void(RHICommandBuffer*)> recordFunc) {
    if (!m_immediateCommandBuffer) {
        m_immediateCommandBuffer = createCommandBuffer(CommandBufferLevel::Primary);
    }

    m_immediateCommandBuffer->begin();
    recordFunc(m_immediateCommandBuffer.get());
    m_immediateCommandBuffer->end();
    m_graphicsQueue->submit({m_immediateCommandBuffer.get()});
    waitIdle();
    m_immediateCommandBuffer->reset();
}

// ============================================================================
// FORMAT CONVERSION
// ============================================================================

VkFormat VKDevice::toVkFormat(Format format) {
    switch (format) {
        case Format::R8_UNORM:      return VK_FORMAT_R8_UNORM;
        case Format::R8_SNORM:      return VK_FORMAT_R8_SNORM;
        case Format::R8_UINT:       return VK_FORMAT_R8_UINT;
        case Format::R8_SINT:       return VK_FORMAT_R8_SINT;
        case Format::R16_FLOAT:     return VK_FORMAT_R16_SFLOAT;
        case Format::R16_UINT:      return VK_FORMAT_R16_UINT;
        case Format::R16_SINT:      return VK_FORMAT_R16_SINT;
        case Format::RG8_UNORM:     return VK_FORMAT_R8G8_UNORM;
        case Format::RG8_SNORM:     return VK_FORMAT_R8G8_SNORM;
        case Format::R32_FLOAT:     return VK_FORMAT_R32_SFLOAT;
        case Format::R32_UINT:      return VK_FORMAT_R32_UINT;
        case Format::R32_SINT:      return VK_FORMAT_R32_SINT;
        case Format::RG16_FLOAT:    return VK_FORMAT_R16G16_SFLOAT;
        case Format::RGBA8_UNORM:   return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::RGBA8_UINT:    return VK_FORMAT_R8G8B8A8_UINT;
        case Format::RGBA8_SINT:    return VK_FORMAT_R8G8B8A8_SINT;
        case Format::RGBA8_SRGB:    return VK_FORMAT_R8G8B8A8_SRGB;
        case Format::BGRA8_UNORM:   return VK_FORMAT_B8G8R8A8_UNORM;
        case Format::BGRA8_SRGB:    return VK_FORMAT_B8G8R8A8_SRGB;
        case Format::RGB10A2_UNORM: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case Format::RG11B10_FLOAT: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        case Format::RG16_UINT:     return VK_FORMAT_R16G16_UINT;
        case Format::RG16_SINT:     return VK_FORMAT_R16G16_SINT;
        case Format::RGB16_SINT:    return VK_FORMAT_R16G16B16_SINT;
        case Format::RGB16_UINT:    return VK_FORMAT_R16G16B16_UINT;
        case Format::RG32_FLOAT:    return VK_FORMAT_R32G32_SFLOAT;
        case Format::RGB32_FLOAT:   return VK_FORMAT_R32G32B32_SFLOAT;
        case Format::RGB32_UINT:    return VK_FORMAT_R32G32B32_UINT;
        case Format::RGBA16_FLOAT:  return VK_FORMAT_R16G16B16A16_SFLOAT;
        case Format::RGBA32_FLOAT:  return VK_FORMAT_R32G32B32A32_SFLOAT;
        case Format::D16_UNORM:     return VK_FORMAT_D16_UNORM;
        case Format::D24_UNORM_S8_UINT: return VK_FORMAT_D24_UNORM_S8_UINT;
        case Format::D32_FLOAT:     return VK_FORMAT_D32_SFLOAT;
        case Format::D32_FLOAT_S8_UINT: return VK_FORMAT_D32_SFLOAT_S8_UINT;
        case Format::BC1_UNORM:     return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case Format::BC1_SRGB:      return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        case Format::BC3_UNORM:     return VK_FORMAT_BC3_UNORM_BLOCK;
        case Format::BC3_SRGB:      return VK_FORMAT_BC3_SRGB_BLOCK;
        case Format::BC5_UNORM:     return VK_FORMAT_BC5_UNORM_BLOCK;
        case Format::BC7_UNORM:     return VK_FORMAT_BC7_UNORM_BLOCK;
        case Format::BC7_SRGB:      return VK_FORMAT_BC7_SRGB_BLOCK;
        default: return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

VkBufferUsageFlags VKDevice::toVkBufferUsage(BufferUsage usage) {
    VkBufferUsageFlags flags = 0;
    if (hasFlag(usage, BufferUsage::Vertex))      flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::Index))       flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::Uniform))     flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::Storage))     flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::Indirect))    flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::TransferSrc)) flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (hasFlag(usage, BufferUsage::TransferDst)) flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    return flags;
}

VkImageType VKDevice::toVkImageType(TextureType type) {
    switch (type) {
        case TextureType::Texture1D: return VK_IMAGE_TYPE_1D;
        case TextureType::Texture3D: return VK_IMAGE_TYPE_3D;
        default: return VK_IMAGE_TYPE_2D;
    }
}

VkImageViewType VKDevice::toVkImageViewType(TextureType type) {
    switch (type) {
        case TextureType::Texture1D:        return VK_IMAGE_VIEW_TYPE_1D;
        case TextureType::Texture2D:        return VK_IMAGE_VIEW_TYPE_2D;
        case TextureType::Texture3D:        return VK_IMAGE_VIEW_TYPE_3D;
        case TextureType::TextureCube:      return VK_IMAGE_VIEW_TYPE_CUBE;
        case TextureType::Texture2DArray:   return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case TextureType::TextureCubeArray: return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        default: return VK_IMAGE_VIEW_TYPE_2D;
    }
}

VkShaderStageFlagBits VKDevice::toVkShaderStage(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:      return VK_SHADER_STAGE_VERTEX_BIT;
        case ShaderStage::Fragment:    return VK_SHADER_STAGE_FRAGMENT_BIT;
        case ShaderStage::Geometry:    return VK_SHADER_STAGE_GEOMETRY_BIT;
        case ShaderStage::TessControl: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        case ShaderStage::TessEval:    return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        case ShaderStage::Compute:     return VK_SHADER_STAGE_COMPUTE_BIT;
        default: return VK_SHADER_STAGE_ALL;
    }
}

VkShaderStageFlags VKDevice::toVkShaderStageFlags(ShaderStage stages) {
    VkShaderStageFlags flags = 0;
    if (static_cast<uint32_t>(stages) & static_cast<uint32_t>(ShaderStage::Vertex))
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (static_cast<uint32_t>(stages) & static_cast<uint32_t>(ShaderStage::Fragment))
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (static_cast<uint32_t>(stages) & static_cast<uint32_t>(ShaderStage::Geometry))
        flags |= VK_SHADER_STAGE_GEOMETRY_BIT;
    if (static_cast<uint32_t>(stages) & static_cast<uint32_t>(ShaderStage::TessControl))
        flags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    if (static_cast<uint32_t>(stages) & static_cast<uint32_t>(ShaderStage::TessEval))
        flags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    if (static_cast<uint32_t>(stages) & static_cast<uint32_t>(ShaderStage::Compute))
        flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    return flags;
}

VkPrimitiveTopology VKDevice::toVkPrimitiveTopology(PrimitiveTopology topology) {
    switch (topology) {
        case PrimitiveTopology::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case PrimitiveTopology::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveTopology::LineStrip:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case PrimitiveTopology::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case PrimitiveTopology::TriangleFan:   return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        case PrimitiveTopology::PatchList:     return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

VkPolygonMode VKDevice::toVkPolygonMode(PolygonMode mode) {
    switch (mode) {
        case PolygonMode::Fill:  return VK_POLYGON_MODE_FILL;
        case PolygonMode::Line:  return VK_POLYGON_MODE_LINE;
        case PolygonMode::Point: return VK_POLYGON_MODE_POINT;
        default: return VK_POLYGON_MODE_FILL;
    }
}

VkCullModeFlags VKDevice::toVkCullMode(CullMode mode) {
    switch (mode) {
        case CullMode::None:         return VK_CULL_MODE_NONE;
        case CullMode::Front:        return VK_CULL_MODE_FRONT_BIT;
        case CullMode::Back:         return VK_CULL_MODE_BACK_BIT;
        case CullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
        default: return VK_CULL_MODE_BACK_BIT;
    }
}

VkFrontFace VKDevice::toVkFrontFace(FrontFace face) {
    switch (face) {
        case FrontFace::CounterClockwise: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
        case FrontFace::Clockwise:        return VK_FRONT_FACE_CLOCKWISE;
        default: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    }
}

VkCompareOp VKDevice::toVkCompareOp(CompareOp op) {
    switch (op) {
        case CompareOp::Never:          return VK_COMPARE_OP_NEVER;
        case CompareOp::Less:           return VK_COMPARE_OP_LESS;
        case CompareOp::Equal:          return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessOrEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater:        return VK_COMPARE_OP_GREATER;
        case CompareOp::NotEqual:       return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always:         return VK_COMPARE_OP_ALWAYS;
        default: return VK_COMPARE_OP_LESS;
    }
}

VkBlendFactor VKDevice::toVkBlendFactor(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::Zero:                  return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::One:                   return VK_BLEND_FACTOR_ONE;
        case BlendFactor::SrcColor:              return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor:              return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::OneMinusDstColor:      return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::SrcAlpha:              return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha:              return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:      return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case BlendFactor::ConstantColor:         return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case BlendFactor::OneMinusConstantColor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case BlendFactor::ConstantAlpha:         return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case BlendFactor::OneMinusConstantAlpha: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        case BlendFactor::SrcAlphaSaturate:      return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        default: return VK_BLEND_FACTOR_ONE;
    }
}

VkBlendOp VKDevice::toVkBlendOp(BlendOp op) {
    switch (op) {
        case BlendOp::Add:             return VK_BLEND_OP_ADD;
        case BlendOp::Subtract:        return VK_BLEND_OP_SUBTRACT;
        case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendOp::Min:             return VK_BLEND_OP_MIN;
        case BlendOp::Max:             return VK_BLEND_OP_MAX;
        default: return VK_BLEND_OP_ADD;
    }
}

VkFilter VKDevice::toVkFilter(Filter filter) {
    switch (filter) {
        case Filter::Nearest: return VK_FILTER_NEAREST;
        case Filter::Linear:  return VK_FILTER_LINEAR;
        default: return VK_FILTER_LINEAR;
    }
}

VkSamplerMipmapMode VKDevice::toVkMipmapMode(MipmapMode mode) {
    switch (mode) {
        case MipmapMode::Nearest: return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        case MipmapMode::Linear:  return VK_SAMPLER_MIPMAP_MODE_LINEAR;
        default: return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
}

VkSamplerAddressMode VKDevice::toVkAddressMode(AddressMode mode) {
    switch (mode) {
        case AddressMode::Repeat:            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case AddressMode::MirroredRepeat:    return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case AddressMode::ClampToEdge:       return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case AddressMode::ClampToBorder:     return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case AddressMode::MirrorClampToEdge: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
        default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

VkDescriptorType VKDevice::toVkDescriptorType(DescriptorType type) {
    switch (type) {
        case DescriptorType::Sampler:              return VK_DESCRIPTOR_TYPE_SAMPLER;
        case DescriptorType::SampledTexture:       return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case DescriptorType::StorageTexture:       return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case DescriptorType::UniformBuffer:        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case DescriptorType::StorageBuffer:        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case DescriptorType::UniformBufferDynamic: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        case DescriptorType::StorageBufferDynamic: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        case DescriptorType::InputAttachment:      return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        default: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
}

VkAttachmentLoadOp VKDevice::toVkLoadOp(LoadOp op) {
    switch (op) {
        case LoadOp::Load:     return VK_ATTACHMENT_LOAD_OP_LOAD;
        case LoadOp::Clear:    return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        default: return VK_ATTACHMENT_LOAD_OP_CLEAR;
    }
}

VkAttachmentStoreOp VKDevice::toVkStoreOp(StoreOp op) {
    switch (op) {
        case StoreOp::Store:    return VK_ATTACHMENT_STORE_OP_STORE;
        case StoreOp::DontCare: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        default: return VK_ATTACHMENT_STORE_OP_STORE;
    }
}

// ============================================================================
// VK QUEUE
// ============================================================================

VKQueue::VKQueue(VKDevice* device, VkQueue queue, uint32_t familyIndex, QueueType type)
    : m_device(device), m_queue(queue), m_familyIndex(familyIndex), m_type(type) {
}

void VKQueue::submit(const std::vector<RHICommandBuffer*>& commandBuffers) {
    std::vector<VkCommandBuffer> vkBuffers;
    vkBuffers.reserve(commandBuffers.size());
    for (auto* cmd : commandBuffers) {
        vkBuffers.push_back(static_cast<VKCommandBuffer*>(cmd)->getVkCommandBuffer());
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = static_cast<uint32_t>(vkBuffers.size());
    submitInfo.pCommandBuffers = vkBuffers.data();

    vkQueueSubmit(m_queue, 1, &submitInfo, VK_NULL_HANDLE);
}

void VKQueue::waitIdle() {
    vkQueueWaitIdle(m_queue);
}

void VKQueue::submitWithSync(const std::vector<RHICommandBuffer*>& commandBuffers,
                              VkSemaphore waitSemaphore, VkSemaphore signalSemaphore,
                              VkFence fence) {
    std::vector<VkCommandBuffer> vkBuffers;
    vkBuffers.reserve(commandBuffers.size());
    for (auto* cmd : commandBuffers) {
        vkBuffers.push_back(static_cast<VKCommandBuffer*>(cmd)->getVkCommandBuffer());
    }

    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = static_cast<uint32_t>(vkBuffers.size());
    submitInfo.pCommandBuffers = vkBuffers.data();

    if (waitSemaphore != VK_NULL_HANDLE) {
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &waitSemaphore;
        submitInfo.pWaitDstStageMask = waitStages;
    }

    if (signalSemaphore != VK_NULL_HANDLE) {
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &signalSemaphore;
    }

    vkQueueSubmit(m_queue, 1, &submitInfo, fence);
}

// ============================================================================
// VK FENCE
// ============================================================================

VKFence::VKFence(VKDevice* device, bool signaled) : m_device(device) {
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (signaled) {
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    }
    vkCreateFence(device->getDevice(), &fenceInfo, nullptr, &m_fence);
}

VKFence::~VKFence() {
    if (m_fence != VK_NULL_HANDLE) {
        vkDestroyFence(m_device->getDevice(), m_fence, nullptr);
    }
}

void VKFence::reset() {
    vkResetFences(m_device->getDevice(), 1, &m_fence);
}

void VKFence::wait(uint64_t timeout) {
    vkWaitForFences(m_device->getDevice(), 1, &m_fence, VK_TRUE, timeout);
}

bool VKFence::isSignaled() const {
    return vkGetFenceStatus(m_device->getDevice(), m_fence) == VK_SUCCESS;
}

// ============================================================================
// VK SEMAPHORE
// ============================================================================

VKSemaphore::VKSemaphore(VKDevice* device) : m_device(device) {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(device->getDevice(), &semaphoreInfo, nullptr, &m_semaphore);
}

VKSemaphore::~VKSemaphore() {
    if (m_semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(m_device->getDevice(), m_semaphore, nullptr);
    }
}

} // namespace RHI
