#pragma once
#include <vulkan/vulkan.hpp>
#include "Physical_device.hpp"
#include "Device.hpp"
#include "Render_pass.hpp"
#define MSG_PREFIX "-- SWAPCHAIN: "

namespace base
{
class Swapchain
{
public:
    vk::SwapchainKHR swapchain;
    vk::Viewport onscreen_viewport;
    vk::Rect2D onscreen_scissor;
    std::vector<vk::Framebuffer> framebuffers;

    class Depth_attachment
    {
    public:
        vk::Image image;
        vk::ImageView view;
        vk::DeviceMemory mem;
        Depth_attachment(Physical_device* p_phy_dev,
                         Device* p_dev,
                         vk::Format& format,
                         vk::Extent2D& extent)
            :p_dev_(p_dev)
        {

            image=p_dev_->dev.createImage(
                vk::ImageCreateInfo({},
                                    vk::ImageType::e2D,
                                    format,
                                    {extent.width, extent.height, 1},
                                    1,
                                    1,
                                    vk::SampleCountFlagBits::e1,
                                    vk::ImageTiling::eOptimal,
                                    vk::ImageUsageFlagBits::eDepthStencilAttachment
                                    | vk::ImageUsageFlagBits::eTransferSrc));

            vk::MemoryRequirements mem_reqs=p_dev_->dev.getImageMemoryRequirements(image);
            mem=p_dev_->dev.allocateMemory(
                vk::MemoryAllocateInfo(mem_reqs.size,
                                       p_phy_dev->get_memory_type_index(mem_reqs.memoryTypeBits,
                                                                        vk::MemoryPropertyFlagBits::eDeviceLocal)));
            p_dev_->dev.bindImageMemory(image, mem, 0);

            view=p_dev_->dev.createImageView(
                vk::ImageViewCreateInfo({},
                                        image,
                                        vk::ImageViewType::e2D,
                                        format,
                                        {},
                                        {vk::ImageAspectFlagBits::eDepth, 0, 1, 0,
                                        1}));
        }
        ~Depth_attachment()
        {
            if (view) p_dev_->dev.destroyImageView(view);
            if (image) p_dev_->dev.destroyImage(image);
            if (mem) p_dev_->dev.freeMemory(mem);
        }

    private:
        Device* p_dev_;
    };

    class Color_attachment
    {
    public:
        vk::Image image;
        vk::ImageView view;
        Color_attachment(Device* p_dev, vk::Image& swapchain_image, vk::Format& format)
            :p_dev_(p_dev),
            image(swapchain_image)
        {
            view=p_dev_->dev.createImageView(
                vk::ImageViewCreateInfo({},
                                        swapchain_image,
                                        vk::ImageViewType::e2D,
                                        format,
                                        {},
                                        {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}));
        }
        ~Color_attachment()
        {
            if (view) p_dev_->dev.destroyImageView(view);
        }

    private:
        Device* p_dev_;
    };

    Swapchain(Physical_device* p_phy_dev,
              Device* p_dev,
              vk::SurfaceKHR surface,
              vk::SurfaceFormatKHR surface_format,
              vk::Format depth_format,
              uint32_t image_count,
              Render_pass* p_onscreen_rp)
        :p_phy_dev_(p_phy_dev),
        p_dev_(p_dev),
        surface_(surface),
        surface_format_(surface_format),
        depth_format_(depth_format),
        image_count_(image_count),
        p_onscreen_rp_(p_onscreen_rp)
    {}

    virtual ~Swapchain()
    {
        // need to call detach() in the program before deconstruction
        assert(p_depth_attachment_ == nullptr);
        assert(p_color_attachments_.size() == 0);
        if (swapchain) p_dev_->dev.destroySwapchainKHR(swapchain);
    }

    void resize(uint32_t width_hint, uint32_t height_hint)
    {
        vk::SurfaceCapabilitiesKHR caps=p_phy_dev_->phy_dev.getSurfaceCapabilitiesKHR(surface_);
        assert(caps.supportedUsageFlags & vk::ImageUsageFlagBits::eColorAttachment);

        // create new_extent
        vk::Extent2D new_extent=caps.currentExtent;
        new_extent.width=std::min(caps.maxImageExtent.width,
                                  std::max(caps.minImageExtent.width, width_hint));
        new_extent.height=std::min(caps.maxImageExtent.height,
                                   std::max(caps.minImageExtent.height, height_hint));
        if (curr_extent_.width == new_extent.width && curr_extent_.height == new_extent.height)
            return;

        // image count
        assert(image_count_ <= caps.maxImageCount && image_count_ >= caps.minImageCount);

        // composite alpha
        vk::CompositeAlphaFlagBitsKHR composite_alpha=vk::CompositeAlphaFlagBitsKHR::eOpaque;
        if (caps.supportedCompositeAlpha & composite_alpha) {
            std::vector<vk::CompositeAlphaFlagBitsKHR> composite_alpha_flags={
                vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
                vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
                vk::CompositeAlphaFlagBitsKHR::eInherit
            };
            for (auto& composite_alpha_flag : composite_alpha_flags) {
                if (caps.supportedCompositeAlpha & composite_alpha_flag) {
                    composite_alpha=composite_alpha_flag;
                    break;
                }
            }
        }
        else {
            std::string errstr=MSG_PREFIX;
            errstr.append("composite alpha not supported");
            throw std::runtime_error(errstr);
        }

        // pre_transform
        vk::SurfaceTransformFlagBitsKHR pre_transform=
            (bool)(caps.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity) ?
            vk::SurfaceTransformFlagBitsKHR::eIdentity : caps.currentTransform;

        // present mode
        vk::PresentModeKHR present_mode=vk::PresentModeKHR::eFifo;

        // sharing mode
        vk::SharingMode sharing_mode;
        std::vector<uint32_t> queue_families;
        if (p_phy_dev_->graphics_queue_family_idx != p_phy_dev_->present_queue_family_idx) {
            sharing_mode=vk::SharingMode::eConcurrent;
            queue_families.emplace_back(p_phy_dev_->graphics_queue_family_idx);
            queue_families.emplace_back(p_phy_dev_->present_queue_family_idx);
        }
        else {
            sharing_mode=vk::SharingMode::eExclusive;
        }

        // create new swapchain
        auto old_swapchain=swapchain;
        swapchain=p_dev_->dev.createSwapchainKHR(
            vk::SwapchainCreateInfoKHR({},
                                       surface_,
                                       image_count_,
                                       surface_format_.format,
                                       surface_format_.colorSpace,
                                       new_extent,
                                       1,
                                       vk::ImageUsageFlagBits::eColorAttachment,
                                       sharing_mode,
                                       static_cast<uint32_t>(queue_families.size()),
                                       queue_families.empty() ? nullptr : queue_families.data(),
                                       pre_transform,
                                       composite_alpha,
                                       present_mode,
                                       VK_TRUE,
                                       old_swapchain));

        // destroy old swapchain
        if (old_swapchain) {
            p_dev_->dev.waitIdle();
            detach();
            p_dev_->dev.destroySwapchainKHR(old_swapchain);
        }

        // attach framebuffers
        curr_extent_=new_extent;
        attach();

        std::cout << MSG_PREFIX << "swapchain resized to " << curr_extent_.width << " x " << curr_extent_.height
            << std::endl;
    }

    void attach()
    {
        // update render area
        onscreen_viewport=vk::Viewport(0.f, 0.f,
                                       static_cast<float>(curr_extent_.width),
                                       static_cast<float>(curr_extent_.height),
                                       0.f, 1.f);
        onscreen_scissor=vk::Rect2D({0, 0}, curr_extent_);
        p_onscreen_rp_->update_render_area(onscreen_scissor);

        // swapchain images
        std::vector<vk::Image> swapchain_images=p_dev_->dev.getSwapchainImagesKHR(swapchain);
        assert(!swapchain_images.empty());

        if (depth_format_ != vk::Format::eUndefined) {
            create_depth_attachment_();
        }
        create_color_attachments_(swapchain_images);
        create_framebuffers_();

        std::cout << MSG_PREFIX << "swapchain attached" << std::endl;
    }

    virtual void detach()
    {
        if (p_depth_attachment_) {
            delete p_depth_attachment_;
            p_depth_attachment_=nullptr;
        }
        if (!p_color_attachments_.empty()) {
            for (auto p_color_attachment : p_color_attachments_) {
                delete p_color_attachment;
            }
            p_color_attachments_.clear();
        }
        if (!framebuffers.empty()) {
            for (auto& fb : framebuffers) {
                p_dev_->dev.destroyFramebuffer(fb);
            }
            framebuffers.clear();
        }

        std::cout << MSG_PREFIX << "swapchain detached" << std::endl;
    }

    vk::Extent2D curr_extent() const
    {
        return curr_extent_;
    }

    uint32_t image_count() const
    {
        return image_count_;
    }

protected:
    Physical_device* p_phy_dev_;
    Device* p_dev_;
    vk::SurfaceKHR surface_;
    vk::SurfaceFormatKHR surface_format_;
    vk::Format depth_format_;

    uint32_t image_count_{0};
    vk::Extent2D curr_extent_;

    Render_pass* p_onscreen_rp_;

    std::vector<Color_attachment*> p_color_attachments_;
    Depth_attachment* p_depth_attachment_=nullptr;

    virtual void create_depth_attachment_()
    {
        p_depth_attachment_=new Depth_attachment(p_phy_dev_, p_dev_, depth_format_, curr_extent_);
    }

    virtual void create_color_attachments_(std::vector<vk::Image> &swapchain_images)
    {
        assert(image_count_);
        p_color_attachments_.reserve(image_count_);
        for (uint32_t i=0; i < image_count_; i++) {
            p_color_attachments_.emplace_back(new Color_attachment(p_dev_, swapchain_images[i], surface_format_.format));
        }
    }

    virtual void create_framebuffers_()
    {
        assert(image_count_);
        framebuffers.reserve(image_count_);
        vk::ImageView attachments[2];
        if (p_depth_attachment_) {
            attachments[1]=p_depth_attachment_->view;
        }
        for (uint32_t i=0; i < image_count_; i++) {
            attachments[0]=p_color_attachments_[i]->view;
            framebuffers.push_back(p_dev_->dev.createFramebuffer(
                vk::FramebufferCreateInfo({},
                                          p_onscreen_rp_->rp,
                                          p_depth_attachment_ ? 2 : 1,
                                          attachments,
                                          curr_extent_.width,
                                          curr_extent_.height,
                                          1)));
        }
    }
};
} // namespace base

#undef MSG_PREFIX