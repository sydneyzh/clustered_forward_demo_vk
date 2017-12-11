#pragma once
#include <vulkan/vulkan.hpp>
#include <sstream>

namespace base
{
const char* get_error_str(VkResult res)
{
    switch (res) {
#define STR(r) case r: return #r
        STR(VK_NOT_READY);
        STR(VK_TIMEOUT);
        STR(VK_EVENT_SET);
        STR(VK_EVENT_RESET);
        STR(VK_INCOMPLETE);
        STR(VK_ERROR_OUT_OF_HOST_MEMORY);
        STR(VK_ERROR_OUT_OF_DEVICE_MEMORY);
        STR(VK_ERROR_INITIALIZATION_FAILED);
        STR(VK_ERROR_DEVICE_LOST);
        STR(VK_ERROR_MEMORY_MAP_FAILED);
        STR(VK_ERROR_LAYER_NOT_PRESENT);
        STR(VK_ERROR_EXTENSION_NOT_PRESENT);
        STR(VK_ERROR_FEATURE_NOT_PRESENT);
        STR(VK_ERROR_INCOMPATIBLE_DRIVER);
        STR(VK_ERROR_TOO_MANY_OBJECTS);
        STR(VK_ERROR_FORMAT_NOT_SUPPORTED);
        //    STR(VK_ERROR_FRAGMENTED_POOL);
        STR(VK_ERROR_SURFACE_LOST_KHR);
        STR(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR);
        STR(VK_SUBOPTIMAL_KHR);
        STR(VK_ERROR_OUT_OF_DATE_KHR);
        STR(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR);
        STR(VK_ERROR_VALIDATION_FAILED_EXT);
        STR(VK_ERROR_INVALID_SHADER_NV);
        STR(VK_ERROR_OUT_OF_POOL_MEMORY_KHR);
        STR(VK_ERROR_INVALID_EXTERNAL_HANDLE_KHR);
        STR(VK_RESULT_BEGIN_RANGE);
        //    STR(VK_RESULT_END_RANGE);
        STR(VK_RESULT_RANGE_SIZE);
        STR(VK_RESULT_MAX_ENUM);
#undef STR
        default:return "UNKNOWN_ERROR";
    }
}

void assert_success(VkResult res)
{
    if (res == VK_SUCCESS) return;
    std::stringstream ss;
    ss << "VKResult " << get_error_str(res);
    throw std::runtime_error(ss.str());
}

void assert_success(vk::Result res)
{
    if (res == vk::Result::eSuccess) return;
    std::stringstream ss;
    ss << "VKResult " << to_string(res);
    throw std::runtime_error(ss.str());
}
} // namespace base