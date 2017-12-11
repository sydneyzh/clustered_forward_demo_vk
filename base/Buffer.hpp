#pragma once
#include <vulkan/vulkan.hpp>
#include "Physical_device.hpp"
#include "Device.hpp"
#include "assert.hpp"
#define MSG_PREFIX "-- BUFFER: "

namespace base
{
void align_size(vk::DeviceSize& size,
                const vk::DeviceSize& alignment_size)
{
    if (size%alignment_size != 0) {
        size+=alignment_size - (size%alignment_size);
    }
}

class Buffer
{
public:
    vk::BufferUsageFlags usage;
    vk::DeviceSize size;
    vk::Buffer buf;

    vk::MemoryPropertyFlags mem_prop_flags;
    vk::MemoryRequirements mem_reqs{VK_NULL_HANDLE};
    vk::DeviceSize allocation_size{0};
    void* mapped{nullptr};

    vk::Format view_format;
    vk::BufferView view;

    vk::DescriptorBufferInfo desc_buf_info;

    Buffer(Device* p_dev,
           const vk::BufferUsageFlags& usage,
           const vk::MemoryPropertyFlags& mem_prop_flags,
           const vk::DeviceSize& size,
           const vk::SharingMode& sharing_mode=vk::SharingMode::eExclusive,
           const uint32_t queue_family_count=0,
           const uint32_t* p_queue_family_idx=nullptr)
        : p_dev_(p_dev),
        usage(usage),
        mem_prop_flags(mem_prop_flags),
        size(size)
    {

        buf=p_dev->dev.createBuffer(
            vk::BufferCreateInfo({},
                                 size,
                                 usage,
                                 sharing_mode,
                                 queue_family_count,
                                 p_queue_family_idx));
        mem_reqs=p_dev->dev.getBufferMemoryRequirements(buf);
        allocation_size=mem_reqs.size;
        align_size(allocation_size, mem_reqs.alignment);
    }

    ~Buffer()
    {
        if (view) p_dev_->dev.destroyBufferView(view);
        if (buf) p_dev_->dev.destroyBuffer(buf);
    }

    void create_view(vk::Format format,
                     vk::DeviceSize offset=0,
                     vk::DeviceSize range=VK_WHOLE_SIZE)
    {
        assert(((usage & vk::BufferUsageFlagBits::eUniformTexelBuffer) == vk::BufferUsageFlagBits::eUniformTexelBuffer) ||
            ((usage & vk::BufferUsageFlagBits::eStorageTexelBuffer) == vk::BufferUsageFlagBits::eStorageTexelBuffer));
        view_format=format;
        view=p_dev_->dev.createBufferView(
            vk::BufferViewCreateInfo({},
                                     buf,
                                     format,
                                     0,
                                     VK_WHOLE_SIZE));
    }

    void update_desc_buf_info(vk::DeviceSize offset, vk::DeviceSize range)
    {
        desc_buf_info=vk::DescriptorBufferInfo(buf, offset, range);
    }

private:
    Device* p_dev_;
};

// allocate and bind memory for multiple buffers
// retrieve the pointers if mappable
void allocate_and_bind_buffer_memory(Physical_device* p_phy_dev,
                                     Device* p_dev,
                                     vk::DeviceMemory& mem,
                                     uint32_t buf_count,
                                     Buffer** p_bufs,
                                     uint32_t override_alignment=0)
{
    assert(buf_count > 0);

    uint32_t type_bits=p_bufs[0]->mem_reqs.memoryTypeBits;
    vk::MemoryPropertyFlags mem_prop_flags=p_bufs[0]->mem_prop_flags;
    vk::DeviceSize total_allocation_size=0;

    for (uint32_t i=0; i < buf_count; i++) {
        auto* p_buf=p_bufs[i];
        // should have the same memory property flags
        assert(p_buf->mem_prop_flags == mem_prop_flags);

        // should have the same memory type bits
        assert(type_bits == p_buf->mem_reqs.memoryTypeBits);

        // allocation size
        if (override_alignment > 0) {
            total_allocation_size+=((p_buf->allocation_size - 1) / override_alignment + 1) * override_alignment;
        }
        else {
            total_allocation_size+=p_buf->allocation_size;
        }
    }

    std::cout << MSG_PREFIX << "allocated memory for " << buf_count << " buffers, " <<
        "total allocation size: " << total_allocation_size << std::endl;

    mem=p_dev->dev.allocateMemory(
        vk::MemoryAllocateInfo(
            total_allocation_size,
            p_phy_dev->get_memory_type_index(type_bits, mem_prop_flags)));

    void* ptr=nullptr;
    bool mapped_flag=false;

    // only host visible memory is mappable
    if ((mem_prop_flags & vk::MemoryPropertyFlagBits::eHostVisible) ==
        vk::MemoryPropertyFlagBits::eHostVisible) {
        ptr=p_dev->dev.mapMemory(mem, 0, VK_WHOLE_SIZE, {});
        mapped_flag=true;
    }

    vk::DeviceSize offset=0;
    for (uint32_t i=0; i < buf_count; i++) {
        auto* p_buf=p_bufs[i];
        p_dev->dev.bindBufferMemory(p_buf->buf,
                                    mem,
                                    offset);
        if (mapped_flag) {
            p_buf->mapped=reinterpret_cast<uint8_t*>(ptr) + offset;
        }

        if (override_alignment > 0) {
            offset+=((p_buf->allocation_size - 1) / override_alignment + 1) * override_alignment;
        }
        else {
            offset+=p_buf->allocation_size;
        }
    }

    // this function does not unmap memory if mapped
}

static void update_host_visible_buffer_memory(
    Device* p_dev,
    Buffer* p_buffer,
    vk::DeviceMemory& mem, // mapped
    vk::DeviceSize data_size,
    void* data,
    bool unmap=false)
{

    auto host_visible_mem_flag=vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent;
    if ((p_buffer->mem_prop_flags & host_visible_mem_flag) != host_visible_mem_flag) {
        throw std::runtime_error("wrong type of buffer memory update function");
    }

    assert(p_buffer->mapped);
    memcpy(p_buffer->mapped, reinterpret_cast<uint8_t*>(data), data_size);
    if (unmap) p_dev->dev.unmapMemory(mem);
}

static void update_device_local_buffer_memory(
    Physical_device* p_phy_dev,
    Device* p_dev,
    Buffer* p_buffer,
    vk::DeviceMemory& mem,
    vk::DeviceSize data_size,
    void* data,
    const vk::DeviceSize offset=0,
    const vk::PipelineStageFlags generating_stages=vk::PipelineStageFlags(),
    const vk::PipelineStageFlags consuming_stages=vk::PipelineStageFlags(),
    const vk::AccessFlags curr_access=vk::AccessFlags(),
    const vk::AccessFlags new_access=vk::AccessFlags(),
    const vk::CommandBuffer& cmd_buf=nullptr)
{
    if ((p_buffer->mem_prop_flags & vk::MemoryPropertyFlagBits::eDeviceLocal) !=
        vk::MemoryPropertyFlagBits::eDeviceLocal) {
        throw std::runtime_error("wrong buffer memory update function");
    }

    // use staging buffer
    auto p_staging_buf=new Buffer(p_dev,
                                  vk::BufferUsageFlagBits::eTransferSrc,
                                  vk::MemoryPropertyFlagBits::eHostVisible |
                                  vk::MemoryPropertyFlagBits::eHostCoherent,
                                  data_size);

    // allocate and bind memory
    vk::DeviceMemory staging_mem;
    allocate_and_bind_buffer_memory(p_phy_dev,
                                    p_dev,
                                    staging_mem,
                                    1, &p_staging_buf);

    // write staging buffer with data
    update_host_visible_buffer_memory(p_dev,
                                      p_staging_buf,
                                      staging_mem,
                                      data_size, data);

    // begin copy cmd buf
    cmd_buf.begin(vk::CommandBufferBeginInfo(
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    // change buf layout to transfer write
    vk::BufferMemoryBarrier barrier(curr_access,
                                    vk::AccessFlagBits::eTransferWrite,
                                    0, 0,
                                    p_buffer->buf,
                                    0, VK_WHOLE_SIZE);
    cmd_buf.pipelineBarrier(generating_stages,
                            vk::PipelineStageFlagBits::eTransfer,
                            vk::DependencyFlags(),
                            0, nullptr,
                            1, &barrier,
                            0, nullptr);

    // copy data from staging buf to buf
    vk::BufferCopy region(0, offset, p_buffer->size);
    cmd_buf.copyBuffer(p_staging_buf->buf, p_buffer->buf, 1, &region);

    // change buf layout to final
    barrier=vk::BufferMemoryBarrier(
        vk::AccessFlagBits::eTransferWrite,
        new_access,
        0, 0,
        p_buffer->buf,
        0, VK_WHOLE_SIZE);
    cmd_buf.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        consuming_stages,
        vk::DependencyFlags(),
        0, nullptr,
        1, &barrier,
        0, nullptr);

    cmd_buf.end();

    // submit and wait until done
    const auto fence=p_dev->dev.createFence(
        vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
    p_dev->dev.resetFences(1, &fence);
    p_dev->graphics_queue.submit(
        vk::SubmitInfo(0, nullptr,
                       nullptr,
                       1, &cmd_buf,
                       0, nullptr),
        fence);
    assert_success(p_dev->dev.waitForFences(1, &fence, VK_TRUE, UINT64_MAX));

    // cleanup
    p_dev->dev.destroyFence(fence);
    delete p_staging_buf;
    p_dev->dev.freeMemory(staging_mem);
}
} // namespace base
#undef MSG_PREFIX