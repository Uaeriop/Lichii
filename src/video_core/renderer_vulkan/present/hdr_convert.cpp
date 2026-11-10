// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>

#include "video_core/host_shaders/full_screen_triangle_vert_spv.h"
#include "video_core/host_shaders/hdr_convert_frag_spv.h"
#include "video_core/renderer_vulkan/present/hdr_convert.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

namespace {

// Float: an HDR10 signal needs more than 8 bits, and the layer's filters take float images from
// the anti-aliasing passes already.
constexpr VkFormat ImageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

/// full_screen_triangle.vert's push constants, then hdr_convert.frag's.
struct PushConstants {
    std::array<f32, 2> tex_scale;
    std::array<f32, 2> tex_offset;
    s32 mode;
    f32 paper_white;
    f32 peak;
};
constexpr u32 VertexConstantsSize = 16;

} // Anonymous namespace

HdrConvert::HdrConvert(const Device& device, MemoryAllocator& allocator, size_t image_count,
                       VkExtent2D extent)
    : m_extent(extent) {
    m_renderpass = CreateWrappedRenderPass(device, ImageFormat);
    m_sampler = CreateWrappedSampler(device, VK_FILTER_NEAREST);
    m_vertex_shader = BuildShader(device, FULL_SCREEN_TRIANGLE_VERT_SPV);
    m_fragment_shader = BuildShader(device, HDR_CONVERT_FRAG_SPV);
    m_descriptor_pool = CreateWrappedDescriptorPool(device, image_count, image_count);
    m_descriptor_set_layout =
        CreateWrappedDescriptorSetLayout(device, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER});

    const std::array ranges{
        VkPushConstantRange{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = VertexConstantsSize,
        },
        VkPushConstantRange{
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = VertexConstantsSize,
            .size = sizeof(PushConstants) - VertexConstantsSize,
        },
    };
    m_pipeline_layout = device.GetLogical().CreatePipelineLayout(VkPipelineLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = 1,
        .pSetLayouts = m_descriptor_set_layout.address(),
        .pushConstantRangeCount = static_cast<u32>(ranges.size()),
        .pPushConstantRanges = ranges.data(),
    });
    m_pipeline = CreateWrappedPipeline(device, m_renderpass, m_pipeline_layout,
                                       std::tie(m_vertex_shader, m_fragment_shader));

    const VkDescriptorSetLayout layout = *m_descriptor_set_layout;
    m_images.resize(image_count);
    for (Image& image : m_images) {
        image.image = CreateWrappedImage(allocator, m_extent, ImageFormat);
        image.image_view = CreateWrappedImageView(device, image.image, ImageFormat);
        image.framebuffer =
            CreateWrappedFramebuffer(device, m_renderpass, image.image_view, m_extent);
        image.descriptor_sets = CreateWrappedDescriptorSets(m_descriptor_pool, {layout});
    }
}

HdrConvert::~HdrConvert() = default;

void HdrConvert::UpdateDescriptorSets(const Device& device, VkImageView image_view,
                                      size_t image_index) {
    std::vector<VkDescriptorImageInfo> image_infos;
    image_infos.reserve(1);
    const std::array updates{CreateWriteDescriptorSet(
        image_infos, *m_sampler, image_view, m_images[image_index].descriptor_sets[0], 0)};
    device.GetLogical().UpdateDescriptorSets(updates, {});
}

void HdrConvert::ClearImages(const Device& device, Scheduler& scheduler) {
    if (m_images_ready) {
        return;
    }
    scheduler.Record([this](vk::CommandBuffer cmdbuf) {
        for (auto& image : m_images) {
            ClearColorImage(cmdbuf, *image.image);
        }
    });
    scheduler.Finish();
    m_images_ready = true;
}

void HdrConvert::Draw(const Device& device, Scheduler& scheduler, size_t image_index,
                      VideoCore::HdrOutput::Conversion conversion, f32 paper_white_nits,
                      f32 peak_nits, VkImage* inout_image, VkImageView* inout_image_view) {
    const Image& image{m_images[image_index]};
    const VkImage input_image{*inout_image};
    const VkImage output_image{*image.image};
    const VkDescriptorSet descriptor_set{image.descriptor_sets[0]};
    const VkFramebuffer framebuffer{*image.framebuffer};
    const VkRenderPass renderpass{*m_renderpass};
    const VkPipeline pipeline{*m_pipeline};
    const VkPipelineLayout layout{*m_pipeline_layout};
    const VkExtent2D extent{m_extent};
    const PushConstants constants{
        .tex_scale = {1.0f, 1.0f},
        .tex_offset = {0.0f, 0.0f},
        .mode = static_cast<s32>(conversion),
        .paper_white = paper_white_nits,
        .peak = peak_nits,
    };

    ClearImages(device, scheduler);
    UpdateDescriptorSets(device, *inout_image_view, image_index);

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([=](vk::CommandBuffer cmdbuf) {
        TransitionImageLayout(cmdbuf, input_image, VK_IMAGE_LAYOUT_GENERAL);
        TransitionImageLayout(cmdbuf, output_image, VK_IMAGE_LAYOUT_GENERAL);
        BeginRenderPass(cmdbuf, renderpass, framebuffer, extent);
        cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, descriptor_set, {});
        cmdbuf.PushConstants(layout, VK_SHADER_STAGE_VERTEX_BIT, 0, VertexConstantsSize,
                             &constants);
        cmdbuf.PushConstants(layout, VK_SHADER_STAGE_FRAGMENT_BIT, VertexConstantsSize,
                             sizeof(PushConstants) - VertexConstantsSize, &constants.mode);
        cmdbuf.Draw(3, 1, 0, 0);
        cmdbuf.EndRenderPass();
        TransitionImageLayout(cmdbuf, output_image, VK_IMAGE_LAYOUT_GENERAL);
    });

    *inout_image = *image.image;
    *inout_image_view = *image.image_view;
}

} // namespace Vulkan
