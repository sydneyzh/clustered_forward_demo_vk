#pragma once
#include <vulkan/vulkan.hpp>
#include "assert.hpp"
#include "Timer.hpp"
#include "Physical_device.hpp"
#include "Device.hpp"
#include <iostream>
#include <sstream>
#define DEBUG_REPORT_VERBOSE false
#define MSG_PREFIX "-- PROGRAM_BASE: "

namespace base
{
VKAPI_ATTR VkBool32 VKAPI_CALL
debug_report_callback(VkDebugReportFlagsEXT msg_flags,
                      VkDebugReportObjectTypeEXT obj_type,
                      uint64_t object,
                      size_t location,
                      int32_t msg_code,
                      const char *layer_prefix,
                      const char *msg,
                      void *pUserData)
{
    std::string msg_prefix;
    uint32_t priority;
    if (msg_flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
        msg_prefix="VK_DEBUG_REPORT [ERROR] ";
        priority=0;
    }
    else if (msg_flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
        msg_prefix="VK_DEBUG_REPORT [WARNING] ";
        priority=1;
    }
    else if (msg_flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
        msg_prefix="VK_DEBUG_REPORT [INFORMATION] ";
        priority=2;
    }
    else if (msg_flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) {
        msg_prefix="VK_DEBUG_REPORT [PERFORMANCE] ";
        priority=3;
    }
    else if (msg_flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
        msg_prefix="VK_DEBUG_REPORT [DEBUG] ";
        priority=4;
    }
    else {
        return VK_FALSE;
    }
    std::ostream &st=priority == 0 ? std::cerr : std::cout;
    st << msg_prefix << "[Layer " << layer_prefix << "] [Code " << msg_code << "] " << msg << "\n";
    return priority == 0 ? VK_TRUE : VK_FALSE;
}

class Program_base
{
public:
    Program_base(Prog_info_base *p_info,
                 Shell_base *p_shell,
                 bool enable_validation)
        : p_info_(p_info),
        p_shell_(p_shell),
        enable_validation_(enable_validation)
    {}

    virtual ~Program_base()
    {
        p_dev_->dev.waitIdle();

        delete p_dev_;
        delete p_phy_dev_;

        instance_.destroySurfaceKHR(surface_);
        p_shell_->destroy_window();

        if (enable_validation_) destroy_debug_report_callback_();
        instance_.destroy();

        // close xcb connection...
    }

    virtual void init()
    {
        req_inst_extensions_.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#ifdef VK_USE_PLATFORM_WIN32_KHR
        req_inst_extensions_.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#else
#error "uninplemented platform"
#endif
        req_device_extensions_.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

        if (enable_validation_) {
            req_inst_layers_.push_back("VK_LAYER_LUNARG_standard_validation");
            req_inst_extensions_.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
        }

        if (!check_instance_layer_support_()) {
            std::string errstr=MSG_PREFIX;
            errstr.append("missing instance layer support");
            throw std::runtime_error(errstr);
        }

        init_vk_();
        init_debug_report_();

        // init xcb connection...

        p_phy_dev_=new Physical_device(&instance_,
                                       p_shell_,
                                       req_phy_dev_features_,
                                       req_device_extensions_);
        p_dev_=new Device(p_phy_dev_);

        p_shell_->init_window();
        init_surface_();
    }

    void run()
    {
#ifdef VK_USE_PLATFORM_WIN32_KHR
        Timer timer;
        double prev_time=timer.get();

        while (true) {
            bool quit=false;
            MSG msg{};
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    quit=true;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            if (quit) break;

            acquire_back_buffer_();

            double curr_time=timer.get();
            prev_time=curr_time;

            present_back_buffer_(curr_time);
        }
#else
#error "uninplemented platform"
#endif
    }

protected:
    Prog_info_base *p_info_;
    Shell_base *p_shell_;
    bool enable_validation_;

    std::vector<const char *> req_inst_layers_{};
    std::vector<const char *> req_inst_extensions_{};
    vk::PhysicalDeviceFeatures req_phy_dev_features_{};
    std::vector<const char *> req_device_extensions_{};

    vk::Instance instance_;
    VkDebugReportCallbackEXT debug_report_=VK_NULL_HANDLE;

    Physical_device *p_phy_dev_=nullptr;
    Device *p_dev_=nullptr;

    vk::SurfaceKHR surface_;
    vk::SurfaceFormatKHR surface_format_{}; // color format may differ from preferred_color_format

    bool check_instance_layer_support_()
    {
        uint32_t layer_count;
        vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
        std::vector<VkLayerProperties> available_layers(layer_count);
        vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());
        for (const char *layer_name : req_inst_layers_) {
            bool found=false;
            for (const auto &layer_props : available_layers) {
                if (strcmp(layer_name, layer_props.layerName) == 0) {
                    found=true;
                    break;
                }
            }
            if (!found) {
                std::cout << (MSG_PREFIX) << layer_name << "not supported" << std::endl;
                return false;
            }
        }
        return true;
    }

    void init_vk_()
    {
        vk::ApplicationInfo app_info(p_info_->prog_name().c_str(),
                                     1,
                                     p_info_->prog_name().c_str(),
                                     1,
                                     VK_API_VERSION_1_0);

        vk::InstanceCreateInfo inst_info({},
                                         &app_info,
                                         static_cast<uint32_t>(req_inst_layers_.size()),
                                         req_inst_layers_.data(),
                                         static_cast<uint32_t>(req_inst_extensions_.size()),
                                         req_inst_extensions_.data());

        instance_=vk::createInstance(inst_info);
        std::cout << (MSG_PREFIX) << "Vulkan instance created" << std::endl;
    }

    void init_debug_report_()
    {
        if (enable_validation_) {
            VkDebugReportCallbackCreateInfoEXT debug_report_info={};
            debug_report_info.sType=VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
            debug_report_info.flags=VK_DEBUG_REPORT_WARNING_BIT_EXT |
                VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
                VK_DEBUG_REPORT_ERROR_BIT_EXT;
            if (DEBUG_REPORT_VERBOSE) {
                debug_report_info.flags=VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;
            }
            debug_report_info.pfnCallback=debug_report_callback;
            debug_report_info.pUserData=nullptr;
            assert_success(create_debug_report_callback_(&debug_report_info));
            std::cout << (MSG_PREFIX) << "debug report created" << std::endl;
        }
    }

    VkResult create_debug_report_callback_(VkDebugReportCallbackCreateInfoEXT *p_create_info)
    {
        auto func=(PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(VkInstance(instance_),
                                                                            "vkCreateDebugReportCallbackEXT");
        if (func != nullptr) {
            return func(VkInstance(instance_), p_create_info, nullptr, &debug_report_);
        }
        else {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }

    void destroy_debug_report_callback_()
    {
        auto func=(PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(VkInstance(instance_),
                                                                             "vkDestroyDebugReportCallbackEXT");
        if (func != nullptr) {
            func(VkInstance(instance_), debug_report_, nullptr);
        }
    }

    void init_surface_()
    {
#ifdef VK_USE_PLATFORM_WIN32_KHR
        vk::Win32SurfaceCreateInfoKHR surface_info({}, p_shell_->hinstance, p_shell_->hwnd);
        surface_=instance_.createWin32SurfaceKHR(surface_info);
        std::cout << (MSG_PREFIX) << "Win32 surface created" << std::endl;
#else
#error "uninplemented platform"
#endif
        VkBool32 supported;
        p_phy_dev_->phy_dev.getSurfaceSupportKHR(p_phy_dev_->present_queue_family_idx, surface_, &supported);
        assert(supported);

        // choose suitable surface format
        std::vector<vk::SurfaceFormatKHR> surface_formats=p_phy_dev_->phy_dev.getSurfaceFormatsKHR(surface_);
        assert(!surface_formats.empty());
        if ((surface_formats.size() == 1) && (surface_formats[0].format == vk::Format::eUndefined)) {
            surface_format_={vk::Format::eR8G8B8A8Unorm, surface_formats[0].colorSpace};
        }
        else {
            surface_format_=surface_formats[0];
        }
    }

    virtual void acquire_back_buffer_()=0;
    virtual void present_back_buffer_(float elapsed_time)=0;
};
} // namespace base

#undef MSG_PREFIX
