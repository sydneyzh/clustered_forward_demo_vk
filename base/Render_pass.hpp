#pragma once
#include <vulkan/vulkan.hpp>
#include "Device.hpp"

namespace base
{
class Render_pass
{
public:
    vk::RenderPass rp;
    vk::RenderPassBeginInfo rp_begin;

    Render_pass()=delete;
    Render_pass(Device* p_dev,
                const uint32_t clear_value_count,
                vk::ClearValue* p_clear_value)
        :p_dev_(p_dev),
        clear_value_count_(clear_value_count),
        p_clear_value_(p_clear_value)
    {}

    ~Render_pass()
    {
        if (rp) p_dev_->dev.destroyRenderPass(rp);
    }

    void create(const uint32_t attachment_count,
                vk::AttachmentDescription* attachments,
                const uint32_t subpass_count,
                vk::SubpassDescription* subpass_descriptions,
                const uint32_t dependency_count=0,
                vk::SubpassDependency* dependencies=nullptr)
    {

        if (rp) p_dev_->dev.destroyRenderPass(rp);
        rp=p_dev_->dev.createRenderPass(
            vk::RenderPassCreateInfo({},
                                     attachment_count, attachments,
                                     subpass_count, subpass_descriptions,
                                     dependency_count, dependencies));

        rp_begin=vk::RenderPassBeginInfo(rp,
                                         nullptr, // framebuffer
                                         {}, // render area
                                         clear_value_count_,
                                         p_clear_value_);
    }

    void update_render_area(const vk::Rect2D& render_area)
    {
        rp_begin.renderArea=render_area;
    }

    void update_framebuffer(const vk::Framebuffer& fb)
    {
        rp_begin.framebuffer=fb;
    }

private:
    Device* p_dev_;

    uint32_t clear_value_count_;
    vk::ClearValue* p_clear_value_;
};
} // namespace base
