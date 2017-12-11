#pragma once
#include <vulkan/vulkan.hpp>
#include "Physical_device.hpp"

namespace base
{
class Device
{
public:
    vk::Device dev;

    vk::Queue graphics_queue;
    vk::Queue compute_queue;
    vk::Queue present_queue;

    explicit Device(Physical_device* p_phy_dev)
        :p_phy_dev_(p_phy_dev)
    {

        uint32_t queue_count=1;
        const std::vector<float> queue_priorities(queue_count, 0.f);
        std::vector<vk::DeviceQueueCreateInfo> dev_queue_infos;
        // graphics queue
        dev_queue_infos.push_back({{},
                                  p_phy_dev->graphics_queue_family_idx,
                                  1,
                                  queue_priorities.data()});
        // graphics, compute, presant queues may or may not be the same
        if (p_phy_dev->graphics_queue_family_idx != p_phy_dev->compute_queue_family_idx) {
            dev_queue_infos.push_back({{},
                                      p_phy_dev->compute_queue_family_idx,
                                      1,
                                      queue_priorities.data()});
        }
        if (p_phy_dev->graphics_queue_family_idx != p_phy_dev->present_queue_family_idx) {
            dev_queue_infos.push_back({{},
                                      p_phy_dev->present_queue_family_idx,
                                      1,
                                      queue_priorities.data()});
            queue_count++;
        }

        dev=p_phy_dev->phy_dev.createDevice(
            vk::DeviceCreateInfo({},
                                 queue_count,
                                 dev_queue_infos.data(),
                                 0,
                                 nullptr,
                                 static_cast<uint32_t>(p_phy_dev->req_extensions.size()),
                                 p_phy_dev->req_extensions.data(),
                                 &p_phy_dev->req_features));
        graphics_queue=dev.getQueue(p_phy_dev->graphics_queue_family_idx, 0);
        compute_queue=dev.getQueue(p_phy_dev->compute_queue_family_idx, 0);
        present_queue=dev.getQueue(p_phy_dev->present_queue_family_idx, 0);
    }

    ~Device()
    {
        graphics_queue=nullptr;
        compute_queue=nullptr;
        present_queue=nullptr;
        dev.waitIdle();
        dev.destroy();
    }

    vk::CommandPool create_graphics_command_pool(const vk::CommandPoolCreateFlags& create_flags)
    {
        return dev.createCommandPool(vk::CommandPoolCreateInfo(create_flags,
                                                               p_phy_dev_->graphics_queue_family_idx));
    }

    vk::CommandPool create_compute_command_pool(const vk::CommandPoolCreateFlags& create_flags)
    {
        return dev.createCommandPool(vk::CommandPoolCreateInfo(create_flags,
                                                               p_phy_dev_->compute_queue_family_idx));
    }

private:
    Physical_device* p_phy_dev_;

};
} // namespace base
