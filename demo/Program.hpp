#pragma once
#include <Program_base.hpp>
#include <Render_pass.hpp>
#include <Buffer.hpp>
#include <Camera.hpp>
#include <random.hpp>
#include <color.hpp>
#include <Aabb.hpp>
#include <Shader.hpp>
#include <Render_target.hpp>

#include "Light.hpp"
#include "Model.hpp"
#include "Swapchain.hpp"
#include "Shell.hpp"
#include "Text_overlay.hpp"

#include <queue>
#include <glm/gtc/type_ptr.hpp>

#include "simple.vert.h"
#include "clustering.vert.h"
#include "clustering.frag.h"
#include "calc_light_grids.comp.h"
#include "calc_grid_offsets.comp.h"
#include "calc_light_list.comp.h"

#include "cluster_forward.vert.h"
#include "cluster_forward.frag.h"
#include "light_particles.vert.h"
#include "light_particles.frag.h"

// return in millsec
inline std::string timestamp_to_str(uint32_t data)
{
    std::stringstream ss;
    ss << std::fixed << std::setprecision(4) << (static_cast<float>(data) / 1000000.f);
    return ss.str();
}

class Program : public base::Program_base
{
public:
    Program(Prog_info *p_info,
            Shell *p_shell,
            const bool enable_validation,
            base::Camera *p_camera)
        : Program_base(p_info, p_shell, enable_validation),
        p_info_(p_info),
        p_shell_(p_shell),
        p_camera_(p_camera)
    {

        p_camera_->update_aspect(p_info->width(), p_info->height());
        req_phy_dev_features_.shaderStorageImageExtendedFormats=VK_TRUE;
        req_phy_dev_features_.textureCompressionBC=VK_TRUE;
    }

    ~Program() override
    {
        p_dev_->dev.waitIdle();
        destroy_pipelines_();
        destroy_swapchain_();
        destroy_offscreen_framebuffer_();
        destroy_render_passes_();
        destroy_shaders_();
        destroy_descriptors_();
        destroy_frame_data_();
        destroy_text_overlay_();
        destroy_lights_();
        destroy_model_();
        destroy_command_pools_();
        destroy_back_buffers_();
        destroy_texel_buffers_();
    }

    void init() override
    {
        base::Program_base::init();
        init_texel_buffers_();
        init_back_buffers_();
        init_command_pools_();
        init_model_();
        init_lights_();
        init_text_overlay_();
        init_frame_data_();
        init_render_passes_();
        init_offscreen_framebuffer_();
        init_descriptors_();
        init_shaders_();
        init_swapchain_();
        init_pipelines_();
    }

    void generate_lights()
    {
        assert(p_model_);
        const base::Aabb &aabb=p_model_->get_aabb();
        lights_.clear();
        const float light_vol=aabb.get_volume() / static_cast<float>(p_info_->num_lights);
        const float base_range=powf(light_vol, 1.f / 3.f);
        const float max_range=base_range * 3.f;
        const float min_range=base_range / 1.5f;
        const glm::vec3 half_size=aabb.get_half_size();
        const float pos_radius=std::max(half_size.x, std::max(half_size.y, half_size.z));
        glm::vec3 fcol;
        glm::u8vec4 col;
        for (auto i=0; i < p_info_->num_lights; ++i) {
            float range=base::random_range(min_range, max_range);
            base::hue_to_rgb(fcol, base::random_range(0.f, 1.f));
            fcol*=1.3f;
            fcol-=0.15f;
            base::float_to_rgbunorm(col, fcol);
            glm::vec3 pos{base::random_range(-pos_radius, pos_radius),
                base::random_range(-pos_radius, pos_radius),
                base::random_range(-pos_radius, pos_radius)};
            lights_.emplace_back(pos, col, range);
        }
    }

private:
    Prog_info * p_info_{nullptr};
    Shell *p_shell_{nullptr};
    base::Camera *p_camera_{nullptr};
    vk::Format depth_format_{vk::Format::eD16Unorm};

    // ************************************************************************
    // texel buffers
    // ************************************************************************

    class Texel_buffer
    {
    public:
        base::Buffer *p_buf{nullptr};

        Texel_buffer(base::Physical_device *p_phy_dev,
                     base::Device *p_dev,
                     const vk::MemoryPropertyFlags mem_prop_flags,
                     const vk::DeviceSize size,
                     const vk::SharingMode sharing_mode,
                     const uint32_t queue_family_count,
                     const uint32_t *p_queue_family,
                     const vk::Format view_format)
            : p_dev_(p_dev)
        {
            p_buf=new base::Buffer(p_dev_,
                                   usage_,
                                   mem_prop_flags,
                                   size,
                                   sharing_mode,
                                   queue_family_count,
                                   p_queue_family);
            base::allocate_and_bind_buffer_memory(p_phy_dev, p_dev, mem_, 1, &p_buf);
            p_buf->create_view(view_format);
            p_buf->update_desc_buf_info(0, VK_WHOLE_SIZE);
        }
        ~Texel_buffer()
        {
            if (p_buf) delete p_buf;
            if (mem_) p_dev_->dev.freeMemory(mem_);
        }
    private:
        base::Device *p_dev_;
        vk::BufferUsageFlags usage_{vk::BufferUsageFlagBits::eStorageTexelBuffer | vk::BufferUsageFlagBits::eTransferDst};
        vk::DeviceMemory mem_;
    };

    Texel_buffer *p_grid_flags_{nullptr};
    Texel_buffer *p_light_bounds_{nullptr};
    Texel_buffer *p_grid_light_counts_{nullptr};
    Texel_buffer *p_grid_light_count_total_{nullptr};
    Texel_buffer *p_grid_light_count_offsets_{nullptr};
    Texel_buffer *p_light_list_{nullptr};
    Texel_buffer *p_grid_light_counts_compare_{nullptr};

    void init_texel_buffers_()
    {
        std::vector<uint32_t> queue_families;
        if (p_phy_dev_->graphics_queue_family_idx != p_phy_dev_->compute_queue_family_idx) {
            queue_families.emplace_back(p_phy_dev_->graphics_queue_family_idx);
            queue_families.emplace_back(p_phy_dev_->compute_queue_family_idx);
        }

        vk::MemoryPropertyFlags device_local{vk::MemoryPropertyFlagBits::eDeviceLocal};
        vk::SharingMode sharing_mode=
            queue_families.empty() ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent;
        uint32_t *p_queue_family=
            queue_families.empty() ? nullptr : queue_families.data();
        uint32_t queue_family_count=
            queue_families.empty() ? 0 : static_cast<uint32_t>(queue_families.size());
        const uint32_t max_grid_count=((p_info_->MAX_WIDTH - 1) / p_info_->TILE_WIDTH + 1) *
            ((p_info_->MAX_HEIGHT - 1) / p_info_->TILE_HEIGHT + 1) *
            p_info_->TILE_COUNT_Z;

        p_grid_flags_=new Texel_buffer(p_phy_dev_, p_dev_,
                                       device_local,
                                       max_grid_count * sizeof(uint8_t),
                                       sharing_mode, queue_family_count, p_queue_family,
                                       vk::Format::eR8Uint // bool
        );

        p_light_bounds_=new Texel_buffer(p_phy_dev_,
                                         p_dev_,
                                         device_local,
                                         p_info_->MAX_NUM_LIGHTS * 6 * sizeof(uint32_t),
                                         sharing_mode, queue_family_count, p_queue_family,
                                         vk::Format::eR32Uint); // max tile count 1d (z 256)

        p_grid_light_counts_=new Texel_buffer(p_phy_dev_,
                                              p_dev_,
                                              device_local,
                                              max_grid_count * sizeof(uint32_t),
                                              sharing_mode, queue_family_count, p_queue_family,
                                              vk::Format::eR32Uint); // light count / grid

        p_grid_light_count_total_=new Texel_buffer(p_phy_dev_,
                                                   p_dev_,
                                                   device_local,
                                                   1 * sizeof(uint32_t),
                                                   sharing_mode, queue_family_count, p_queue_family,
                                                   vk::Format::eR32Uint); // light count total * max grid count

        p_grid_light_count_offsets_=new Texel_buffer(p_phy_dev_,
                                                     p_dev_,
                                                     device_local,
                                                     max_grid_count * sizeof(uint32_t),
                                                     sharing_mode, queue_family_count, p_queue_family,
                                                     vk::Format::eR32Uint); // same as above

        p_light_list_=new Texel_buffer(p_phy_dev_, p_dev_,
                                       device_local,
                                       1024 * 1024 * sizeof(uint32_t),
                                       sharing_mode, queue_family_count, p_queue_family,
                                       vk::Format::eR32Uint); // light idx

        p_grid_light_counts_compare_=new Texel_buffer(p_phy_dev_,
                                                      p_dev_,
                                                      device_local,
                                                      max_grid_count * sizeof(uint32_t),
                                                      sharing_mode, queue_family_count, p_queue_family,
                                                      vk::Format::eR32Uint); // light count / grid
    }

    void destroy_texel_buffers_()
    {
        delete p_grid_flags_;
        delete p_light_bounds_;
        delete p_grid_light_counts_;
        delete p_grid_light_count_offsets_;
        delete p_grid_light_count_total_;
        delete p_light_list_;
        delete p_grid_light_counts_compare_;
    }

    // ************************************************************************
    // back buffers
    // ************************************************************************

    struct Back_buffer
    {
        uint32_t swapchain_image_idx{0};
        vk::Semaphore swapchain_image_acquire_semaphore;
        vk::Semaphore pre_compute_render_semaphore;
        vk::Semaphore compute_semaphore;
        vk::Semaphore onscreen_render_semaphore;
        vk::Fence present_queue_submit_fence;
    };
    std::deque<Back_buffer> back_buffers_;
    Back_buffer acquired_back_buf_;
    const uint32_t back_buf_count_=3;

    void init_back_buffers_()
    {
        for (auto i=0; i < back_buf_count_; i++) {
            Back_buffer back;
            back.swapchain_image_acquire_semaphore=p_dev_->dev.createSemaphore(vk::SemaphoreCreateInfo());
            back.onscreen_render_semaphore=p_dev_->dev.createSemaphore(vk::SemaphoreCreateInfo());
            back.pre_compute_render_semaphore=p_dev_->dev.createSemaphore(vk::SemaphoreCreateInfo());
            back.compute_semaphore=p_dev_->dev.createSemaphore(vk::SemaphoreCreateInfo());
            back.present_queue_submit_fence=p_dev_->dev
                .createFence(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
            back_buffers_.push_back(back);
        }
    }

    void destroy_back_buffers_()
    {
        while (!back_buffers_.empty()) {
            auto &back=back_buffers_.front();
            p_dev_->dev.destroySemaphore(back.swapchain_image_acquire_semaphore);
            p_dev_->dev.destroySemaphore(back.onscreen_render_semaphore);
            p_dev_->dev.destroySemaphore(back.pre_compute_render_semaphore);
            p_dev_->dev.destroySemaphore(back.compute_semaphore);
            p_dev_->dev.destroyFence(back.present_queue_submit_fence);
            back_buffers_.pop_front();
        }
    }

    // ************************************************************************
    // command pools
    // ************************************************************************

    vk::CommandPool graphics_cmd_pool_;
    vk::CommandPool compute_cmd_pool_;

    void init_command_pools_()
    {
        graphics_cmd_pool_=p_dev_->dev.createCommandPool(
            vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                      p_phy_dev_->graphics_queue_family_idx));
        if (p_phy_dev_->compute_queue_family_idx == p_phy_dev_->graphics_queue_family_idx)
            compute_cmd_pool_=graphics_cmd_pool_;
        else
            compute_cmd_pool_=p_dev_->dev.createCommandPool(
                vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                          p_phy_dev_->compute_queue_family_idx));
    }

    void destroy_command_pools_()
    {
        p_dev_->dev.destroyCommandPool(graphics_cmd_pool_);
        if (p_phy_dev_->compute_queue_family_idx != p_phy_dev_->graphics_queue_family_idx)
            p_dev_->dev.destroyCommandPool(compute_cmd_pool_);
    }

    // ************************************************************************
    // model
    // ************************************************************************

    Model *p_model_{nullptr};

    void init_model_()
    {
        p_model_=new Model(p_phy_dev_, p_dev_, graphics_cmd_pool_);
        std::string model_dir=MODEL_DIR;
        auto asset_path=model_dir + "/sibenik";
        auto model_path=asset_path + "/sibenik_bubble.fbx";
        auto dummy_asset_path=model_dir + "/dummy";
        auto components=std::vector<base::Vertex_component >
        {
            base::VERT_COMP_POSITION,
            base::VERT_COMP_NORMAL,
            base::VERT_COMP_UV,
            base::VERT_COMP_TANGENT,
            base::VERT_COMP_BITANGENT
        };
        base::Vertex_layout layout(components);
        p_model_->load(model_path, layout, asset_path, dummy_asset_path);

        // update camera
        auto di=p_model_->aabb.get_diagonal();
        p_camera_->cam_far=10.f * glm::length(di);
        auto half_size=p_model_->aabb.get_half_size();
        p_camera_->eye_pos={half_size.x * .75f, -half_size.y *0.5f, 0.f};
        p_camera_->target.y-=half_size.y * 0.5f;
        p_camera_->update();

        // update camera speed
        auto di_len=glm::length(di);
        p_shell_->pan_speed=di_len / 100.f;
        p_shell_->zoom_speed=di_len / 800.f;
    }

    void destroy_model_()
    {
        delete p_model_;
    }

    // ************************************************************************
    // lights
    // ************************************************************************

    Lights lights_;

    float *p_light_position_ranges_{nullptr};
    char *p_light_colors_{nullptr};

    void init_lights_()
    {
        // Light objects
        lights_.reserve(p_info_->MAX_NUM_LIGHTS);
        generate_lights();

        // host data
        p_light_position_ranges_=reinterpret_cast<float *>(malloc(4 * sizeof(float) * p_info_->MAX_NUM_LIGHTS));
        p_light_colors_=reinterpret_cast<char *>(malloc(4 * sizeof(char) * p_info_->MAX_NUM_LIGHTS));
    }

    void destroy_lights_()
    {
        delete p_light_position_ranges_;
        delete p_light_colors_;
    }

    // ************************************************************************
    // Frame data
    // ************************************************************************

    struct Global_uniforms
    {
        glm::mat4 view;
        glm::mat4 normal;
        glm::mat4 model;
        glm::mat4 projection_clip;

        glm::vec2 tile_size;
        uint32_t grid_dim[2]; // tile count

        glm::vec3 cam_pos;
        float cam_far;

        glm::vec2 resolution;
        uint32_t num_lights;
    } global_uniforms_;

    enum Queries
    {
        QUERY_DEPTH_PASS=0,
        QUERY_CLUSTERING=1,
        QUERY_COMPUTE_FLAGS=2,
        QUERY_COMPUTE_OFFSETS=3,
        QUERY_COMPUTE_LIST=4,
        QUERY_ONSCREEN=5,
        QUERY_TRANSFER=6,
        QUERY_HSIZE=7
    };
    struct Query_data
    {
        uint32_t depth_pass[2];
        uint32_t clustering[2];
        uint32_t compute_flags[2];
        uint32_t compute_offsets[2];
        uint32_t compute_list[2];
        uint32_t onscreen[2];
        uint32_t transfer[2];
    };
    uint32_t query_count_;

    struct Frame_data
    {
        base::Buffer *p_global_uniforms{nullptr};

        Texel_buffer *p_light_pos_ranges{nullptr};
        Texel_buffer *p_light_colors{nullptr};

        vk::DescriptorSet desc_set;

        struct Command_buffer_block
        {
            vk::CommandBuffer cmd_buffer;
            vk::Fence submit_fence;
        } offscreen_cmd_buf_blk, compute_cmd_buf_blk, onscreen_cmd_buf_blk;

        vk::QueryPool query_pool;
        Query_data query_data;
    };
    std::vector<Frame_data> frame_data_vec_;
    vk::DeviceMemory global_uniforms_mem_;
    uint32_t frame_data_count_{0};
    uint32_t frame_data_idx_{0};

    vk::CommandBufferBeginInfo cmd_begin_info_;

    vk::SubmitInfo offscreen_cmd_submit_info_;
    vk::SubmitInfo compute_cmd_submit_info_;
    vk::SubmitInfo onscreen_cmd_submit_info_;

    vk::PipelineStageFlags offscreen_wait_stages_;
    vk::PipelineStageFlags compute_wait_stages_;
    vk::PipelineStageFlags onscreen_wait_stages_;

    void init_frame_data_()
    {
        frame_data_count_=back_buf_count_;
        frame_data_vec_.resize(frame_data_count_);

        // buffers
        {
            std::vector<base::Buffer *> p_bufs(frame_data_count_);
            vk::MemoryPropertyFlags device_local{vk::MemoryPropertyFlagBits::eDeviceLocal};
            vk::MemoryPropertyFlags host_visible_coherent{vk::MemoryPropertyFlagBits::eHostVisible |
                vk::MemoryPropertyFlagBits::eHostCoherent};
            vk::SharingMode sharing_mode{vk::SharingMode::eExclusive};
            uint32_t queue_family_count=0;
            uint32_t *p_queue_family=nullptr;
            uint32_t queue_families[2]=
            {
                p_phy_dev_->graphics_queue_family_idx,
                p_phy_dev_->compute_queue_family_idx
            };
            if (p_phy_dev_->graphics_queue_family_idx != p_phy_dev_->compute_queue_family_idx) {
                sharing_mode=vk::SharingMode::eConcurrent;
                queue_family_count=2;
                p_queue_family=queue_families;
            }

            uint32_t idx=0;
            for (auto &data : frame_data_vec_) {
                // light buffers
                data.p_light_colors=new Texel_buffer(p_phy_dev_, p_dev_,
                                                     host_visible_coherent,
                                                     p_info_->MAX_NUM_LIGHTS * 4 * sizeof(char),
                                                     vk::SharingMode::eExclusive,
                                                     0, nullptr,
                                                     vk::Format::eR8G8B8A8Unorm);
                data.p_light_pos_ranges=new Texel_buffer(p_phy_dev_, p_dev_,
                                                         host_visible_coherent,
                                                         p_info_->MAX_NUM_LIGHTS * sizeof(glm::vec4),
                                                         sharing_mode,
                                                         queue_family_count,
                                                         p_queue_family,
                                                         vk::Format::eR32G32B32A32Sfloat);

                // global_uniforms_buffer
                data.p_global_uniforms=new base::Buffer(p_dev_,
                                                        vk::BufferUsageFlagBits::eUniformBuffer,
                                                        host_visible_coherent,
                                                        sizeof(Global_uniforms),
                                                        sharing_mode,
                                                        queue_family_count,
                                                        p_queue_family);
                data.p_global_uniforms->update_desc_buf_info(0, VK_WHOLE_SIZE);
                p_bufs[idx++]=data.p_global_uniforms;
            }
            base::allocate_and_bind_buffer_memory(p_phy_dev_,
                                                  p_dev_,
                                                  global_uniforms_mem_,
                                                  frame_data_count_,
                                                  p_bufs.data());
        }

        // cmd buffers
        {
            std::vector<vk::CommandBuffer> graphics_cmd_buffers(2 * frame_data_count_);
            std::vector<vk::CommandBuffer> compute_cmd_buffers(frame_data_count_);
            graphics_cmd_buffers=p_dev_->dev.allocateCommandBuffers(
                vk::CommandBufferAllocateInfo(graphics_cmd_pool_,
                                              vk::CommandBufferLevel::ePrimary,
                                              static_cast<uint32_t>(graphics_cmd_buffers.size())));
            compute_cmd_buffers=p_dev_->dev.allocateCommandBuffers(
                vk::CommandBufferAllocateInfo(compute_cmd_pool_,
                                              vk::CommandBufferLevel::ePrimary,
                                              static_cast<uint32_t>(compute_cmd_buffers.size())));

            uint32_t gidx=0;
            uint32_t cidx=0;
            for (auto &data : frame_data_vec_) {
                data.offscreen_cmd_buf_blk.cmd_buffer=graphics_cmd_buffers[gidx++];
                data.onscreen_cmd_buf_blk.cmd_buffer=graphics_cmd_buffers[gidx++];
                data.compute_cmd_buf_blk.cmd_buffer=compute_cmd_buffers[cidx++];
                data.offscreen_cmd_buf_blk.submit_fence=
                    p_dev_->dev.createFence(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
                data.compute_cmd_buf_blk.submit_fence=
                    p_dev_->dev.createFence(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
                data.onscreen_cmd_buf_blk.submit_fence=
                    p_dev_->dev.createFence(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
            }
        }

        // cmd info
        {
            cmd_begin_info_=
            {
                vk::CommandBufferUsageFlagBits::eOneTimeSubmit
            };

            // the stage where semaphore wait happens
            offscreen_wait_stages_=vk::PipelineStageFlagBits::eFragmentShader;
            offscreen_cmd_submit_info_=
            {
                1, nullptr,
                &offscreen_wait_stages_,
                1, nullptr,
                1, nullptr
            };

            compute_wait_stages_=vk::PipelineStageFlagBits::eComputeShader;
            compute_cmd_submit_info_=
            {
                1, nullptr,
                &compute_wait_stages_,
                1, nullptr,
                1, nullptr
            };

            onscreen_wait_stages_=vk::PipelineStageFlagBits::eColorAttachmentOutput;
            onscreen_cmd_submit_info_=
            {
                1, nullptr,
                &onscreen_wait_stages_,
                1, nullptr,
                1, nullptr
            };
        }

        // query pool
        {
            query_count_=QUERY_HSIZE * 2;
            for (auto &data : frame_data_vec_) {
                data.query_pool=p_dev_->dev.createQueryPool(
                    vk::QueryPoolCreateInfo({},
                                            vk::QueryType::eTimestamp,
                                            query_count_,
                                            {}));
            }
        }
    }

    void destroy_frame_data_()
    {
        p_dev_->dev.freeMemory(global_uniforms_mem_);
        for (auto &data : frame_data_vec_) {
            delete data.p_global_uniforms;
            delete data.p_light_pos_ranges;
            delete data.p_light_colors;
            p_dev_->dev.destroyFence(data.offscreen_cmd_buf_blk.submit_fence);
            p_dev_->dev.destroyFence(data.compute_cmd_buf_blk.submit_fence);
            p_dev_->dev.destroyFence(data.onscreen_cmd_buf_blk.submit_fence);
            p_dev_->dev.destroyQueryPool(data.query_pool);
        }
    }

    // ************************************************************************
    // text overlay 
    // ************************************************************************

    Text_overlay *p_text_overlay_{nullptr};

    void init_text_overlay_()
    {
        std::string file_path=FONT_DIR;
        file_path.append("/RobotoMonoMedium");
        p_text_overlay_=new Text_overlay(p_phy_dev_, p_dev_, graphics_cmd_pool_, file_path);
    }

    void destroy_text_overlay_()
    {
        delete p_text_overlay_;
    }

    // ************************************************************************
    // descriptor sets
    // ************************************************************************

    vk::DescriptorPool desc_pool_;

    // the same desc-set cannot appear in different layouts
    struct Descriptor_set_layouts
    {
        vk::DescriptorSetLayout frame_data;
        vk::DescriptorSetLayout texel_buffers;
        vk::DescriptorSetLayout font_tex;
    } desc_set_layouts_;

    vk::DescriptorSet desc_set_texel_buffers_;
    vk::DescriptorSet desc_set_font_tex_;

    void init_descriptors_()
    {
        // desc set layouts
        {
            // desc set layout bindings
            vk::ShaderStageFlags vert_frag_comp
            {
                vk::ShaderStageFlagBits::eVertex |
                vk::ShaderStageFlagBits::eFragment
                | vk::ShaderStageFlagBits::eCompute};
            vk::ShaderStageFlags frag_comp
            {
                vk::ShaderStageFlagBits::eFragment |
                vk::ShaderStageFlagBits::eCompute};
            vk::ShaderStageFlags frag{vk::ShaderStageFlagBits::eFragment};
            vk::ShaderStageFlags comp{vk::ShaderStageFlagBits::eCompute};

            vk::DescriptorSetLayoutBinding binding_global_uniforms=
            {
                0, vk::DescriptorType::eUniformBuffer, 1, vert_frag_comp
            };
            vk::DescriptorSetLayoutBinding binding_light_pos_ranges=
            {
                0, vk::DescriptorType::eStorageTexelBuffer, 1, frag_comp
            };
            vk::DescriptorSetLayoutBinding binding_light_colors=
            {
                0, vk::DescriptorType::eStorageTexelBuffer, 1, frag
            };
            vk::DescriptorSetLayoutBinding binding_grid_flags=
            {
                0, vk::DescriptorType::eStorageTexelBuffer, 1, frag_comp
            };
            vk::DescriptorSetLayoutBinding binding_light_bounds=
            {
                0, vk::DescriptorType::eStorageTexelBuffer, 1, frag_comp
            };
            vk::DescriptorSetLayoutBinding binding_grid_light_counts=
            {
                0, vk::DescriptorType::eStorageTexelBuffer, 1, frag_comp
            };
            vk::DescriptorSetLayoutBinding binding_grid_light_count_total=
            {
                0, vk::DescriptorType::eStorageTexelBuffer, 1, comp
            };
            vk::DescriptorSetLayoutBinding binding_grid_light_count_offsets=
            {
                0, vk::DescriptorType::eStorageTexelBuffer, 1, frag_comp
            };
            vk::DescriptorSetLayoutBinding binding_light_list=
            {
                0, vk::DescriptorType::eStorageTexelBuffer, 1, frag_comp
            };
            vk::DescriptorSetLayoutBinding binding_grid_light_counts_compare=
            {
                0, vk::DescriptorType::eStorageTexelBuffer, 1, frag_comp
            };
            vk::DescriptorSetLayoutBinding binding_font_tex=
            {
                0, vk::DescriptorType::eCombinedImageSampler, 1, frag
            };

            // frame data

            std::vector<vk::DescriptorSetLayoutBinding> bindings;
            binding_global_uniforms.binding=0;
            binding_light_pos_ranges.binding=1;
            binding_light_colors.binding=2;

            bindings.push_back(binding_global_uniforms);
            bindings.push_back(binding_light_pos_ranges);
            bindings.push_back(binding_light_colors);
            desc_set_layouts_.frame_data=p_dev_->dev.createDescriptorSetLayout(
                vk::DescriptorSetLayoutCreateInfo({},
                                                  static_cast<uint32_t>(bindings.size()),
                                                  bindings.data()));
            bindings.clear();

            // buf and sampler

            binding_grid_flags.binding=0;
            binding_light_bounds.binding=1;
            binding_grid_light_counts.binding=2;
            binding_grid_light_count_total.binding=3;
            binding_grid_light_count_offsets.binding=4;
            binding_light_list.binding=5;
            binding_grid_light_counts_compare.binding=6;

            bindings.push_back(binding_grid_flags);
            bindings.push_back(binding_light_bounds);
            bindings.push_back(binding_grid_light_counts);
            bindings.push_back(binding_grid_light_count_total);
            bindings.push_back(binding_grid_light_count_offsets);
            bindings.push_back(binding_light_list);
            bindings.push_back(binding_grid_light_counts_compare);

            desc_set_layouts_.texel_buffers=p_dev_->dev.createDescriptorSetLayout(
                vk::DescriptorSetLayoutCreateInfo({},
                                                  static_cast<uint32_t>(bindings.size()),
                                                  bindings.data()));
            bindings.clear();

            // font tex

            desc_set_layouts_.font_tex=p_dev_->dev.createDescriptorSetLayout(
                vk::DescriptorSetLayoutCreateInfo({},
                                                  1,
                                                  &binding_font_tex));
        }

        // desc pool
        {
            std::vector<vk::DescriptorPoolSize> pool_sizes
            {
                vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, frame_data_count_ * 1),
                vk::DescriptorPoolSize(vk::DescriptorType::eStorageTexelBuffer, frame_data_count_ * 2 + 7),
                vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 1)
            };

            desc_pool_=p_dev_->dev.createDescriptorPool(
                vk::DescriptorPoolCreateInfo({},
                                             frame_data_count_ + 2,
                                             static_cast<uint32_t>(pool_sizes.size()),
                                             pool_sizes.data()));
        }

        // allocate and write desc sets
        {
            std::vector<vk::DescriptorSetLayout> set_layouts;
            std::vector<vk::WriteDescriptorSet> writes;

            for (auto i=0; i < frame_data_count_; i++) {
                set_layouts.emplace_back(desc_set_layouts_.frame_data);
            }
            set_layouts.emplace_back(desc_set_layouts_.texel_buffers);
            set_layouts.emplace_back(desc_set_layouts_.font_tex);

            std::vector<vk::DescriptorSet> desc_sets=p_dev_->dev.allocateDescriptorSets(
                vk::DescriptorSetAllocateInfo(
                    desc_pool_, static_cast<uint32_t>(set_layouts.size()), set_layouts.data()));

            // frame data

            uint32_t idx=0;
            for (auto &data : frame_data_vec_) {
                data.desc_set=desc_sets[idx++];

                writes.emplace_back(data.desc_set,
                                    0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr,
                                    &data.p_global_uniforms->desc_buf_info, nullptr);
                writes.emplace_back(data.desc_set,
                                    1, 0, 1, vk::DescriptorType::eStorageTexelBuffer, nullptr,
                                    &data.p_light_pos_ranges->p_buf->desc_buf_info,
                                    &data.p_light_pos_ranges->p_buf->view);
                writes.emplace_back(data.desc_set,
                                    2, 0, 1, vk::DescriptorType::eStorageTexelBuffer, nullptr,
                                    &data.p_light_colors->p_buf->desc_buf_info,
                                    &data.p_light_colors->p_buf->view);
            }

            // texel_buffers

            desc_set_texel_buffers_=desc_sets[idx++];

            writes.emplace_back(desc_set_texel_buffers_,
                                0, 0, 1, vk::DescriptorType::eStorageTexelBuffer, nullptr,
                                &p_grid_flags_->p_buf->desc_buf_info,
                                &p_grid_flags_->p_buf->view);
            writes.emplace_back(desc_set_texel_buffers_,
                                1, 0, 1, vk::DescriptorType::eStorageTexelBuffer, nullptr,
                                &p_light_bounds_->p_buf->desc_buf_info,
                                &p_light_bounds_->p_buf->view);
            writes.emplace_back(desc_set_texel_buffers_,
                                2, 0, 1, vk::DescriptorType::eStorageTexelBuffer, nullptr,
                                &p_grid_light_counts_->p_buf->desc_buf_info,
                                &p_grid_light_counts_->p_buf->view);
            writes.emplace_back(desc_set_texel_buffers_,
                                3, 0, 1, vk::DescriptorType::eStorageTexelBuffer, nullptr,
                                &p_grid_light_count_total_->p_buf->desc_buf_info,
                                &p_grid_light_count_total_->p_buf->view);
            writes.emplace_back(desc_set_texel_buffers_,
                                4, 0, 1, vk::DescriptorType::eStorageTexelBuffer, nullptr,
                                &p_grid_light_count_offsets_->p_buf->desc_buf_info,
                                &p_grid_light_count_offsets_->p_buf->view);
            writes.emplace_back(desc_set_texel_buffers_,
                                5, 0, 1, vk::DescriptorType::eStorageTexelBuffer, nullptr,
                                &p_light_list_->p_buf->desc_buf_info,
                                &p_light_list_->p_buf->view);
            writes.emplace_back(desc_set_texel_buffers_,
                                6, 0, 1, vk::DescriptorType::eStorageTexelBuffer, nullptr,
                                &p_grid_light_counts_compare_->p_buf->desc_buf_info,
                                &p_grid_light_counts_compare_->p_buf->view);

            // font tex

            desc_set_font_tex_=desc_sets[idx];

            writes.emplace_back(desc_set_font_tex_,
                                0, 0, 1, vk::DescriptorType::eCombinedImageSampler,
                                &p_text_overlay_->p_font->p_tex->desc_image_info,
                                nullptr, nullptr);

            p_dev_->dev.updateDescriptorSets(static_cast<uint32_t>(writes.size()),
                                             writes.data(), 0, nullptr);
            writes.clear();

        }
    }

    void destroy_descriptors_()
    {
        p_dev_->dev.destroyDescriptorPool(desc_pool_);
        p_dev_->dev.destroyDescriptorSetLayout(desc_set_layouts_.frame_data);
        p_dev_->dev.destroyDescriptorSetLayout(desc_set_layouts_.texel_buffers);
        p_dev_->dev.destroyDescriptorSetLayout(desc_set_layouts_.font_tex);
    }

    // ************************************************************************
    // shaders
    // ************************************************************************

    base::Shader *p_simple_vs_{nullptr};
    base::Shader *p_clustering_vs_{nullptr};
    base::Shader *p_clustering_fs_{nullptr};
    base::Shader *p_calc_light_grids_{nullptr};
    base::Shader *p_calc_grid_offsets_{nullptr};
    base::Shader *p_calc_light_list_{nullptr};
    base::Shader *p_cluster_forward_vs_{nullptr};
    base::Shader *p_cluster_forward_fs_{nullptr};
    base::Shader *p_light_particles_vs_{nullptr};
    base::Shader *p_light_particles_fs_{nullptr};

    void init_shaders_()
    {
        p_simple_vs_=new base::Shader(p_dev_, vk::ShaderStageFlagBits::eVertex);
        p_clustering_vs_=new base::Shader(p_dev_, vk::ShaderStageFlagBits::eVertex);
        p_clustering_fs_=new base::Shader(p_dev_, vk::ShaderStageFlagBits::eFragment);
        p_calc_light_grids_=new base::Shader(p_dev_, vk::ShaderStageFlagBits::eCompute);
        p_calc_grid_offsets_=new base::Shader(p_dev_, vk::ShaderStageFlagBits::eCompute);
        p_calc_light_list_=new base::Shader(p_dev_, vk::ShaderStageFlagBits::eCompute);
        p_cluster_forward_vs_=new base::Shader(p_dev_, vk::ShaderStageFlagBits::eVertex);
        p_cluster_forward_fs_=new base::Shader(p_dev_, vk::ShaderStageFlagBits::eFragment);
        p_light_particles_vs_=new base::Shader(p_dev_, vk::ShaderStageFlagBits::eVertex);
        p_light_particles_fs_=new base::Shader(p_dev_, vk::ShaderStageFlagBits::eFragment);

        p_simple_vs_->generate(sizeof(simple_vert), simple_vert);
        p_clustering_vs_->generate(sizeof(clustering_vert), clustering_vert);
        p_clustering_fs_->generate(sizeof(clustering_frag), clustering_frag);
        p_calc_light_grids_->generate(sizeof(calc_light_grids_comp), calc_light_grids_comp);
        p_calc_grid_offsets_->generate(sizeof(calc_grid_offsets_comp), calc_grid_offsets_comp);
        p_calc_light_list_->generate(sizeof(calc_light_list_comp), calc_light_list_comp);
        p_cluster_forward_vs_->generate(sizeof(cluster_forward_vert), cluster_forward_vert);
        p_cluster_forward_fs_->generate(sizeof(cluster_forward_frag), cluster_forward_frag);
        p_light_particles_vs_->generate(sizeof(light_particles_vert), light_particles_vert);
        p_light_particles_fs_->generate(sizeof(light_particles_frag), light_particles_frag);
    }

    void destroy_shaders_()
    {
        delete p_simple_vs_;
        delete p_clustering_vs_;
        delete p_clustering_fs_;
        delete p_calc_light_grids_;
        delete p_calc_grid_offsets_;
        delete p_calc_light_list_;
        delete p_cluster_forward_vs_;
        delete p_cluster_forward_fs_;
        delete p_light_particles_vs_;
        delete p_light_particles_fs_;
    }

    // ************************************************************************
    // render passes
    // ************************************************************************

    std::vector<vk::ClearValue> offscreen_rp_clear_values_
    {
        vk::ClearDepthStencilValue(1.0f, 0)
    };
    std::vector<vk::ClearValue> onscreen_rp_clear_values_
    {
        vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}),
        vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}),
        vk::ClearDepthStencilValue(1.0f, 0),
    };

    base::Render_pass *p_offscreen_rp_{nullptr};
    base::Render_pass *p_onscreen_rp_{nullptr};

    void init_render_passes_()
    {
        /********************* pre-compute render pass ********************/

        {
            p_offscreen_rp_=new base::Render_pass(p_dev_, 1, offscreen_rp_clear_values_.data());

            enum
            {
                ATTACHMENT_REFERENCE_DEPTH=0
            };
            enum
            {
                SUBPASS_DEPTH=0,
                SUBPASS_CLUSTER_FLAG=1,
            };

            std::vector<vk::AttachmentDescription> attachment_descriptions=
            {
                // depth
                {
                    {},
                    depth_format_,
                    vk::SampleCountFlagBits::e1,
                    vk::AttachmentLoadOp::eClear,
                    vk::AttachmentStoreOp::eStore,
                    vk::AttachmentLoadOp::eDontCare,
                    vk::AttachmentStoreOp::eDontCare,
                    vk::ImageLayout::eUndefined,
                    vk::ImageLayout::eDepthStencilAttachmentOptimal
                }
            };

            // depth prepass read/write
            static const vk::AttachmentReference ref_depth
            {
                static_cast<uint32_t>(ATTACHMENT_REFERENCE_DEPTH),
                vk::ImageLayout::eDepthStencilAttachmentOptimal
            };

            std::vector<vk::SubpassDescription> subpass_descriptions=
            {
                // depth prepass
                vk::SubpassDescription(
                    vk::SubpassDescriptionFlags(),
                    vk::PipelineBindPoint::eGraphics,
                    0, nullptr, // input
                    0, nullptr, // color
                    nullptr, // resolve
                    &ref_depth, // depth
                    0, nullptr // preserve
                ),
                // clustering subpass
                vk::SubpassDescription(
                    vk::SubpassDescriptionFlags(),
                    vk::PipelineBindPoint::eGraphics,
                    0, nullptr, // input
                    0, nullptr, // color
                    nullptr, // resolve
                    &ref_depth, // depth
                    0, nullptr // preserve
                )
            };

            std::vector<vk::SubpassDependency> dependencies=
            {
                {
                    VK_SUBPASS_EXTERNAL, // src
                    static_cast<uint32_t>(SUBPASS_DEPTH), // dst
                    vk::PipelineStageFlagBits::eBottomOfPipe, // src stages
                    vk::PipelineStageFlagBits::eVertexShader, // dst stages
                    vk::AccessFlagBits::eMemoryWrite, // src access
                    vk::AccessFlagBits::eUniformRead, // dst access
                    vk::DependencyFlagBits::eByRegion // dependency flags
                },
                {
                    static_cast<uint32_t>(SUBPASS_DEPTH), // src
                    static_cast<uint32_t>(SUBPASS_CLUSTER_FLAG), // dst
                    vk::PipelineStageFlagBits::eLateFragmentTests, // src stages
                    vk::PipelineStageFlagBits::eEarlyFragmentTests, // dst stages
                    vk::AccessFlagBits::eDepthStencilAttachmentWrite, // src access
                    vk::AccessFlagBits::eDepthStencilAttachmentRead, // dst access
                    vk::DependencyFlagBits::eByRegion // dependency flags
                },
                {
                    static_cast<uint32_t>(SUBPASS_CLUSTER_FLAG), // src
                    VK_SUBPASS_EXTERNAL, // dst
                    vk::PipelineStageFlagBits::eFragmentShader, // src stages
                    vk::PipelineStageFlagBits::eComputeShader, // dst stages
                    vk::AccessFlagBits::eShaderWrite, // src access
                    vk::AccessFlagBits::eShaderRead, // dst access
                    vk::DependencyFlagBits::eByRegion // dependency flags
                }
            };

            p_offscreen_rp_->create(static_cast<uint32_t>(attachment_descriptions.size()),
                                    attachment_descriptions.data(),
                                    static_cast<uint32_t>(subpass_descriptions.size()),
                                    subpass_descriptions.data(),
                                    static_cast<uint32_t>(dependencies.size()),
                                    dependencies.data());
        }

        /********************* onscreen render pass *********************/

        {
            p_onscreen_rp_=new base::Render_pass(p_dev_, 3, onscreen_rp_clear_values_.data());

            enum
            {
                ATTACHMENT_REFERENCE_CLUSTER_FORWARD=0,
                ATTACHMENT_REFERENCE_ONSCREEN_COLOR=1,
                ATTACHMENT_REFERENCE_CLUSTER_FORWARD_DEPTH=2
            };

            enum
            {
                SUBPASS_CLUSTER_FORWARD=0
            };

            std::vector<vk::AttachmentDescription> attachment_descriptions=
            {
                // cluster forward
                {
                    {},
                    surface_format_.format,
                    vk::SampleCountFlagBits::e4,
                    vk::AttachmentLoadOp::eClear,
                    vk::AttachmentStoreOp::eStore,
                    vk::AttachmentLoadOp::eDontCare,
                    vk::AttachmentStoreOp::eDontCare,
                    vk::ImageLayout::eUndefined,
                    vk::ImageLayout::eColorAttachmentOptimal
                },
                // onscreen color
                {
                    {},
                    surface_format_.format,
                    vk::SampleCountFlagBits::e1,
                    vk::AttachmentLoadOp::eDontCare,
                    vk::AttachmentStoreOp::eStore,
                    vk::AttachmentLoadOp::eDontCare,
                    vk::AttachmentStoreOp::eDontCare,
                    vk::ImageLayout::eUndefined,
                    vk::ImageLayout::ePresentSrcKHR
                },
                // cluster forward depth
                {
                    {},
                    depth_format_,
                    vk::SampleCountFlagBits::e4,
                    vk::AttachmentLoadOp::eClear,
                    vk::AttachmentStoreOp::eDontCare,
                    vk::AttachmentLoadOp::eDontCare,
                    vk::AttachmentStoreOp::eDontCare,
                    vk::ImageLayout::eUndefined,
                    vk::ImageLayout::eDepthStencilAttachmentOptimal
                }
            };

            // cluster forward subpass write
            static const vk::AttachmentReference ref_cluster_forward_color
            {
                static_cast<uint32_t>(ATTACHMENT_REFERENCE_CLUSTER_FORWARD),
                vk::ImageLayout::eColorAttachmentOptimal
            };
            static const vk::AttachmentReference ref_cluster_forward_depth
            {
                static_cast<uint32_t>(ATTACHMENT_REFERENCE_CLUSTER_FORWARD_DEPTH),
                vk::ImageLayout::eDepthStencilAttachmentOptimal
            };
            static const vk::AttachmentReference ref_onscreen
            {
                static_cast<uint32_t>(ATTACHMENT_REFERENCE_ONSCREEN_COLOR),
                vk::ImageLayout::eColorAttachmentOptimal
            };

            std::vector<vk::SubpassDescription> subpass_descriptions=
            {
                vk::SubpassDescription(
                    {},
                    vk::PipelineBindPoint::eGraphics,
                    0, nullptr, // input
                    1, &ref_cluster_forward_color, // color
                    &ref_onscreen, // resolve
                    &ref_cluster_forward_depth, // depth
                    0, nullptr // preserve
                )
            };

            /* subpass dependencies */

            std::vector<vk::SubpassDependency> dependencies=
            {
                {
                    VK_SUBPASS_EXTERNAL, // src
                    static_cast<uint32_t>(SUBPASS_CLUSTER_FORWARD), // dst
                    vk::PipelineStageFlagBits::eBottomOfPipe, // src stages
                    vk::PipelineStageFlagBits::eColorAttachmentOutput, // dst stages
                    vk::AccessFlagBits::eMemoryRead, // src access
                    vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite, // dst access
                    vk::DependencyFlagBits::eByRegion // dependency flags
                },
                {
                    static_cast<uint32_t>(SUBPASS_CLUSTER_FORWARD), // src
                    VK_SUBPASS_EXTERNAL, // dst
                    vk::PipelineStageFlagBits::eColorAttachmentOutput, // src stages
                    vk::PipelineStageFlagBits::eBottomOfPipe, // dst stages
                    vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
                    vk::AccessFlagBits::eMemoryRead, // dst access
                    vk::DependencyFlagBits::eByRegion // dependency flags
                }
            };

            p_onscreen_rp_->create(static_cast<uint32_t>(attachment_descriptions.size()),
                                   attachment_descriptions.data(),
                                   static_cast<uint32_t>(subpass_descriptions.size()),
                                   subpass_descriptions.data(),
                                   static_cast<uint32_t>(dependencies.size()),
                                   dependencies.data());
        }
    }

    void destroy_render_passes_()
    {
        delete p_offscreen_rp_;
        delete p_onscreen_rp_;
    }

    // ************************************************************************
    // offscreen framebuffers
    // ************************************************************************
    base::Render_target *p_rt_offscreen_depth_{nullptr};
    vk::Framebuffer offscreen_framebuffer_;

    vk::Viewport offscreen_viewport_; // set on frame
    vk::Rect2D offscreen_scissor_; // set on frame

    void init_offscreen_framebuffer_()
    {
        offscreen_viewport_.minDepth=0.f;
        offscreen_viewport_.maxDepth=1.f;

        p_rt_offscreen_depth_=new base::Render_target(p_phy_dev_,
                                                      p_dev_,
                                                      depth_format_,
                                                      {p_info_->MAX_WIDTH, p_info_->MAX_HEIGHT},
                                                      vk::ImageUsageFlagBits::eDepthStencilAttachment,
                                                      vk::ImageAspectFlagBits::eDepth,
                                                      vk::SampleCountFlagBits::e1);

        const uint32_t attachment_count=1;
        vk::ImageView attachments[attachment_count]=
        {
            p_rt_offscreen_depth_->view
        };

        offscreen_framebuffer_=p_dev_->dev.createFramebuffer(
            vk::FramebufferCreateInfo({},
                                      p_offscreen_rp_->rp,
                                      attachment_count,
                                      attachments,
                                      p_info_->MAX_WIDTH,
                                      p_info_->MAX_HEIGHT,
                                      1));
        p_offscreen_rp_->rp_begin.framebuffer=offscreen_framebuffer_;
    }

    void destroy_offscreen_framebuffer_()
    {
        p_dev_->dev.destroyFramebuffer(offscreen_framebuffer_);
        delete p_rt_offscreen_depth_;
    }

    // ************************************************************************
    // swapchain
    // ************************************************************************

    Swapchain *p_swapchain_{nullptr};

    void init_swapchain_()
    {
        p_swapchain_=new Swapchain(p_phy_dev_,
                                   p_dev_,
                                   surface_,
                                   surface_format_,
                                   depth_format_,
                                   back_buf_count_,
                                   p_onscreen_rp_);
        p_swapchain_->resize(p_info_->width(), p_info_->height());
        auto extent=p_swapchain_->curr_extent();
        if (extent.width != p_info_->width() || extent.height != p_info_->height()) {
            p_info_->on_resize(extent.width, extent.height);
        }
    }

    void destroy_swapchain_()
    {
        p_swapchain_->detach();
        delete p_swapchain_;
    }

    // ************************************************************************
    // pipelines
    // ************************************************************************

    struct Pipelines
    {
        vk::Pipeline depth;
        vk::Pipeline clustering_opaque;
        vk::Pipeline clustering_transparent;
        vk::Pipeline calc_light_grids;
        vk::Pipeline calc_grid_offsets;
        vk::Pipeline calc_light_list;
        vk::Pipeline cluster_forward_opaque;
        vk::Pipeline cluster_forward_transparent;
        vk::Pipeline light_particles;
        vk::Pipeline text_overlay;
    } pipelines_;

    struct Pipeline_layouts
    {
        vk::PipelineLayout depth;
        vk::PipelineLayout clustering;
        vk::PipelineLayout calc_light_grids;
        vk::PipelineLayout calc_grid_offsets;
        vk::PipelineLayout calc_light_list;
        vk::PipelineLayout cluster_forward;
        vk::PipelineLayout light_particles;
        vk::PipelineLayout text_overlay;
    } pipeline_layouts_;

    struct Pipeline_desc_set_ptrs
    {
        std::vector<vk::DescriptorSet> clustering;
        std::vector<vk::DescriptorSet> calc_light_grids;
        std::vector<vk::DescriptorSet> calc_grid_offsets;
        std::vector<vk::DescriptorSet> calc_light_list;
        std::vector<vk::DescriptorSet> cluster_forward;
        std::vector<vk::DescriptorSet> light_particles;
    } pipeline_desc_sets_;

    void init_pipelines_()
    {
        // pipeline layouts
        {
            // depth
            std::vector<vk::DescriptorSetLayout> desc_set_layouts={desc_set_layouts_.frame_data}; // set on frame
            pipeline_layouts_.depth=p_dev_->dev.createPipelineLayout(
                vk::PipelineLayoutCreateInfo({},
                                             static_cast<uint32_t>(desc_set_layouts.size()),
                                             desc_set_layouts.data(),
                                             0, nullptr));

            // clustering

            desc_set_layouts=
            {
                desc_set_layouts_.frame_data,  // set on frame
                desc_set_layouts_.texel_buffers
            };

            pipeline_desc_sets_.clustering.resize(desc_set_layouts.size());
            pipeline_desc_sets_.clustering[1]=desc_set_texel_buffers_;

            pipeline_layouts_.clustering=p_dev_->dev.createPipelineLayout(
                vk::PipelineLayoutCreateInfo({},
                                             static_cast<uint32_t>(desc_set_layouts.size()),
                                             desc_set_layouts.data(),
                                             0, nullptr));

            //  cluster forward

            desc_set_layouts=
            {
                p_model_->desc_set_layout_uniform,
                p_model_->desc_set_layout_sampler, // set for each material
                desc_set_layouts_.frame_data, // set on frame
                desc_set_layouts_.texel_buffers,
            };

            pipeline_desc_sets_.cluster_forward.resize(desc_set_layouts.size());
            pipeline_desc_sets_.cluster_forward[0]=p_model_->desc_set_uniform;
            pipeline_desc_sets_.cluster_forward[3]=desc_set_texel_buffers_;

            pipeline_layouts_.cluster_forward=p_dev_->dev.createPipelineLayout(
                vk::PipelineLayoutCreateInfo({},
                                             static_cast<uint32_t>(desc_set_layouts.size()),
                                             desc_set_layouts.data(),
                                             0, nullptr));

            // light particles
            desc_set_layouts={
                desc_set_layouts_.frame_data // set on frame
            };

            pipeline_desc_sets_.light_particles.resize(desc_set_layouts.size());
            pipeline_layouts_.light_particles=p_dev_->dev.createPipelineLayout(
                vk::PipelineLayoutCreateInfo({},
                                             static_cast<uint32_t>(desc_set_layouts.size()),
                                             desc_set_layouts.data(),
                                             0, nullptr));

            // text overlay
            desc_set_layouts={
                desc_set_layouts_.font_tex
            };

            pipeline_layouts_.text_overlay=p_dev_->dev.createPipelineLayout(
                vk::PipelineLayoutCreateInfo({},
                                             static_cast<uint32_t>(desc_set_layouts.size()),
                                             desc_set_layouts.data(),
                                             0, nullptr));

            // cal light grids

            desc_set_layouts=
            {
                desc_set_layouts_.frame_data, // set on frame
                desc_set_layouts_.texel_buffers
            };

            pipeline_desc_sets_.calc_light_grids.resize(desc_set_layouts.size());
            pipeline_desc_sets_.calc_light_grids[1]=desc_set_texel_buffers_;

            pipeline_layouts_.calc_light_grids=p_dev_->dev.createPipelineLayout(
                vk::PipelineLayoutCreateInfo({},
                                             static_cast<uint32_t>(desc_set_layouts.size()),
                                             desc_set_layouts.data(),
                                             0, nullptr));

            // cal grid offsets

            desc_set_layouts=
            {
                desc_set_layouts_.frame_data, // set on frame
                desc_set_layouts_.texel_buffers
            };

            pipeline_desc_sets_.calc_grid_offsets.resize(desc_set_layouts.size());
            pipeline_desc_sets_.calc_grid_offsets[1]=desc_set_texel_buffers_;

            pipeline_layouts_.calc_grid_offsets=p_dev_->dev.createPipelineLayout(
                vk::PipelineLayoutCreateInfo({},
                                             static_cast<uint32_t>(desc_set_layouts.size()),
                                             desc_set_layouts.data(),
                                             0, nullptr));

            // calc light list

            desc_set_layouts=
            {
                desc_set_layouts_.frame_data, // set on frame
                desc_set_layouts_.texel_buffers,
            };

            pipeline_desc_sets_.calc_light_list.resize(desc_set_layouts.size());
            pipeline_desc_sets_.calc_light_list[1]=desc_set_texel_buffers_;

            pipeline_layouts_.calc_light_list=p_dev_->dev.createPipelineLayout(
                vk::PipelineLayoutCreateInfo({},
                                             static_cast<uint32_t>(desc_set_layouts.size()),
                                             desc_set_layouts.data(),
                                             0, nullptr));
        }

        {
            /* depth pipeline */

            vk::PipelineInputAssemblyStateCreateInfo input_assembly_state(
                {},
                vk::PrimitiveTopology::eTriangleList, // topology
                VK_FALSE // primitive restart enable
            );

            vk::PipelineRasterizationStateCreateInfo rasterization_state(
                {},
                VK_FALSE, // depth clamp enable
                VK_FALSE, // rasterizer discard
                vk::PolygonMode::eFill, // polygon mode
                vk::CullModeFlagBits::eBack, // cull mode
                vk::FrontFace::eCounterClockwise, // front face
                VK_FALSE, // depth bias
                0, 0, 0, 1.f);

            vk::PipelineDepthStencilStateCreateInfo depth_stencil_state(
                {},
                VK_TRUE, // depth test enable
                VK_TRUE, // depth write enable
                vk::CompareOp::eLessOrEqual, // depth compare op
                VK_FALSE, // depth bounds test enable
                VK_FALSE, // stencil test enable
                vk::StencilOpState(),
                vk::StencilOpState(),
                0.f, // min depth bounds
                0.f); // max depth bounds

            vk::PipelineColorBlendStateCreateInfo color_blend_state(
                {},
                VK_FALSE,  // logic op enable
                vk::LogicOp::eClear, // logic op
                0, // attachment count
                nullptr, // attachments
                std::array<float, 4> {1.f, 1.f, 1.f, 1.f} // blend constants
            );

            vk::PipelineMultisampleStateCreateInfo multisample_state(
                {},
                vk::SampleCountFlagBits::e1, // sample count
                VK_FALSE, // sample shading enable
                0.f, // min sample shading
                nullptr, // sample mask
                VK_FALSE, // alpha to coverage enable
                VK_FALSE);// alpha to one enable

            std::vector<vk::DynamicState> dynamic_states=
            {
                vk::DynamicState::eViewport,
                vk::DynamicState::eScissor
            };
            vk::PipelineDynamicStateCreateInfo dynamic_state_ci(
                {},
                2,
                dynamic_states.data());

            vk::PipelineViewportStateCreateInfo viewport_state(
                {},
                1, nullptr,
                1, nullptr);

            vk::PipelineShaderStageCreateInfo shader_stages[2];
            shader_stages[0]=p_simple_vs_->create_pipeline_stage_info();

            vk::PipelineVertexInputStateCreateInfo vertex_input_state(
                {},
                1,
                &p_model_->vi_binding,
                1, //pos
                p_model_->vi_attribs.data());

            vk::GraphicsPipelineCreateInfo pipeline_ci(
                {},
                1, // vs
                shader_stages,
                &vertex_input_state,
                &input_assembly_state,
                nullptr,
                &viewport_state,
                &rasterization_state,
                &multisample_state,
                &depth_stencil_state,
                &color_blend_state,
                &dynamic_state_ci,
                pipeline_layouts_.depth,
                p_offscreen_rp_->rp,
                0); // subpass

            pipelines_.depth=p_dev_->dev.createGraphicsPipeline(
                nullptr, pipeline_ci);


            /* clustering */

            shader_stages[0]=p_clustering_vs_->create_pipeline_stage_info();
            shader_stages[1]=p_clustering_fs_->create_pipeline_stage_info();
            pipeline_ci.stageCount=2;

            color_blend_state.attachmentCount=0;

            depth_stencil_state.depthTestEnable=VK_TRUE;
            depth_stencil_state.depthWriteEnable=VK_FALSE;

            pipeline_ci.layout=pipeline_layouts_.clustering;
            pipeline_ci.renderPass=p_offscreen_rp_->rp;

            pipeline_ci.subpass=1;

            // opaque

            rasterization_state.cullMode=vk::CullModeFlagBits::eBack;

            pipelines_.clustering_opaque=p_dev_->dev.createGraphicsPipeline(
                nullptr, pipeline_ci);

            // transparent

            rasterization_state.cullMode=vk::CullModeFlagBits::eNone;

            pipelines_.clustering_transparent=p_dev_->dev.createGraphicsPipeline(
                nullptr, pipeline_ci);

            /* onscreen */

            vk::PipelineColorBlendAttachmentState blend_attachment_state;
            blend_attachment_state.blendEnable=VK_FALSE;
            blend_attachment_state.colorWriteMask=
                vk::ColorComponentFlagBits::eR |
                vk::ColorComponentFlagBits::eG |
                vk::ColorComponentFlagBits::eB |
                vk::ColorComponentFlagBits::eA;
            blend_attachment_state.colorBlendOp=vk::BlendOp::eAdd;
            blend_attachment_state.srcColorBlendFactor=vk::BlendFactor::eSrcAlpha;
            blend_attachment_state.dstColorBlendFactor=vk::BlendFactor::eOneMinusSrcAlpha;

            blend_attachment_state.alphaBlendOp=vk::BlendOp::eAdd;
            blend_attachment_state.srcAlphaBlendFactor=vk::BlendFactor::eSrcAlpha;
            blend_attachment_state.dstAlphaBlendFactor=vk::BlendFactor::eOneMinusSrcAlpha;

            color_blend_state.attachmentCount=1;
            color_blend_state.pAttachments=&blend_attachment_state;

            /* cluster forward */

            vertex_input_state.vertexAttributeDescriptionCount=static_cast<uint32_t>(p_model_->vi_attribs.size());

            multisample_state.sampleShadingEnable=VK_TRUE;
            multisample_state.rasterizationSamples=vk::SampleCountFlagBits::e4;
            multisample_state.minSampleShading=0.25;

            shader_stages[0]=p_cluster_forward_vs_->create_pipeline_stage_info();
            shader_stages[1]=p_cluster_forward_fs_->create_pipeline_stage_info();

            depth_stencil_state.depthTestEnable=VK_TRUE;

            pipeline_ci.layout=pipeline_layouts_.cluster_forward;
            pipeline_ci.renderPass=p_onscreen_rp_->rp;
            pipeline_ci.subpass=0;

            // opaque

            rasterization_state.cullMode=vk::CullModeFlagBits::eBack;
            blend_attachment_state.blendEnable=VK_FALSE;
            depth_stencil_state.depthWriteEnable=VK_TRUE;

            pipelines_.cluster_forward_opaque=p_dev_->dev.createGraphicsPipeline(
                nullptr, pipeline_ci);

            // transparent

            rasterization_state.cullMode=vk::CullModeFlagBits::eNone;
            blend_attachment_state.blendEnable=VK_TRUE;
            depth_stencil_state.depthWriteEnable=VK_FALSE;

            pipelines_.cluster_forward_transparent=p_dev_->dev.createGraphicsPipeline(
                nullptr, pipeline_ci);

            /* light particles */

            input_assembly_state.topology=vk::PrimitiveTopology::ePointList;

            blend_attachment_state.blendEnable=VK_TRUE;
            depth_stencil_state.depthTestEnable=VK_TRUE;
            depth_stencil_state.depthWriteEnable=VK_TRUE;

            shader_stages[0]=p_light_particles_vs_->create_pipeline_stage_info();
            shader_stages[1]=p_light_particles_fs_->create_pipeline_stage_info();

            std::vector<vk::VertexInputBindingDescription> vi_bindings={
                vk::VertexInputBindingDescription(0, 4 * sizeof(float), vk::VertexInputRate::eVertex),
                vk::VertexInputBindingDescription(1, 4 * sizeof(char), vk::VertexInputRate::eVertex)
            };
            std::vector<vk::VertexInputAttributeDescription> vi_attribs={
                vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32A32Sfloat, 0),
                vk::VertexInputAttributeDescription(1, 1, vk::Format::eR8G8B8A8Unorm, 0)
            };
            vertex_input_state.vertexBindingDescriptionCount=static_cast<uint32_t>(vi_bindings.size());
            vertex_input_state.pVertexBindingDescriptions=vi_bindings.data();
            vertex_input_state.vertexAttributeDescriptionCount=static_cast<uint32_t>(vi_attribs.size());
            vertex_input_state.pVertexAttributeDescriptions=vi_attribs.data();

            pipeline_ci.layout=pipeline_layouts_.light_particles;
            pipelines_.light_particles=p_dev_->dev.createGraphicsPipeline(nullptr, pipeline_ci);

            /* text overlay */

            input_assembly_state.topology=vk::PrimitiveTopology::eTriangleList;

            depth_stencil_state.depthTestEnable=VK_FALSE;
            depth_stencil_state.depthWriteEnable=VK_FALSE;

            shader_stages[0]=p_text_overlay_->p_vs->create_pipeline_stage_info();
            shader_stages[1]=p_text_overlay_->p_fs->create_pipeline_stage_info();

            vi_bindings[0]=vk::VertexInputBindingDescription(0, 4 * sizeof(float), vk::VertexInputRate::eVertex);
            vi_attribs[0]=vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32A32Sfloat, 0);
            vertex_input_state.vertexBindingDescriptionCount=1;
            vertex_input_state.pVertexBindingDescriptions=vi_bindings.data();
            vertex_input_state.vertexAttributeDescriptionCount=1;
            vertex_input_state.pVertexAttributeDescriptions=vi_attribs.data();

            pipeline_ci.layout=pipeline_layouts_.text_overlay;
            pipelines_.text_overlay=p_dev_->dev.createGraphicsPipeline(nullptr, pipeline_ci);

            /* compute */

            pipelines_.calc_light_grids=p_dev_->dev.createComputePipeline(
                nullptr, vk::ComputePipelineCreateInfo({},
                                                       p_calc_light_grids_->create_pipeline_stage_info(),
                                                       pipeline_layouts_.calc_light_grids));

            pipelines_.calc_grid_offsets=p_dev_->dev.createComputePipeline(
                nullptr, vk::ComputePipelineCreateInfo({},
                                                       p_calc_grid_offsets_->create_pipeline_stage_info(),
                                                       pipeline_layouts_.calc_grid_offsets));

            pipelines_.calc_light_list=p_dev_->dev.createComputePipeline(
                nullptr, vk::ComputePipelineCreateInfo({},
                                                       p_calc_light_list_->create_pipeline_stage_info(),
                                                       pipeline_layouts_.calc_light_list));

        }
    }

    void destroy_pipelines_()
    {
        p_dev_->dev.destroyPipelineLayout(pipeline_layouts_.depth);
        p_dev_->dev.destroyPipelineLayout(pipeline_layouts_.clustering);
        p_dev_->dev.destroyPipelineLayout(pipeline_layouts_.calc_light_grids);
        p_dev_->dev.destroyPipelineLayout(pipeline_layouts_.calc_grid_offsets);
        p_dev_->dev.destroyPipelineLayout(pipeline_layouts_.calc_light_list);
        p_dev_->dev.destroyPipelineLayout(pipeline_layouts_.cluster_forward);
        p_dev_->dev.destroyPipelineLayout(pipeline_layouts_.light_particles);
        p_dev_->dev.destroyPipelineLayout(pipeline_layouts_.text_overlay);
        p_dev_->dev.destroyPipeline(pipelines_.depth);
        p_dev_->dev.destroyPipeline(pipelines_.clustering_opaque);
        p_dev_->dev.destroyPipeline(pipelines_.clustering_transparent);
        p_dev_->dev.destroyPipeline(pipelines_.light_particles);
        p_dev_->dev.destroyPipeline(pipelines_.calc_light_grids);
        p_dev_->dev.destroyPipeline(pipelines_.calc_grid_offsets);
        p_dev_->dev.destroyPipeline(pipelines_.calc_light_list);
        p_dev_->dev.destroyPipeline(pipelines_.cluster_forward_opaque);
        p_dev_->dev.destroyPipeline(pipelines_.cluster_forward_transparent);
        p_dev_->dev.destroyPipeline(pipelines_.text_overlay);
    }

    // ************************************************************************
    // on frame
    // ************************************************************************

    void update_global_uniforms_(Frame_data &data)
    {
        /*
        glm::mat4 view;
        glm::mat4 normal;
        glm::mat4 model;
        glm::mat4 projection_clip;

        glm::vec2 tile_size;
        uint32_t grid_dim[2]; // tile count

        glm::vec3 cam_pos;
        float cam_far;

        glm::vec2 resolution;
        uint32_t num_lights;
        */

        // update host data
        global_uniforms_.view=p_camera_->view;
        global_uniforms_.normal=p_model_->normal_matrix;
        global_uniforms_.model=p_model_->model_matrix;
        global_uniforms_.projection_clip=p_camera_->clip * p_camera_->projection;

        global_uniforms_.tile_size[0]=static_cast<float>(p_info_->TILE_WIDTH);
        global_uniforms_.tile_size[1]=static_cast<float>(p_info_->TILE_HEIGHT);
        global_uniforms_.grid_dim[0]=p_info_->tile_count_x;
        global_uniforms_.grid_dim[1]=p_info_->tile_count_y;

        global_uniforms_.cam_pos=p_camera_->eye_pos;
        global_uniforms_.cam_far=p_camera_->cam_far;

        global_uniforms_.resolution[0]=static_cast<float>(p_info_->width());
        global_uniforms_.resolution[1]=static_cast<float>(p_info_->height());
        global_uniforms_.num_lights=p_info_->num_lights;

        // memcpy to host visible memory
        auto mapped=reinterpret_cast<Global_uniforms *>(data.p_global_uniforms->mapped);
        memcpy(mapped, &global_uniforms_, sizeof(Global_uniforms));
    }

    void update_light_buffers_(float elapsed_time, Frame_data &data)
    {
        // update host data
        for (auto i=0; i < p_info_->num_lights; i++) {
            lights_[i].update(elapsed_time);
            glm::vec4 tmp={lights_[i].position, lights_[i].range};
            memcpy(p_light_position_ranges_ + i * 4, glm::value_ptr(tmp), 4 * sizeof(float));
            memcpy(p_light_colors_ + i * 4, glm::value_ptr(lights_[i].color), 4 * sizeof(char));
        }

        // memcpy to host visible memory
        {
            auto mapped=reinterpret_cast<float *>(data.p_light_pos_ranges->p_buf->mapped);
            memcpy(mapped, p_light_position_ranges_, 4 * sizeof(float) * p_info_->num_lights);
        }
        {
            auto mapped=reinterpret_cast<char *>(data.p_light_colors->p_buf->mapped);
            memcpy(mapped, p_light_colors_, 4 * sizeof(char) * p_info_->num_lights);
        }
    }

    void acquire_back_buffer_() override
    {
        auto &back=back_buffers_.front();

        p_dev_->dev.waitForFences(1, &back.present_queue_submit_fence, VK_TRUE, UINT64_MAX);
        p_dev_->dev.resetFences(1, &back.present_queue_submit_fence);

        detect_window_resize_();

        vk::Result res=vk::Result::eTimeout;
        while (res != vk::Result::eSuccess) {

            res=p_dev_->dev.acquireNextImageKHR(
                p_swapchain_->swapchain,
                UINT64_MAX,
                back.swapchain_image_acquire_semaphore,
                vk::Fence(),
                &back.swapchain_image_idx);
            if (res == vk::Result::eErrorOutOfDateKHR) {
                p_swapchain_->resize(0, 0);
                p_shell_->post_quit_msg();
            }
            else {
                assert(res == vk::Result::eSuccess);
            }
        }

        acquired_back_buf_=back;
        back_buffers_.pop_front();
    }

    void present_back_buffer_(float elapsed_time, float delta_time) override
    {
        on_frame_(elapsed_time, delta_time);

        auto &back=acquired_back_buf_;
        vk::PresentInfoKHR present_info(1, &back.onscreen_render_semaphore,
                                        1, &p_swapchain_->swapchain,
                                        &back.swapchain_image_idx);
        p_dev_->present_queue.presentKHR(present_info);
        p_dev_->present_queue.submit(0, nullptr, back.present_queue_submit_fence);

        back_buffers_.push_back(back);
    }

    void detect_window_resize_() const
    {
        if (p_info_->resize_flag) {
            p_info_->resize_flag=false;
            p_swapchain_->resize(p_info_->width(), p_info_->height());
        }
    }

    base::FPS_log text_overlay_update_counter_{60};
    std::string text_overlay_content_;

    void generate_text_(Frame_data &data, std::string &text)
    {
        std::stringstream ss;
        uint32_t depth=data.query_data.depth_pass[1] - data.query_data.depth_pass[0];
        uint32_t clustering=data.query_data.clustering[1] - data.query_data.clustering[0];
        uint32_t compute_flags=data.query_data.compute_flags[1] - data.query_data.compute_flags[0];
        uint32_t compute_offsets=data.query_data.compute_offsets[1] - data.query_data.compute_offsets[0];
        uint32_t compute_list=data.query_data.compute_list[1] - data.query_data.compute_list[0];
        uint32_t onscreen=data.query_data.onscreen[1] - data.query_data.onscreen[0];
        uint32_t transfer=data.query_data.transfer[1] - data.query_data.transfer[0];
        ss << p_phy_dev_->props.deviceName << "\n" <<
            "resolution: " << std::to_string(p_info_->width()) << "x" << std::to_string(p_info_->height()) << "\n\n" <<
            "query data (in ms)\n" <<
            "------------------\n" <<
            "subpass depth: " << timestamp_to_str(depth) << "\n" <<
            "subpass clustering: " << timestamp_to_str(clustering) << "\n" <<
            "compute grid_flags: " << timestamp_to_str(compute_flags) << "\n" <<
            "compute light_offsets: " << timestamp_to_str(compute_offsets) << "\n" <<
            "compute light_list: " << timestamp_to_str(compute_list) << "\n" <<
            "subpass scene, particles, text (4xMSAA): " << timestamp_to_str(onscreen) << "\n" <<
            "transfer: " << timestamp_to_str(transfer) << "\n" <<
            "total: " << timestamp_to_str(depth + clustering + compute_flags + compute_offsets + compute_list + onscreen + transfer);
        text=ss.str();
    }

    void on_frame_(float elapsed_time, float delta_time)
    {
        const vk::DeviceSize vb_offset{0};
        vk::BufferMemoryBarrier barriers[2];

        auto &data=frame_data_vec_[frame_data_idx_];
        auto &back=acquired_back_buf_;

        bool update_text=text_overlay_update_counter_.silent_update(delta_time);

        // offscreen 
        {
            base::assert_success(p_dev_->dev.waitForFences(1,
                                                           &data.offscreen_cmd_buf_blk.submit_fence,
                                                           VK_TRUE,
                                                           UINT64_MAX));
            p_dev_->dev.resetFences(1, &data.offscreen_cmd_buf_blk.submit_fence);

            update_global_uniforms_(data);
            if (update_text) {
                generate_text_(data, text_overlay_content_);
                p_text_overlay_->update_text(text_overlay_content_, 0.1, 0.1, 12, 1.f / p_info_->height());
            }

            auto &cmd_buf=data.offscreen_cmd_buf_blk.cmd_buffer;
            cmd_buf.begin(cmd_begin_info_);

            cmd_buf.resetQueryPool(data.query_pool, 0, 4);

            // host write to shader read
            barriers[0]={
                vk::AccessFlagBits::eHostWrite,
                vk::AccessFlagBits::eShaderRead,
                VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED,
                data.p_global_uniforms->buf,
                0, VK_WHOLE_SIZE
            };
            cmd_buf.pipelineBarrier(vk::PipelineStageFlagBits::eHost,
                                    vk::PipelineStageFlagBits::eVertexShader,
                                    vk::DependencyFlagBits::eByRegion,
                                    0, nullptr, 1, barriers, 0, nullptr);

            // offscreen framebuffer size is set to MAX_WIDHT and MAX_HEIGHT
            // only update current extent
            offscreen_viewport_.width=p_info_->width();
            offscreen_viewport_.height=p_info_->height();
            offscreen_scissor_.extent.width=p_info_->width();
            offscreen_scissor_.extent.height=p_info_->height();
            cmd_buf.setViewport(0, 1, &offscreen_viewport_);
            cmd_buf.setScissor(0, 1, &offscreen_scissor_);

            p_offscreen_rp_->rp_begin.renderArea.extent.width=p_info_->width();
            p_offscreen_rp_->rp_begin.renderArea.extent.height=p_info_->height();
            cmd_buf.beginRenderPass(&p_offscreen_rp_->rp_begin, vk::SubpassContents::eInline);

            // depth
            {
                cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, data.query_pool, QUERY_DEPTH_PASS * 2);

                cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                           pipeline_layouts_.depth,
                                           0, 1, &data.desc_set,
                                           0, nullptr);
                cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                     pipelines_.depth);
                cmd_buf.bindVertexBuffers(p_model_->vi_bind_id, 1,
                                          &p_model_->p_vert_buffer->buf,
                                          &vb_offset);
                cmd_buf.bindIndexBuffer(p_model_->p_idx_buffer->buf,
                                        0,
                                        vk::IndexType::eUint32);
                for (auto &part : p_model_->scene_parts) {
                    // only draw opaque
                    if (part.p_mtl->properties.alpha == 1.f) {
                        cmd_buf.drawIndexed(part.idx_count,
                                            1, part.idx_base, part.vert_offset, 0);
                    }
                }

                cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eFragmentShader, data.query_pool, QUERY_DEPTH_PASS * 2 + 1);
            }

            cmd_buf.nextSubpass(vk::SubpassContents::eInline);

            // clustering
            {
                cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, data.query_pool, QUERY_CLUSTERING * 2);

                pipeline_desc_sets_.clustering[0]=data.desc_set;
                cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                           pipeline_layouts_.clustering,
                                           0, static_cast<uint32_t>(pipeline_desc_sets_.clustering.size()),
                                           pipeline_desc_sets_.clustering.data(),
                                           0, nullptr);

                cmd_buf.bindVertexBuffers(p_model_->vi_bind_id, 1,
                                          &p_model_->p_vert_buffer->buf,
                                          &vb_offset);
                cmd_buf.bindIndexBuffer(p_model_->p_idx_buffer->buf,
                                        0,
                                        vk::IndexType::eUint32);

                // scene parts have been sorted
                // the opaque are drawn first
                for (auto &part : p_model_->scene_parts) {
                    if (part.p_mtl->properties.alpha < 1.f) {
                        cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                             pipelines_.clustering_transparent);
                    }
                    else {
                        cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                             pipelines_.clustering_opaque);
                    }
                    cmd_buf.drawIndexed(part.idx_count,
                                        1, part.idx_base, part.vert_offset, 0);
                }

                cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eFragmentShader, data.query_pool, QUERY_CLUSTERING * 2 + 1);
            }

            cmd_buf.endRenderPass();
            cmd_buf.end();

            auto &submit_info=offscreen_cmd_submit_info_;
            submit_info.pCommandBuffers=&cmd_buf;
            submit_info.pWaitSemaphores=&back.swapchain_image_acquire_semaphore;
            submit_info.pSignalSemaphores=&back.pre_compute_render_semaphore;
            base::assert_success(p_dev_->graphics_queue
                                 .submit(1, &submit_info, data.offscreen_cmd_buf_blk.submit_fence));
        }

        base::assert_success(vkGetQueryPoolResults(static_cast<VkDevice>(p_dev_->dev),
                                                   static_cast<VkQueryPool>(data.query_pool),
                                                   0, 4,
                                                   sizeof(uint32_t) * 4,
                                                   &data.query_data,
                                                   sizeof(uint32_t),
                                                   static_cast<VkQueryResultFlagBits>(vk::QueryResultFlagBits::eWait)));

        // compute
        {
            base::assert_success(p_dev_->dev.waitForFences(1,
                                                           &data.compute_cmd_buf_blk.submit_fence,
                                                           VK_TRUE,
                                                           UINT64_MAX));
            p_dev_->dev.resetFences(1, &data.compute_cmd_buf_blk.submit_fence);

            update_light_buffers_(elapsed_time, data);

            auto &cmd_buf=data.compute_cmd_buf_blk.cmd_buffer;
            cmd_buf.begin(cmd_begin_info_);

            cmd_buf.resetQueryPool(data.query_pool, 4, 6);

            barriers[0]={
                vk::AccessFlagBits::eHostWrite,
                vk::AccessFlagBits::eShaderRead,
                VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED,
                data.p_light_pos_ranges->p_buf->buf,
                0, VK_WHOLE_SIZE
            };
            cmd_buf.pipelineBarrier(vk::PipelineStageFlagBits::eHost,
                                    vk::PipelineStageFlagBits::eComputeShader,
                                    vk::DependencyFlagBits::eByRegion,
                                    0, nullptr, 1, barriers, 0, nullptr);

            // --------------------- calc light grids ---------------------

            // reads grid_flags, light_pos_ranges
            // writes light_bounds, grid_light_counts

            cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, data.query_pool, QUERY_COMPUTE_FLAGS * 2);

            cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, pipelines_.calc_light_grids);
            pipeline_desc_sets_.calc_light_grids[0]=data.desc_set;
            cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                       pipeline_layouts_.calc_light_grids,
                                       0, static_cast<uint32_t>(pipeline_desc_sets_.calc_light_grids.size()),
                                       pipeline_desc_sets_.calc_light_grids.data(),
                                       0, nullptr);
            cmd_buf.dispatch((p_info_->num_lights - 1) / 32 + 1, 1, 1);

            cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eComputeShader, data.query_pool, QUERY_COMPUTE_FLAGS * 2 + 1);

            barriers[0]=vk::BufferMemoryBarrier(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
                                                vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
                                                VK_QUEUE_FAMILY_IGNORED,
                                                VK_QUEUE_FAMILY_IGNORED,
                                                p_light_bounds_->p_buf->buf,
                                                0, VK_WHOLE_SIZE);
            barriers[1]=barriers[0];
            barriers[1].buffer=p_grid_light_counts_->p_buf->buf;
            cmd_buf.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                                    vk::PipelineStageFlagBits::eComputeShader,
                                    vk::DependencyFlagBits::eByRegion,
                                    0, nullptr, 2, barriers, 0, nullptr);

            // --------------------- calc grid offsets ---------------------

            // reads grid_flags, grid_light_counts
            // writes grid_light_count_total, grid_light_offsets

            cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, data.query_pool, QUERY_COMPUTE_OFFSETS * 2);

            cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, pipelines_.calc_grid_offsets);
            pipeline_desc_sets_.calc_grid_offsets[0]=data.desc_set;
            cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                       pipeline_layouts_.calc_grid_offsets,
                                       0, static_cast<uint32_t>(pipeline_desc_sets_.calc_grid_offsets.size()),
                                       pipeline_desc_sets_.calc_grid_offsets.data(),
                                       0, nullptr);
            cmd_buf.dispatch((p_info_->tile_count_x - 1) / 16 + 1, (p_info_->tile_count_y - 1) / 16 + 1, p_info_->TILE_COUNT_Z);

            cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eComputeShader, data.query_pool, QUERY_COMPUTE_OFFSETS * 2 + 1);

            barriers[0].buffer=p_grid_light_count_total_->p_buf->buf;
            barriers[1].buffer=p_grid_light_count_offsets_->p_buf->buf;
            cmd_buf.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                                    vk::PipelineStageFlagBits::eComputeShader,
                                    vk::DependencyFlagBits::eByRegion,
                                    0, nullptr, 1, barriers, 0, nullptr);

            // --------------------- calc light list ---------------------

            // reads grid_flags, light_bounds, grid_light_counts, grid_light_offsets
            // writes grid_light_counts_compare, light_list

            cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, data.query_pool, QUERY_COMPUTE_LIST * 2);

            cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, pipelines_.calc_light_list);
            pipeline_desc_sets_.calc_light_list[0]=data.desc_set;
            cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                       pipeline_layouts_.calc_light_list,
                                       0, static_cast<uint32_t>(pipeline_desc_sets_.calc_light_list.size()),
                                       pipeline_desc_sets_.calc_light_list.data(),
                                       0, nullptr);
            cmd_buf.dispatch((p_info_->num_lights - 1) / 32 + 1, 1, 1);

            cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eFragmentShader, data.query_pool, QUERY_COMPUTE_LIST * 2 + 1);

            cmd_buf.end();
            compute_cmd_submit_info_.pCommandBuffers=&cmd_buf;
            compute_cmd_submit_info_.pWaitSemaphores=&back.pre_compute_render_semaphore;
            compute_cmd_submit_info_.pSignalSemaphores=&back.compute_semaphore;
            base::assert_success(p_dev_->compute_queue.submit(
                1, &compute_cmd_submit_info_, data.compute_cmd_buf_blk.submit_fence));
        }

        base::assert_success(vkGetQueryPoolResults(static_cast<VkDevice>(p_dev_->dev),
                                                   static_cast<VkQueryPool>(data.query_pool),
                                                   4, 6,
                                                   sizeof(uint32_t) * 6,
                                                   &data.query_data.compute_flags[0],
                                                   sizeof(uint32_t),
                                                   static_cast<VkQueryResultFlagBits>(vk::QueryResultFlagBits::eWait)));

        // onscreen
        {
            base::assert_success(p_dev_->dev.waitForFences(1,
                                                           &data.onscreen_cmd_buf_blk.submit_fence,
                                                           VK_TRUE,
                                                           UINT64_MAX));
            p_dev_->dev.resetFences(1, &data.onscreen_cmd_buf_blk.submit_fence);

            auto &cmd_buf=data.onscreen_cmd_buf_blk.cmd_buffer;
            cmd_buf.begin(cmd_begin_info_);

            cmd_buf.resetQueryPool(data.query_pool, 10, 4);

            // render pass
            {
                cmd_buf.setViewport(0, 1, &p_swapchain_->onscreen_viewport);
                cmd_buf.setScissor(0, 1, &p_swapchain_->onscreen_scissor);

                p_onscreen_rp_->rp_begin.framebuffer=p_swapchain_->framebuffers[back.swapchain_image_idx];
                p_onscreen_rp_->rp_begin.renderArea.extent=p_swapchain_->curr_extent();

                cmd_buf.beginRenderPass(p_onscreen_rp_->rp_begin, vk::SubpassContents::eInline);

                cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, data.query_pool, QUERY_ONSCREEN * 2);

                // draw scene
                {
                    cmd_buf.bindVertexBuffers(0, 1, &p_model_->p_vert_buffer->buf, &vb_offset);
                    cmd_buf.bindIndexBuffer(p_model_->p_idx_buffer->buf, 0, vk::IndexType::eUint32);

                    pipeline_desc_sets_.cluster_forward[2]=data.desc_set;

                    // scene parts have been sorted
                    // the opaque are drawn first
                    for (auto &part : p_model_->scene_parts) {
                        if (part.p_mtl->properties.alpha < 1.f) {
                            cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                                 pipelines_.cluster_forward_transparent);
                        }
                        else {
                            cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                                 pipelines_.cluster_forward_opaque);
                        }
                        pipeline_desc_sets_.cluster_forward[1]=part.p_mtl->desc_set_sampler;
                        cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                                   pipeline_layouts_.cluster_forward,
                                                   0, static_cast<uint32_t>(pipeline_desc_sets_.cluster_forward.size()),
                                                   pipeline_desc_sets_.cluster_forward.data(),
                                                   1, &part.p_mtl->dynamic_offset);
                        cmd_buf.drawIndexed(part.idx_count, 1, part.idx_base, part.vert_offset, 0);
                    }
                }

                // draw light particles
                {
                    pipeline_desc_sets_.light_particles[0]=data.desc_set;
                    cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                               pipeline_layouts_.light_particles,
                                               0, static_cast<uint32_t>(pipeline_desc_sets_.light_particles.size()),
                                               pipeline_desc_sets_.light_particles.data(),
                                               0, nullptr);
                    cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                         pipelines_.light_particles);
                    cmd_buf.bindVertexBuffers(0, 1,
                                              &data.p_light_pos_ranges->p_buf->buf,
                                              &vb_offset);
                    cmd_buf.bindVertexBuffers(1, 1,
                                              &data.p_light_colors->p_buf->buf,
                                              &vb_offset);
                    cmd_buf.draw(p_info_->num_lights, 1, 0, 0);
                }

                // draw text
                {
                    cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                               pipeline_layouts_.text_overlay,
                                               0, 1, &desc_set_font_tex_,
                                               0, nullptr);
                    cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                         pipelines_.text_overlay);
                    cmd_buf.bindVertexBuffers(0, 1,
                                              &p_text_overlay_->p_vert_buf->buf,
                                              &vb_offset);
                    cmd_buf.bindIndexBuffer(p_text_overlay_->p_idx_buf->buf, 0, vk::IndexType::eUint32);
                    cmd_buf.drawIndexed(p_text_overlay_->draw_index_count, 1, 0, 0, 0);
                }

                cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eColorAttachmentOutput, data.query_pool, QUERY_ONSCREEN * 2 + 1);

                cmd_buf.endRenderPass();
            }

            // clean up buffers
            {
                std::vector<vk::BufferMemoryBarrier> transfer_barriers{4,
                    vk::BufferMemoryBarrier(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
                                            vk::AccessFlagBits::eTransferWrite,
                                            VK_QUEUE_FAMILY_IGNORED,
                                            VK_QUEUE_FAMILY_IGNORED,
                                            p_grid_flags_->p_buf->buf,
                                            0, VK_WHOLE_SIZE)};
                transfer_barriers[1].buffer=p_grid_light_counts_->p_buf->buf;
                transfer_barriers[2].buffer=p_grid_light_count_offsets_->p_buf->buf;
                transfer_barriers[3].buffer=p_light_list_->p_buf->buf;

                cmd_buf.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader,
                                        vk::PipelineStageFlagBits::eTransfer,
                                        vk::DependencyFlagBits::eByRegion,
                                        0, nullptr,
                                        static_cast<uint32_t>(transfer_barriers.size()),
                                        transfer_barriers.data(),
                                        0, nullptr);

                cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, data.query_pool, QUERY_TRANSFER * 2);

                cmd_buf.fillBuffer(p_grid_flags_->p_buf->buf,
                                   0, VK_WHOLE_SIZE,
                                   0);
                cmd_buf.fillBuffer(p_light_bounds_->p_buf->buf,
                                   0, VK_WHOLE_SIZE,
                                   0);
                cmd_buf.fillBuffer(p_grid_light_counts_->p_buf->buf,
                                   0, VK_WHOLE_SIZE,
                                   0);
                cmd_buf.fillBuffer(p_grid_light_count_offsets_->p_buf->buf,
                                   0, VK_WHOLE_SIZE,
                                   0);
                cmd_buf.fillBuffer(p_grid_light_count_total_->p_buf->buf,
                                   0, VK_WHOLE_SIZE,
                                   0);
                cmd_buf.fillBuffer(p_light_list_->p_buf->buf,
                                   0, VK_WHOLE_SIZE,
                                   0);
                cmd_buf.fillBuffer(p_grid_light_counts_compare_->p_buf->buf,
                                   0, VK_WHOLE_SIZE,
                                   0);

                cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eTransfer, data.query_pool, QUERY_TRANSFER * 2 + 1);

                transfer_barriers.clear();
                transfer_barriers.resize(4,
                                         vk::BufferMemoryBarrier(vk::AccessFlagBits::eTransferWrite,
                                                                 vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
                                                                 VK_QUEUE_FAMILY_IGNORED,
                                                                 VK_QUEUE_FAMILY_IGNORED,
                                                                 p_grid_flags_->p_buf->buf,
                                                                 0, VK_WHOLE_SIZE));
                transfer_barriers[1].buffer=p_grid_light_counts_->p_buf->buf;
                transfer_barriers[2].buffer=p_grid_light_count_offsets_->p_buf->buf;
                transfer_barriers[3].buffer=p_light_list_->p_buf->buf;
                cmd_buf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                        vk::PipelineStageFlagBits::eFragmentShader,
                                        vk::DependencyFlagBits::eByRegion,
                                        0, nullptr,
                                        static_cast<uint32_t>(transfer_barriers.size()),
                                        transfer_barriers.data(),
                                        0, nullptr);
            }

            cmd_buf.end();

            auto &submit_info=onscreen_cmd_submit_info_;
            submit_info.pCommandBuffers=&cmd_buf;
            submit_info.pWaitSemaphores=&back.compute_semaphore;
            submit_info.pSignalSemaphores=&back.onscreen_render_semaphore;

            base::assert_success(p_dev_->graphics_queue.submit(
                1,
                &submit_info,
                data.onscreen_cmd_buf_blk.submit_fence));
        }

        base::assert_success(vkGetQueryPoolResults(static_cast<VkDevice>(p_dev_->dev),
                                                   static_cast<VkQueryPool>(data.query_pool),
                                                   10, 4,
                                                   sizeof(uint32_t) * 4,
                                                   &data.query_data.onscreen[0],
                                                   sizeof(uint32_t),
                                                   static_cast<VkQueryResultFlagBits>(vk::QueryResultFlagBits::eWait)));

        frame_data_idx_=(frame_data_idx_ + 1) % frame_data_count_;
    }
};
