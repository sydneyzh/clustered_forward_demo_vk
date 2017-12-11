#pragma once
#include <vulkan/vulkan.hpp>
#include <Physical_device.hpp>
#include <Device.hpp>

class Render_target
{
public:
    vk::Image image;
    vk::DeviceMemory mem;
    vk::ImageView view;
    vk::Format format;
    vk::Sampler sampler;
    vk::DescriptorImageInfo desc_image_info;
    Render_target(base::Physical_device* p_phy_dev,
                  base::Device* p_dev,
                  const vk::Format format,
                  const vk::Extent2D extent,
                  const vk::ImageUsageFlags usage,
                  const vk::ImageAspectFlags aspect_flags,
                  const vk::SampleCountFlagBits sample_count=vk::SampleCountFlagBits::e1,
                  const bool create_sampler=false,
                  const vk::SamplerCreateInfo sampler_create_info={},
                  const vk::ImageLayout layout={})
        :p_phy_dev_(p_phy_dev),
        p_dev_(p_dev),
        format(format)
    {
        create_image_(extent, usage, sample_count);
        allocate_and_bind_memory_();
        create_view_(format, aspect_flags);
        if (create_sampler) create_sampler_(sampler_create_info, layout);
    }
    ~Render_target()
    {
        if (sampler) p_dev_->dev.destroySampler(sampler);
        if (view) p_dev_->dev.destroyImageView(view);
        if (image) p_dev_->dev.destroyImage(image);
        if (mem) p_dev_->dev.freeMemory(mem);
    }

private:
    base::Physical_device* p_phy_dev_;
    base::Device* p_dev_;

    void create_image_(const vk::Extent2D extent, const vk::ImageUsageFlags& usage, const vk::SampleCountFlagBits& sample_count)
    {
        image=p_dev_->dev.createImage(
            vk::ImageCreateInfo({},
                                vk::ImageType::e2D,
                                format,
                                {extent.width, extent.height, 1},
                                1,
                                1,
                                sample_count,
                                vk::ImageTiling::eOptimal,
                                usage));
    }
    void create_view_(const vk::Format& format, const vk::ImageAspectFlags& aspect_flags)
    {
        view=p_dev_->dev.createImageView(
            vk::ImageViewCreateInfo({},
                                    image,
                                    vk::ImageViewType::e2D,
                                    format,
                                    vk::ComponentMapping(vk::ComponentSwizzle::eR,
                                                         vk::ComponentSwizzle::eG,
                                                         vk::ComponentSwizzle::eB,
                                                         vk::ComponentSwizzle::eA),
                                                         {aspect_flags, 0, 1, 0, 1}));
    }
    void allocate_and_bind_memory_()
    {
        vk::MemoryRequirements mem_reqs=p_dev_->dev.getImageMemoryRequirements(image);
        mem=p_dev_->dev.allocateMemory(
            vk::MemoryAllocateInfo(mem_reqs.size,
                                   p_phy_dev_->get_memory_type_index(mem_reqs.memoryTypeBits,
                                                                     vk::MemoryPropertyFlagBits::eDeviceLocal)));
        p_dev_->dev.bindImageMemory(image, mem, 0);
    }
    void create_sampler_(const vk::SamplerCreateInfo& sampler_create_info, const vk::ImageLayout& layout)
    {
        sampler=p_dev_->dev.createSampler(sampler_create_info);
        desc_image_info={sampler, view, layout};
    }
};