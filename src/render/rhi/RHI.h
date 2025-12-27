#pragma once

// ============================================================================
// RHI - Rendering Hardware Interface
// ============================================================================
// Cross-backend rendering abstraction layer supporting OpenGL 4.6 and Vulkan.
//
// Usage:
//   #include <render/rhi/RHI.h>
//
//   // Create device
//   auto device = RHI::RHIDevice::create(RHI::Backend::OpenGL, window);
//
//   // Create resources
//   auto buffer = device->createBuffer({...});
//   auto texture = device->createTexture({...});
//   auto pipeline = device->createGraphicsPipeline({...});
//
//   // Record commands
//   auto cmdBuffer = device->createCommandBuffer();
//   cmdBuffer->begin();
//   cmdBuffer->beginRenderPass(...);
//   cmdBuffer->bindGraphicsPipeline(pipeline.get());
//   cmdBuffer->draw(3);
//   cmdBuffer->endRenderPass();
//   cmdBuffer->end();
//
//   // Submit
//   device->getGraphicsQueue()->submit({cmdBuffer.get()});
//
// ============================================================================

// Core types and enums
#include "RHITypes.h"

// Resource interfaces
#include "RHIBuffer.h"
#include "RHITexture.h"
#include "RHIShader.h"
#include "RHIPipeline.h"
#include "RHIFramebuffer.h"
#include "RHIDescriptorSet.h"

// Command recording
#include "RHICommandBuffer.h"

// Device and factory
#include "RHIDevice.h"
