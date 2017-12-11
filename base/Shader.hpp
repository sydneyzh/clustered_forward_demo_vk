#pragma once
#include <vulkan/vulkan.hpp>
#include "Device.hpp"

namespace base
{
class Shader
{
public:
    Shader(Device* p_dev,
           const vk::ShaderStageFlagBits shader_stage_flag_bits)
        :p_dev_(p_dev),
        shader_stage_flag_bits_(shader_stage_flag_bits)
    {}

    ~Shader()
    {
        if (module_) p_dev_->dev.destroyShaderModule(module_);
    }

    void generate(const uint32_t code_size,
                  const uint32_t* code_ptr)
    {
        module_=p_dev_->dev.createShaderModule(
            vk::ShaderModuleCreateInfo(
        {},
                code_size, code_ptr));
    }

    vk::PipelineShaderStageCreateInfo create_pipeline_stage_info()
    {
        return {{},
            shader_stage_flag_bits_,
            module_,
            "main"};
    }

private:
    Device* p_dev_;
    vk::ShaderStageFlagBits shader_stage_flag_bits_;
    vk::ShaderModule module_;
};
} // namespace base