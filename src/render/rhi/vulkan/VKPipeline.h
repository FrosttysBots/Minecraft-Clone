#pragma once

#include "../RHIPipeline.h"
#include "../RHIDescriptorSet.h"
#include "VKShader.h"
#include <volk.h>

namespace RHI {

class VKDevice;
class VKRenderPass;

// ============================================================================
// VK DESCRIPTOR SET LAYOUT
// ============================================================================

class VKDescriptorSetLayout : public RHIDescriptorSetLayout {
public:
    VKDescriptorSetLayout(VKDevice* device, const DescriptorSetLayoutDesc& desc);
    ~VKDescriptorSetLayout() override;

    const DescriptorSetLayoutDesc& getDesc() const override { return m_desc; }
    void* getNativeHandle() const override { return m_layout; }

    VkDescriptorSetLayout getVkLayout() const { return m_layout; }

private:
    VKDevice* m_device = nullptr;
    DescriptorSetLayoutDesc m_desc;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
};

// ============================================================================
// VK PIPELINE LAYOUT
// ============================================================================

class VKPipelineLayout : public RHIPipelineLayout {
public:
    VKPipelineLayout(VKDevice* device, const PipelineLayoutDesc& desc);
    ~VKPipelineLayout() override;

    void* getNativeHandle() const override { return m_layout; }
    VkPipelineLayout getVkLayout() const { return m_layout; }

private:
    VKDevice* m_device = nullptr;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
};

// ============================================================================
// VK GRAPHICS PIPELINE
// ============================================================================

class VKGraphicsPipeline : public RHIGraphicsPipeline {
public:
    VKGraphicsPipeline(VKDevice* device, const GraphicsPipelineDesc& desc);
    ~VKGraphicsPipeline() override;

    const GraphicsPipelineDesc& getDesc() const override { return m_desc; }
    void* getNativeHandle() const override { return m_pipeline; }

    VkPipeline getVkPipeline() const { return m_pipeline; }
    VkPipelineLayout getVkLayout() const;

private:
    void createPipeline();

    VKDevice* m_device = nullptr;
    GraphicsPipelineDesc m_desc;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

// ============================================================================
// VK COMPUTE PIPELINE
// ============================================================================

class VKComputePipeline : public RHIComputePipeline {
public:
    VKComputePipeline(VKDevice* device, const ComputePipelineDesc& desc);
    ~VKComputePipeline() override;

    const ComputePipelineDesc& getDesc() const override { return m_desc; }
    void* getNativeHandle() const override { return m_pipeline; }

    VkPipeline getVkPipeline() const { return m_pipeline; }
    VkPipelineLayout getVkLayout() const;

private:
    VKDevice* m_device = nullptr;
    ComputePipelineDesc m_desc;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

} // namespace RHI
