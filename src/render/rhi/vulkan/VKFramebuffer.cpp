#include "VKFramebuffer.h"
#include "VKDevice.h"
#include <iostream>
#include <algorithm>
#include <limits>
#include <array>

namespace RHI {

// ============================================================================
// VK RENDER PASS
// ============================================================================

VKRenderPass::VKRenderPass(VKDevice* device, const RenderPassDesc& desc)
    : m_device(device), m_desc(desc) {

    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> colorRefs;
    VkAttachmentReference depthRef{};
    bool hasDepth = false;

    uint32_t attachmentIndex = 0;

    // Process color attachments
    for (const auto& colorAttachment : desc.colorAttachments) {
        VkAttachmentDescription attachment{};
        attachment.format = VKDevice::toVkFormat(colorAttachment.format);
        attachment.samples = static_cast<VkSampleCountFlagBits>(colorAttachment.samples);
        attachment.loadOp = VKDevice::toVkLoadOp(colorAttachment.loadOp);
        attachment.storeOp = VKDevice::toVkStoreOp(colorAttachment.storeOp);
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        attachments.push_back(attachment);

        VkAttachmentReference ref{};
        ref.attachment = attachmentIndex++;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorRefs.push_back(ref);
    }

    // Process depth attachment
    if (desc.hasDepthStencil && desc.depthStencilAttachment.format != Format::Unknown) {
        VkAttachmentDescription attachment{};
        attachment.format = VKDevice::toVkFormat(desc.depthStencilAttachment.format);
        attachment.samples = static_cast<VkSampleCountFlagBits>(desc.depthStencilAttachment.samples);
        attachment.loadOp = VKDevice::toVkLoadOp(desc.depthStencilAttachment.loadOp);
        attachment.storeOp = VKDevice::toVkStoreOp(desc.depthStencilAttachment.storeOp);
        attachment.stencilLoadOp = VKDevice::toVkLoadOp(desc.depthStencilAttachment.stencilLoadOp);
        attachment.stencilStoreOp = VKDevice::toVkStoreOp(desc.depthStencilAttachment.stencilStoreOp);
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        attachments.push_back(attachment);

        depthRef.attachment = attachmentIndex++;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        hasDepth = true;
    }

    // Create subpass
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
    subpass.pColorAttachments = colorRefs.data();
    subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

    // Subpass dependencies
    std::array<VkSubpassDependency, 2> dependencies{};

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    VkResult result = vkCreateRenderPass(m_device->getDevice(), &renderPassInfo, nullptr, &m_renderPass);

    if (result != VK_SUCCESS) {
        std::cerr << "[VKRenderPass] Failed to create render pass" << std::endl;
    }
}

VKRenderPass::~VKRenderPass() {
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device->getDevice(), m_renderPass, nullptr);
    }
}

// ============================================================================
// VK FRAMEBUFFER
// ============================================================================

VKFramebuffer::VKFramebuffer(VKDevice* device, const FramebufferDesc& desc)
    : m_device(device), m_desc(desc) {

    std::vector<VkImageView> attachmentViews;
    attachmentViews.reserve(desc.colorAttachments.size() + 1);

    for (const auto& attachment : desc.colorAttachments) {
        if (attachment.texture) {
            auto* vkTexture = static_cast<VKTexture*>(attachment.texture);
            attachmentViews.push_back(vkTexture->getVkImageView());
        }
    }

    if (desc.depthStencilAttachment.texture) {
        auto* vkTexture = static_cast<VKTexture*>(desc.depthStencilAttachment.texture);
        attachmentViews.push_back(vkTexture->getVkImageView());
    }

    auto* renderPass = static_cast<VKRenderPass*>(desc.renderPass);

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass->getVkRenderPass();
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachmentViews.size());
    framebufferInfo.pAttachments = attachmentViews.data();
    framebufferInfo.width = desc.width;
    framebufferInfo.height = desc.height;
    framebufferInfo.layers = desc.layers;

    VkResult result = vkCreateFramebuffer(m_device->getDevice(), &framebufferInfo, nullptr, &m_framebuffer);

    if (result != VK_SUCCESS) {
        std::cerr << "[VKFramebuffer] Failed to create framebuffer" << std::endl;
    }
}

VKFramebuffer::~VKFramebuffer() {
    if (m_framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device->getDevice(), m_framebuffer, nullptr);
    }
}

// ============================================================================
// VK SWAPCHAIN
// ============================================================================

VKSwapchain::VKSwapchain(VKDevice* device, const SwapchainDesc& desc)
    : m_device(device), m_desc(desc) {

    // Get the surface from the device (created during device initialization)
    // We need to query it from GLFW
    VkResult result = glfwCreateWindowSurface(m_device->getInstance(),
                                               static_cast<GLFWwindow*>(desc.windowHandle),
                                               nullptr, &m_surface);
    if (result != VK_SUCCESS) {
        std::cerr << "[VKSwapchain] Failed to create window surface" << std::endl;
        return;
    }

    createSwapchain();
    createImageViews();
    createDepthResources();
    createRenderPass();
    createUIRenderPass();
    createFramebuffers();
    createSyncObjects();
}

VKSwapchain::~VKSwapchain() {
    cleanup();

    // Destroy render passes (not cleaned up by cleanup() since they're format-dependent, not size-dependent)
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device->getDevice(), m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
    if (m_uiRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device->getDevice(), m_uiRenderPass, nullptr);
        m_uiRenderPass = VK_NULL_HANDLE;
    }

    // Destroy sync objects
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(m_device->getDevice(), m_renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(m_device->getDevice(), m_imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(m_device->getDevice(), m_inFlightFences[i], nullptr);
    }

    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_device->getInstance(), m_surface, nullptr);
    }
}

SwapchainSupportDetails VKSwapchain::querySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapchainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

void VKSwapchain::createSwapchain() {
    SwapchainSupportDetails support = querySwapchainSupport(m_device->getPhysicalDevice(), m_surface);

    VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats);
    VkPresentModeKHR presentMode = choosePresentMode(support.presentModes);
    VkExtent2D extent = chooseExtent(support.capabilities);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    const auto& queueFamilies = m_device->getQueueFamilies();
    uint32_t queueFamilyIndices[] = {queueFamilies.graphicsFamily.value(), queueFamilies.presentFamily.value()};

    if (queueFamilies.graphicsFamily != queueFamilies.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VkResult result = vkCreateSwapchainKHR(m_device->getDevice(), &createInfo, nullptr, &m_swapchain);
    if (result != VK_SUCCESS) {
        std::cerr << "[VKSwapchain] Failed to create swapchain" << std::endl;
        return;
    }

    vkGetSwapchainImagesKHR(m_device->getDevice(), m_swapchain, &imageCount, nullptr);
    m_images.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device->getDevice(), m_swapchain, &imageCount, m_images.data());

    m_imageFormat = surfaceFormat.format;
    m_extent = extent;
}

void VKSwapchain::createImageViews() {
    m_imageViews.resize(m_images.size());
    m_textures.resize(m_images.size());

    for (size_t i = 0; i < m_images.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_images[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_imageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        VkResult result = vkCreateImageView(m_device->getDevice(), &createInfo, nullptr, &m_imageViews[i]);
        if (result != VK_SUCCESS) {
            std::cerr << "[VKSwapchain] Failed to create image view " << i << std::endl;
        }

        // Create texture wrapper for the swapchain image
        TextureDesc texDesc{};
        texDesc.type = TextureType::Texture2D;
        texDesc.width = m_extent.width;
        texDesc.height = m_extent.height;
        texDesc.format = m_desc.format;
        texDesc.mipLevels = 1;
        texDesc.arrayLayers = 1;

        m_textures[i] = std::make_unique<VKTexture>(m_device, m_images[i], m_imageViews[i], texDesc);
    }
}

void VKSwapchain::createDepthResources() {
    VkDevice device = m_device->getDevice();
    VkPhysicalDevice physDevice = m_device->getPhysicalDevice();

    // Create depth image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_extent.width;
    imageInfo.extent.height = m_extent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = m_depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &m_depthImage) != VK_SUCCESS) {
        std::cerr << "[VKSwapchain] Failed to create depth image" << std::endl;
        return;
    }

    // Allocate memory for depth image
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, m_depthImage, &memRequirements);

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physDevice, &memProperties);

    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((memRequirements.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memTypeIndex = i;
            break;
        }
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memTypeIndex;

    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_depthImageMemory) != VK_SUCCESS) {
        std::cerr << "[VKSwapchain] Failed to allocate depth image memory" << std::endl;
        return;
    }

    vkBindImageMemory(device, m_depthImage, m_depthImageMemory, 0);

    // Create depth image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &m_depthImageView) != VK_SUCCESS) {
        std::cerr << "[VKSwapchain] Failed to create depth image view" << std::endl;
        return;
    }

    std::cout << "[VKSwapchain] Created depth buffer (" << m_extent.width << "x" << m_extent.height << ")" << std::endl;
}

void VKSwapchain::createSyncObjects() {
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(m_device->getDevice(), &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device->getDevice(), &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device->getDevice(), &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            std::cerr << "[VKSwapchain] Failed to create synchronization objects" << std::endl;
        }
    }
}

RHIRenderPass* VKSwapchain::getSwapchainRenderPass() {
    return m_rhiRenderPass.get();
}

RHIFramebuffer* VKSwapchain::getCurrentFramebufferRHI() {
    if (m_currentImageIndex < m_rhiFramebuffers.size()) {
        return m_rhiFramebuffers[m_currentImageIndex].get();
    }
    return nullptr;
}

void VKSwapchain::createRenderPass() {
    // Color attachment
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_imageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Depth attachment
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = m_depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_device->getDevice(), &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        std::cerr << "[VKSwapchain] Failed to create render pass" << std::endl;
    } else {
        std::cout << "[VKSwapchain] Created swapchain render pass with depth buffer" << std::endl;

        // Create RHI wrapper for the render pass
        RenderPassDesc rpDesc;
        AttachmentDesc colorDesc;
        colorDesc.format = m_desc.format;
        colorDesc.samples = 1;
        colorDesc.loadOp = LoadOp::Clear;
        colorDesc.storeOp = StoreOp::Store;
        colorDesc.initialLayout = TextureLayout::Undefined;
        colorDesc.finalLayout = TextureLayout::PresentSrc;
        rpDesc.colorAttachments.push_back(colorDesc);

        AttachmentDesc depthDesc;
        depthDesc.format = Format::D32_FLOAT;
        depthDesc.samples = 1;
        depthDesc.loadOp = LoadOp::Clear;
        depthDesc.storeOp = StoreOp::DontCare;
        depthDesc.initialLayout = TextureLayout::Undefined;
        depthDesc.finalLayout = TextureLayout::DepthStencilAttachment;
        rpDesc.depthStencilAttachment = depthDesc;
        rpDesc.hasDepthStencil = true;
        rpDesc.debugName = "SwapchainRenderPass";

        m_rhiRenderPass = std::make_unique<VKSwapchainRenderPass>(m_renderPass, rpDesc);
    }
}

void VKSwapchain::createUIRenderPass() {
    // UI overlay render pass - uses LOAD to preserve existing content
    // This render pass expects the image to already be in COLOR_ATTACHMENT_OPTIMAL
    // (caller must transition from PRESENT_SRC_KHR before beginning this pass)
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_imageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // Preserve existing content
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;  // Caller transitions here first
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Depth attachment - also LOAD to preserve depth buffer
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = m_depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // Preserve depth
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;  // Wait for previous writes
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_device->getDevice(), &renderPassInfo, nullptr, &m_uiRenderPass) != VK_SUCCESS) {
        std::cerr << "[VKSwapchain] Failed to create UI overlay render pass" << std::endl;
    } else {
        std::cout << "[VKSwapchain] Created UI overlay render pass" << std::endl;

        // Create RHI wrapper
        RenderPassDesc rpDesc;
        AttachmentDesc colorDesc;
        colorDesc.format = m_desc.format;
        colorDesc.samples = 1;
        colorDesc.loadOp = LoadOp::Load;
        colorDesc.storeOp = StoreOp::Store;
        colorDesc.initialLayout = TextureLayout::ColorAttachment;  // Caller transitions here first
        colorDesc.finalLayout = TextureLayout::PresentSrc;
        rpDesc.colorAttachments.push_back(colorDesc);

        AttachmentDesc depthDesc;
        depthDesc.format = Format::D32_FLOAT;
        depthDesc.samples = 1;
        depthDesc.loadOp = LoadOp::Load;
        depthDesc.storeOp = StoreOp::DontCare;
        depthDesc.initialLayout = TextureLayout::DepthStencilAttachment;
        depthDesc.finalLayout = TextureLayout::DepthStencilAttachment;
        rpDesc.depthStencilAttachment = depthDesc;
        rpDesc.hasDepthStencil = true;
        rpDesc.debugName = "UIOverlayRenderPass";

        m_rhiUIRenderPass = std::make_unique<VKSwapchainRenderPass>(m_uiRenderPass, rpDesc);
    }
}

RHIRenderPass* VKSwapchain::getUIRenderPass() {
    return m_rhiUIRenderPass.get();
}

void VKSwapchain::createFramebuffers() {
    m_framebuffers.resize(m_imageViews.size());
    m_rhiFramebuffers.resize(m_imageViews.size());

    for (size_t i = 0; i < m_imageViews.size(); i++) {
        std::array<VkImageView, 2> attachments = { m_imageViews[i], m_depthImageView };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = m_extent.width;
        framebufferInfo.height = m_extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_device->getDevice(), &framebufferInfo, nullptr, &m_framebuffers[i]) != VK_SUCCESS) {
            std::cerr << "[VKSwapchain] Failed to create framebuffer " << i << std::endl;
        }

        // Create RHI wrapper for the framebuffer
        m_rhiFramebuffers[i] = std::make_unique<VKSwapchainFramebuffer>(
            m_framebuffers[i], m_extent.width, m_extent.height);
    }
    std::cout << "[VKSwapchain] Created " << m_framebuffers.size() << " swapchain framebuffers with depth" << std::endl;
}

void VKSwapchain::cleanup() {
    VkDevice device = m_device->getDevice();

    // Clear RHI wrappers first (they reference Vulkan resources)
    m_rhiFramebuffers.clear();

    // Destroy framebuffers first (they reference image views)
    for (auto fb : m_framebuffers) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, fb, nullptr);
        }
    }
    m_framebuffers.clear();

    // Destroy depth resources
    if (m_depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_depthImageView, nullptr);
        m_depthImageView = VK_NULL_HANDLE;
    }
    if (m_depthImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, m_depthImage, nullptr);
        m_depthImage = VK_NULL_HANDLE;
    }
    if (m_depthImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_depthImageMemory, nullptr);
        m_depthImageMemory = VK_NULL_HANDLE;
    }

    m_textures.clear();

    for (auto imageView : m_imageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    m_imageViews.clear();

    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

void VKSwapchain::recreate() {
    // Wait for the device to be idle
    vkDeviceWaitIdle(m_device->getDevice());

    cleanup();
    createSwapchain();
    createImageViews();
    createDepthResources();  // Must recreate depth buffer for new size
    createFramebuffers();  // Recreate framebuffers for new images
}

VkSurfaceFormatKHR VKSwapchain::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    // Prefer SRGB format
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    // Fallback to first available
    return formats[0];
}

VkPresentModeKHR VKSwapchain::choosePresentMode(const std::vector<VkPresentModeKHR>& modes) {
    // Map our VSync setting to Vulkan present mode
    VkPresentModeKHR preferred = m_desc.vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;

    for (const auto& mode : modes) {
        if (mode == preferred) {
            return mode;
        }
    }

    // FIFO is guaranteed to be available
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VKSwapchain::chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != (std::numeric_limits<uint32_t>::max)()) {
        return capabilities.currentExtent;
    }

    VkExtent2D actualExtent = {m_desc.width, m_desc.height};

    actualExtent.width = (std::clamp)(actualExtent.width,
                                       capabilities.minImageExtent.width,
                                       capabilities.maxImageExtent.width);
    actualExtent.height = (std::clamp)(actualExtent.height,
                                        capabilities.minImageExtent.height,
                                        capabilities.maxImageExtent.height);

    return actualExtent;
}

RHITexture* VKSwapchain::getCurrentTexture() {
    return m_textures[m_currentImageIndex].get();
}

bool VKSwapchain::acquireNextImage() {
    vkWaitForFences(m_device->getDevice(), 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(m_device->getDevice(), m_swapchain, UINT64_MAX,
                                             m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE,
                                             &m_currentImageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate();
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        std::cerr << "[VKSwapchain] Failed to acquire swapchain image" << std::endl;
        return false;
    }

    vkResetFences(m_device->getDevice(), 1, &m_inFlightFences[m_currentFrame]);
    return true;
}

bool VKSwapchain::present() {
    VkSemaphore waitSemaphores[] = {m_renderFinishedSemaphores[m_currentFrame]};

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = waitSemaphores;

    VkSwapchainKHR swapChains[] = {m_swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &m_currentImageIndex;
    presentInfo.pResults = nullptr;

    auto* queue = static_cast<VKQueue*>(m_device->getGraphicsQueue());
    VkResult result = vkQueuePresentKHR(queue->getVkQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreate();
    } else if (result != VK_SUCCESS) {
        std::cerr << "[VKSwapchain] Failed to present swapchain image" << std::endl;
        return false;
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    return true;
}

void VKSwapchain::resize(uint32_t width, uint32_t height) {
    m_desc.width = width;
    m_desc.height = height;
    recreate();
}

} // namespace RHI
