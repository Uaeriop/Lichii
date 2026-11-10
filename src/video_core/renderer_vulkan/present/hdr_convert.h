// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include "common/common_types.h"
#include "video_core/hdr_output.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class Scheduler;

/// Whether a swapchain or frame format holds an HDR10 signal: the 10-bit formats surfaces offer
/// with VK_COLOR_SPACE_HDR10_ST2084_EXT. Every other target is SDR.
[[nodiscard]] constexpr bool IsHdr10Format(VkFormat format) {
    return format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ||
           format == VK_FORMAT_A2R10G10B10_UNORM_PACK32;
}

/// Converts a present layer's image between SDR and HDR10 (host_shaders/hdr_convert.frag) into
/// an image of its own, at the layer's size, before the layer's filters run.
class HdrConvert final {
public:
    explicit HdrConvert(const Device& device, MemoryAllocator& allocator, size_t image_count,
                        VkExtent2D extent);
    ~HdrConvert();

    [[nodiscard]] VkExtent2D Extent() const {
        return m_extent;
    }

    void Draw(const Device& device, Scheduler& scheduler, size_t image_index,
              VideoCore::HdrOutput::Conversion conversion, f32 paper_white_nits, f32 peak_nits,
              VkImage* inout_image, VkImageView* inout_image_view);

private:
    struct Image {
        vk::DescriptorSets descriptor_sets{};
        vk::Framebuffer framebuffer{};
        vk::Image image{};
        vk::ImageView image_view{};
    };

    void UpdateDescriptorSets(const Device& device, VkImageView image_view, size_t image_index);
    void ClearImages(const Device& device, Scheduler& scheduler);

    std::vector<Image> m_images{};
    const VkExtent2D m_extent;
    vk::ShaderModule m_vertex_shader{};
    vk::ShaderModule m_fragment_shader{};
    vk::DescriptorPool m_descriptor_pool{};
    vk::DescriptorSetLayout m_descriptor_set_layout{};
    vk::PipelineLayout m_pipeline_layout{};
    vk::Pipeline m_pipeline{};
    vk::RenderPass m_renderpass{};
    vk::Sampler m_sampler{};
    bool m_images_ready{};
};

} // namespace Vulkan
