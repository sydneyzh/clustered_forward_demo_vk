#pragma once
#include <vulkan/vulkan.hpp>
#include <gli/gli.hpp>
#include <Physical_device.hpp>
#include <Device.hpp>
#include <tools.hpp>
#include <iostream>

#define MSG_PREFIX "-- TEXTURE: "

namespace base
{
class Texture
{
public:
    uint32_t width{0};
    uint32_t height{0};
    uint32_t mip_levels{0};
    uint32_t layer_count{0};

    vk::ImageLayout layout;
    vk::Image image;
    vk::DeviceMemory mem;
    vk::ImageView view;
    vk::Sampler sampler;
    vk::DescriptorImageInfo desc_image_info;

    Texture(Physical_device* p_phy_dev,
            Device* p_dev)
        :p_phy_dev_(p_phy_dev),
        p_dev_(p_dev)
    {}

    virtual ~Texture()
    {
        if (sampler) p_dev_->dev.destroySampler(sampler);
        if (view) p_dev_->dev.destroyImageView(view);
        if (image) p_dev_->dev.destroyImage(image);
        if (mem) p_dev_->dev.freeMemory(mem);
    }

    void update_desc_image_info()
    {
        desc_image_info.sampler=sampler;
        desc_image_info.imageView=view;
        desc_image_info.imageLayout=layout;
    }

protected:
    Physical_device* p_phy_dev_;
    Device* p_dev_;
};

class Texture2D : public Texture
{
public:
    Texture2D(Physical_device* p_phy_dev,
              Device* p_dev)
        :Texture(p_phy_dev, p_dev)
    {}

    void load(const std::string& full_path,
              const vk::CommandPool cmd_pool,
              const vk::Format format,
              const vk::ImageUsageFlags usage=vk::ImageUsageFlagBits::eSampled,
              const vk::ImageLayout layout=vk::ImageLayout::eShaderReadOnlyOptimal,
              const bool create_sampler=false)
    {

        this->layout=layout;

        if (!file_exists(full_path))
            throw std::runtime_error("file does not exist");

        auto tex2D=gli::texture2d(gli::load(full_path.c_str()));
        assert(!tex2D.empty());

        const auto gli_tex_format=static_cast<vk::Format>(tex2D.format());
        assert(format == gli_tex_format);

        width=static_cast<uint32_t>(tex2D[0].extent().x);
        height=static_cast<uint32_t>(tex2D[0].extent().y);
        mip_levels=static_cast<uint32_t>(tex2D.levels());

        std::cout << MSG_PREFIX << "image loaded, width: "
            << width << ", height: " << height
            << ", mip_levels: " << mip_levels << std::endl;

        // blit image from a staging buffer

        // copy_cmd
        std::vector<vk::CommandBuffer> copy_cmds=
            p_dev_->dev.allocateCommandBuffers(
                vk::CommandBufferAllocateInfo(
                    cmd_pool,
                    vk::CommandBufferLevel::ePrimary,
                    1));
        vk::CommandBuffer copy_cmd_buf=copy_cmds[0];
        copy_cmd_buf.begin(vk::CommandBufferBeginInfo());

        // staging_buffer
        vk::Buffer staging_buffer=p_dev_->dev.createBuffer(
            vk::BufferCreateInfo({},
                                 tex2D.size(),
                                 vk::BufferUsageFlagBits::eTransferSrc));
        vk::MemoryRequirements mem_reqs=
            p_dev_->dev.getBufferMemoryRequirements(staging_buffer);
        vk::DeviceMemory staging_mem=
            p_dev_->dev.allocateMemory(
                vk::MemoryAllocateInfo(
                    mem_reqs.size,
                    p_phy_dev_->get_memory_type_index(
                        mem_reqs.memoryTypeBits,
                        vk::MemoryPropertyFlagBits::eHostVisible |
                        vk::MemoryPropertyFlagBits::eHostCoherent)));
        p_dev_->dev.bindBufferMemory(staging_buffer, staging_mem, 0);
        uint8_t* data;
        p_dev_->dev.mapMemory(
            staging_mem,
            0,
            mem_reqs.size,
            vk::MemoryMapFlagBits(),
            (void**)&data);
        memcpy(data, tex2D.data(), tex2D.size());
        p_dev_->dev.unmapMemory(staging_mem);

        // setup buffer copy regions for each mip level
        std::vector<vk::BufferImageCopy> buf_image_copies;
        uint32_t offset=0;
        for (uint32_t i=0; i < mip_levels; ++i) {
            buf_image_copies.emplace_back(
                offset,
                0, 0,
                vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i, 0, 1),
                vk::Offset3D(0, 0, 0),
                vk::Extent3D(static_cast<uint32_t>(tex2D[i].extent().x),
                             static_cast<uint32_t>(tex2D[i].extent().y),
                             1));
            offset+=static_cast<uint32_t>(tex2D[i].size());
        }

        // create optimal tiling image
        image=p_dev_->dev.createImage(
            vk::ImageCreateInfo(
        {},
                vk::ImageType::e2D,
                format,
                vk::Extent3D(width, height, 1),
                mip_levels,
                1,
                vk::SampleCountFlagBits::e1,
                vk::ImageTiling::eOptimal,
                usage | vk::ImageUsageFlagBits::eTransferDst,
                vk::SharingMode::eExclusive,
                0,
                nullptr,
                vk::ImageLayout::eUndefined));

        // image mem
        mem_reqs=p_dev_->dev.getImageMemoryRequirements(image);
        mem=p_dev_->dev.allocateMemory(
            vk::MemoryAllocateInfo(
                mem_reqs.size,
                p_phy_dev_->get_memory_type_index(
                    mem_reqs.memoryTypeBits,
                    vk::MemoryPropertyFlagBits::eDeviceLocal)));
        p_dev_->dev.bindImageMemory(image, mem, 0);

        vk::ImageSubresourceRange range(
            vk::ImageAspectFlagBits::eColor,
            0,
            mip_levels,
            0,
            1);

        // change image layout
        vk::ImageMemoryBarrier imb=vk::ImageMemoryBarrier(
        {},
            vk::AccessFlagBits::eTransferWrite,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal,
            0,
            0,
            image,
            range);
        copy_cmd_buf.pipelineBarrier(
            vk::PipelineStageFlagBits::eAllCommands,
            vk::PipelineStageFlagBits::eAllCommands,
            {},
            0, nullptr,
            0, nullptr,
            1, &imb);

        // copy staging buffer to image
        copy_cmd_buf.copyBufferToImage(
            staging_buffer,
            image,
            vk::ImageLayout::eTransferDstOptimal,
            static_cast<uint32_t>(buf_image_copies.size()),
            buf_image_copies.data());

        // change image layout
        imb.oldLayout=vk::ImageLayout::eTransferDstOptimal;
        imb.newLayout=layout;
        imb.srcAccessMask=vk::AccessFlagBits::eTransferWrite;
        imb.dstAccessMask=vk::AccessFlagBits::eShaderRead;
        copy_cmd_buf.pipelineBarrier(
            vk::PipelineStageFlagBits::eAllCommands,
            vk::PipelineStageFlagBits::eAllCommands,
            {},
            0, nullptr,
            0, nullptr,
            1, &imb);

        // flush copy command buffer
        copy_cmd_buf.end();
        vk::SubmitInfo si(0, nullptr, nullptr, 1, &copy_cmd_buf, 0, nullptr);
        p_dev_->graphics_queue.submit(1, &si, nullptr);
        p_dev_->graphics_queue.waitIdle();
        // free copy command buffer
        p_dev_->dev.freeCommandBuffers(cmd_pool, 1, &copy_cmd_buf);

        // cleanup
        p_dev_->dev.freeMemory(staging_mem);
        p_dev_->dev.destroyBuffer(staging_buffer);

        std::cout << MSG_PREFIX << "image created" << std::endl;

        create_image_view(format);

        if (create_sampler) {
            create_default_sampler();
            update_desc_image_info();
        }
    }

private:
    void create_image_view(const vk::Format format)
    {
        view=p_dev_->dev.createImageView(
            vk::ImageViewCreateInfo(
        {},
                image,
                vk::ImageViewType::e2D,
                format,
                vk::ComponentMapping(
                    vk::ComponentSwizzle::eR,
                    vk::ComponentSwizzle::eG,
                    vk::ComponentSwizzle::eB,
                    vk::ComponentSwizzle::eA),
                vk::ImageSubresourceRange(
                    vk::ImageAspectFlagBits::eColor,
                    0,
                    mip_levels,
                    0,
                    1)));

        std::cout << MSG_PREFIX << "image view created" << std::endl;
    }
    void create_default_sampler()
    {
        sampler=p_dev_->dev.createSampler(
            vk::SamplerCreateInfo(
        {},
                vk::Filter::eLinear,
                vk::Filter::eLinear,
                vk::SamplerMipmapMode::eLinear,
                vk::SamplerAddressMode::eClampToEdge,
                vk::SamplerAddressMode::eClampToEdge,
                vk::SamplerAddressMode::eClampToEdge,
                0,
                0,
                1.f,
                0,
                vk::CompareOp::eNever,
                0.f,
                mip_levels,
                vk::BorderColor::eFloatOpaqueWhite));

        std::cout << MSG_PREFIX << "default sampler created" << std::endl;
    }

};
} // namespace base
#undef MSG_PREFIX