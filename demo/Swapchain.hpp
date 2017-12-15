#pragma once
#include <Swapchain.hpp> 
#include <Render_target.hpp>

class Swapchain : public base::Swapchain
{
public:
    using base::Swapchain::Swapchain;
    void detach() override
    {
        if (!p_color_attachments_.empty()) {
            for (auto p_color_attachment : p_color_attachments_) {
                if (p_color_attachment)
                    delete p_color_attachment;
            }
            p_color_attachments_.clear();
        }

        if (p_rt_cluster_forward_) {
            delete p_rt_cluster_forward_;
            p_rt_cluster_forward_=nullptr;
        }
        if (p_rt_cluster_forward_depth_) {
            delete p_rt_cluster_forward_depth_;
            p_rt_cluster_forward_depth_=nullptr;
        }

        if (!framebuffers.empty()) {
            for (auto &fb : framebuffers) {
                p_dev_->dev.destroyFramebuffer(fb);
            }
            framebuffers.clear();
        }
    }
private:
    base::Render_target *p_rt_cluster_forward_{nullptr};
    base::Render_target *p_rt_cluster_forward_depth_{nullptr};

    // no onscreen depth attachment
    void create_depth_attachment_()  override {}

    void create_color_attachments_(std::vector<vk::Image> &swapchain_images) override
    {
        // onscreen color attachment
        base::Swapchain::create_color_attachments_(swapchain_images);

        // cluster forward color attachment
        p_rt_cluster_forward_=
            new base::Render_target(p_phy_dev_,
                              p_dev_,
                              surface_format_.format,
                              curr_extent_,
                              {vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment},
                              {vk::ImageAspectFlagBits::eColor},
                              vk::SampleCountFlagBits::e4);
        p_rt_cluster_forward_depth_=
            new base::Render_target(p_phy_dev_,
                              p_dev_,
                              depth_format_,
                              curr_extent_,
                              {vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eDepthStencilAttachment},
                              {vk::ImageAspectFlagBits::eDepth},
                              vk::SampleCountFlagBits::e4);
    }

    void create_framebuffers_() override
    {
        assert(image_count_);
        framebuffers.reserve(image_count_);
        vk::ImageView attachments[3];
        attachments[0]=p_rt_cluster_forward_->view;
        attachments[2]=p_rt_cluster_forward_depth_->view;
        for (uint32_t i=0; i < image_count_; i++) {
            attachments[1]=p_color_attachments_[i]->view;
            framebuffers.push_back(p_dev_->dev.createFramebuffer(
                vk::FramebufferCreateInfo({},
                                          p_onscreen_rp_->rp,
                                          3,
                                          attachments,
                                          curr_extent_.width,
                                          curr_extent_.height,
                                          1)));
        }
    }
};

