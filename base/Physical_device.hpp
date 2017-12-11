#pragma once
#include <vulkan/vulkan.hpp>
#include "Shell_base.hpp"
#include <set>
#include <iostream>
#define MSG_PREFIX "-- PHYSICAL_DEVICE: "

namespace base
{
class Physical_device
{
public:
    vk::PhysicalDevice phy_dev;

    vk::PhysicalDeviceFeatures req_features;
    std::vector<const char*> req_extensions;

    uint32_t graphics_queue_family_idx;
    uint32_t compute_queue_family_idx;
    uint32_t present_queue_family_idx;

    vk::PhysicalDeviceMemoryProperties mem_props;
    vk::PhysicalDeviceProperties props;

    Physical_device(vk::Instance* p_instance,
                    base::Shell_base* p_shell,
                    vk::PhysicalDeviceFeatures& req_features,
                    std::vector<const char*>& req_extensions)
        :p_instance_(p_instance),
        p_shell_(p_shell),
        req_features(req_features),
        req_extensions(req_extensions)
    {

        std::vector<vk::PhysicalDevice> phy_devs=p_instance->enumeratePhysicalDevices();
        for (auto pd : phy_devs) {
            bool has_all_device_extensions=true;
            std::vector<vk::ExtensionProperties> ext_props=pd.enumerateDeviceExtensionProperties();

            std::set<std::string> ext_names;
            for (const auto& ext_prop : ext_props) {
                ext_names.insert(static_cast<std::string>(ext_prop.extensionName));
            }
            for (const auto& ext_name : req_extensions) {
                if (ext_names.find(ext_name) == ext_names.end()) {
                    has_all_device_extensions=false;
                    break;
                }
            }
            if (!has_all_device_extensions) continue;

            // get queue properties
            std::vector<vk::QueueFamilyProperties> queue_family_props=pd.getQueueFamilyProperties();
            // check graphics, present queues
            int gqf=-1, cqf=-1, pqf=-1;
            for (uint32_t i=0; i < queue_family_props.size(); i++) {
                const vk::QueueFamilyProperties& props=queue_family_props[i];
                // graphics
                const vk::QueueFlags graphics_queue_flags(vk::QueueFlagBits::eGraphics);
                // compute
                const vk::QueueFlags compute_queue_flags(vk::QueueFlagBits::eCompute);
                if (gqf < 0 &&
                    (props.queueFlags & graphics_queue_flags) == graphics_queue_flags)
                    gqf=i;
                if (cqf < 0 &&
                    (props.queueFlags & compute_queue_flags) == compute_queue_flags)
                    cqf=i;
                // present queue
                if (pqf < 0 && (bool)p_shell->can_present(pd, i))
                    pqf=i;
                if (gqf >= 0 && cqf >= 0 && pqf >= 0) break;
            }
            if (gqf >= 0 && cqf >= 0 && pqf >= 0) {
                phy_dev=pd;
                graphics_queue_family_idx=(uint32_t)gqf;
                compute_queue_family_idx=(uint32_t)cqf;
                present_queue_family_idx=(uint32_t)pqf;
                break;
            }
        }
        if (!phy_dev) {
            std::string errstr=MSG_PREFIX;
            errstr.append("failed to find any capable vulkan physical device");
            throw std::runtime_error(errstr);
        }

        mem_props=phy_dev.getMemoryProperties();
        props=phy_dev.getProperties();

        if (!check_req_features_support_()) {
            std::string errstr=MSG_PREFIX;
            errstr.append("missing physical device features support");
            throw std::runtime_error(errstr);
        }
    }

    ~Physical_device()=default;

    uint32_t get_memory_type_index(uint32_t type_bits,
                                   const vk::MemoryPropertyFlags& property_flags)
    {
        for (uint32_t i=0; i < mem_props.memoryTypeCount; i++) {
            if ((type_bits & 1) == 1) {
                if ((mem_props.memoryTypes[i].propertyFlags & property_flags) == property_flags) {
                    return i;
                }
            }
            type_bits>>=1;
        }
        throw std::runtime_error("cannot find a suitable memory type");
    }

    //    void ensure_depth_format_support(vk::Format &depth_format) {
    //        // only for onscreen rp depth attachment
    //        bool supported = false;
    //        if (depth_format) {
    //            vk::FormatProperties format_props = phy_dev.getFormatProperties(depth_format);
    //            if (format_props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
    //                supported = true;
    //            }
    //        }
    //
    //        if (!supported) {
    //            std::vector<vk::Format> depth_formats = {
    //                vk::Format::eD32SfloatS8Uint,
    //                vk::Format::eD32Sfloat,
    //                vk::Format::eD24UnormS8Uint,
    //                vk::Format::eD16UnormS8Uint,
    //                vk::Format::eD16Unorm
    //            };
    //            bool found = false;
    //            for (auto &df: depth_formats) {
    //                vk::FormatProperties fp = phy_dev.getFormatProperties(depth_format);
    //                if (fp.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
    //                    found = true;
    //                    depth_format = df;
    //                }
    //            }
    //            if (!found) {
    //                std::string errstr = MSG_PREFIX;
    //                errstr.append("cannot find a depth format that supports depth stencil attachment for optimal tiling");
    //                throw std::runtime_error(errstr);
    //            }
    //        }
    //    }

private:
    vk::Instance* p_instance_;
    Shell_base* p_shell_;

    bool check_req_features_support_()
    {
        auto req=static_cast<VkPhysicalDeviceFeatures>(req_features);
        auto req_ptr=reinterpret_cast<VkBool32*>(&req);
        auto available_features=static_cast<VkPhysicalDeviceFeatures>(phy_dev.getFeatures());
        auto avail_ptr=reinterpret_cast<VkBool32*>(&available_features);
        auto len=sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);
        for (auto i=0; i < len; i++) {
            if (req_ptr[i] == VK_TRUE && avail_ptr[i] == VK_FALSE) {
                std::cout << (MSG_PREFIX) << "physical device feature #" << i << " requested but not supported"
                    << std::endl;
                return false;
            }
        }
        return true;
    }
};
} // namespace base

#undef MSG_PREFIX