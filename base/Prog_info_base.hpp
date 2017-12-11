#pragma once
#include <vulkan/vulkan.hpp>
#include <string>

namespace base
{
class Prog_info_base
{
public:
    bool resize_flag{false};
    //    bool quit{false};

    virtual uint32_t width() const=0;
    virtual uint32_t height() const=0;
    virtual const std::string& prog_name() const=0;

    virtual void on_resize(uint32_t width, uint32_t height)=0;

protected:
};
} // namespace base
