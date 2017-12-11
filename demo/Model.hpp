#pragma once
#include <Model.hpp>
#include <Texture.hpp>
#define MSG_PREFIX "-- MODEL: "

struct Material_properties
{
    glm::vec3 ambient;
    float padding{0.f};

    glm::vec3 diffuse;
    float alpha;

    glm::vec3 specular;
    float specular_exponent;

    glm::vec3 emissive;
};

class Material_texture2D : public base::Texture2D
{
public:
    Material_texture2D(base::Physical_device *p_phy_dev,
                       base::Device *p_dev,
                       const std::string file_path,
                       vk::CommandPool &graphics_cmd_pool,
                       vk::Format format)
        : base::Texture2D(p_phy_dev, p_dev)
    {

        load(file_path,
             graphics_cmd_pool,
             format,
             vk::ImageUsageFlagBits::eSampled,
             vk::ImageLayout::eShaderReadOnlyOptimal,
             false);

        sampler=p_dev_->dev.createSampler(
            vk::SamplerCreateInfo({},
                                  vk::Filter::eNearest, vk::Filter::eNearest,
                                  vk::SamplerMipmapMode::eNearest,
                                  vk::SamplerAddressMode::eRepeat,
                                  vk::SamplerAddressMode::eRepeat,
                                  vk::SamplerAddressMode::eRepeat,
                                  0.f, 0, 1.f, 0, vk::CompareOp::eNever, 0.f, 1.f,
                                  vk::BorderColor::eFloatOpaqueWhite, 0));

        std::cout << MSG_PREFIX << file_path << " loaded" << std::endl;
    }
};

class Material
{
public:
    std::string name;
    Material_properties properties;

    std::shared_ptr<Material_texture2D> p_diffuse_map;
    std::shared_ptr<Material_texture2D> p_opacity_map;
    std::shared_ptr<Material_texture2D> p_specular_map;
    std::shared_ptr<Material_texture2D> p_normal_map;

    vk::DescriptorSet desc_set_sampler;
    std::vector<vk::DescriptorImageInfo> desc_image_info;
    uint32_t dynamic_offset;
};

struct Scene_part
{
    Material *p_mtl;
    uint32_t idx_base;
    uint32_t idx_count;
    int32_t vert_offset;
    //    uint32_t render_flags;
    Scene_part(Material *p_mtl, uint32_t idx_base, uint32_t idx_count, uint32_t vert_offset)
        : p_mtl(p_mtl), idx_base(idx_base), idx_count(idx_count), vert_offset(vert_offset)
    {}
};


class Model : public base::Model
{
public:
    std::string asset_path_;
    std::string dummy_asset_path_;

    std::vector<Scene_part> scene_parts;
    std::vector<Material> materials;

    vk::DescriptorPool desc_pool_;
    vk::DescriptorSetLayout desc_set_layout_sampler;
    vk::DescriptorSetLayout desc_set_layout_uniform;
    vk::DescriptorSet desc_set_uniform;

    using base::Model::Model;
    ~Model() override
    {
        p_dev_->dev.destroyDescriptorPool(desc_pool_);
        p_dev_->dev.destroyDescriptorSetLayout(desc_set_layout_sampler);
        p_dev_->dev.destroyDescriptorSetLayout(desc_set_layout_uniform);
        materials.clear();
        scene_parts.clear();
        p_dev_->dev.freeMemory(uniform_buf_mem_);
        delete p_uniform_buffer_;
    }

    void load(std::string &model_path,
              base::Vertex_layout &layout,
              std::string &asset_path,
              std::string &dummy_asset_path,
              float scale=1.f,
              glm::vec3 xlate=glm::vec3(0.f))
    {

        asset_path_=asset_path;
        dummy_asset_path_=dummy_asset_path;
        int ai_flags=aiProcess_GenUVCoords |
            aiProcess_CalcTangentSpace;
        base::Model::load(model_path, layout, scale, xlate, ai_flags);
    }
private:
    std::shared_ptr<Material_texture2D> p_dummy_tex_{nullptr};
    std::shared_ptr<Material_texture2D> p_dummy_normal_tex_{nullptr};
    std::string dummy_tex_filename_{"dummy_rgba_unorm.ktx"};
    std::string dummy_normal_tex_filename_{"dummy_normal.ktx"};

    // uniform buffer
    base::Buffer *p_uniform_buffer_{nullptr};
    vk::DeviceMemory uniform_buf_mem_;
    vk::DescriptorBufferInfo desc_buffer_info_;

    void load_materials_(const aiScene *p_scene,
                         const std::vector<vk::CommandBuffer> &cmd_buffers) override
    {
        /* load materials */

        std::cout << MSG_PREFIX << "material count: " << p_scene->mNumMaterials << std::endl;
        materials.reserve(p_scene->mNumMaterials);

        vk::Format tex_format{vk::Format::eR8G8B8Unorm}; // uncompressed
        vk::Format dummy_tex_format{vk::Format::eR8G8B8A8Unorm};

        // check compressed tex format support
        std::string tex_format_suffix;
        bool has_compression=false;
        if (p_phy_dev_->req_features.textureCompressionBC) {
            tex_format=vk::Format::eBc3UnormBlock;
            tex_format_suffix="_bc3_unorm";
            std::cout << MSG_PREFIX << "using BC3 texture compression" << std::endl;
            has_compression=true;
        }
        else if (p_phy_dev_->req_features.textureCompressionETC2) {
            tex_format=vk::Format::eEtc2R8G8B8UnormBlock;
            tex_format_suffix="_etc2_unorm";
            std::cout << MSG_PREFIX << "using ETC2 texture compression" << std::endl;
            has_compression=true;
        }
        else {
            std::cout << MSG_PREFIX << "no texture compression" << std::endl;
        }

        // prepare for dynamic uniform offset
        vk::DeviceSize uniform_buffer_aligned_size=sizeof(Material_properties);
        const VkDeviceSize &alignment=p_phy_dev_->props.limits.minUniformBufferOffsetAlignment;
        base::align_size(uniform_buffer_aligned_size, alignment);

        // calculate paddings of floats in uniform_data
        assert((uniform_buffer_aligned_size - sizeof(Material_properties)) % sizeof(float) == 0);
        int padding_count=
            static_cast<int>((uniform_buffer_aligned_size - sizeof(Material_properties)) / sizeof(float));
        std::vector<float> padding(padding_count, 0);

        // prepare to push in all material properties
        uint32_t uniform_data_device_size=p_scene->mNumMaterials * (sizeof(Material_properties) + padding_count * sizeof(float));
        auto *p_uniform_data=reinterpret_cast<float *>(malloc(uniform_data_device_size));
        int ptr_offset=0;
        materials.resize(p_scene->mNumMaterials);
        for (size_t i=0; i < p_scene->mNumMaterials; i++) {
            auto p_m=p_scene->mMaterials[i];

            aiString name;
            p_m->Get(AI_MATKEY_NAME, name);
            materials[i].name=name.C_Str();
            std::cout << MSG_PREFIX << "loading material: " << materials[i].name << std::endl;

            aiColor4D color;
            p_m->Get(AI_MATKEY_COLOR_AMBIENT, color);
            materials[i].properties.ambient={color.r, color.g, color.b};
            p_m->Get(AI_MATKEY_COLOR_DIFFUSE, color);
            materials[i].properties.diffuse={color.r, color.g, color.b};
            p_m->Get(AI_MATKEY_COLOR_SPECULAR, color);
            materials[i].properties.specular={color.r, color.g, color.b};
            p_m->Get(AI_MATKEY_COLOR_EMISSIVE, color);
            materials[i].properties.emissive={color.r, color.g, color.b};
            p_m->Get(AI_MATKEY_OPACITY, materials[i].properties.alpha);
            p_m->Get(AI_MATKEY_SHININESS, materials[i].properties.specular_exponent);

            setup_material_texture_(p_m, aiTextureType_DIFFUSE,
                                    materials[i].p_diffuse_map,
                                    p_dummy_tex_,
                                    has_compression,
                                    tex_format_suffix,
                                    dummy_tex_filename_,
                                    tex_format,
                                    dummy_tex_format);
            setup_material_texture_(p_m, aiTextureType_OPACITY,
                                    materials[i].p_opacity_map,
                                    p_dummy_tex_,
                                    has_compression,
                                    tex_format_suffix,
                                    dummy_tex_filename_,
                                    tex_format,
                                    dummy_tex_format);
            setup_material_texture_(p_m, aiTextureType_SPECULAR,
                                    materials[i].p_specular_map,
                                    p_dummy_tex_,
                                    has_compression,
                                    tex_format_suffix,
                                    dummy_tex_filename_,
                                    tex_format,
                                    dummy_tex_format);
            setup_material_texture_(p_m, aiTextureType_NORMALS,
                                    materials[i].p_normal_map,
                                    p_dummy_normal_tex_,
                                    has_compression,
                                    tex_format_suffix,
                                    dummy_normal_tex_filename_,
                                    tex_format,
                                    dummy_tex_format);

            // desc buffer and image info
            materials[i].desc_image_info.emplace_back(materials[i].p_diffuse_map->sampler,
                                                      materials[i].p_diffuse_map->view,
                                                      vk::ImageLayout::eShaderReadOnlyOptimal);
            materials[i].desc_image_info.emplace_back(materials[i].p_opacity_map->sampler,
                                                      materials[i].p_opacity_map->view,
                                                      vk::ImageLayout::eShaderReadOnlyOptimal);
            materials[i].desc_image_info.emplace_back(materials[i].p_specular_map->sampler,
                                                      materials[i].p_specular_map->view,
                                                      vk::ImageLayout::eShaderReadOnlyOptimal);
            materials[i].desc_image_info.emplace_back(materials[i].p_normal_map->sampler,
                                                      materials[i].p_normal_map->view,
                                                      vk::ImageLayout::eShaderReadOnlyOptimal);

            // uniform data

            memcpy(p_uniform_data + ptr_offset, &materials[i].properties.ambient, 3 * sizeof(float));
            ptr_offset+=3;
            // padding
            ptr_offset+=1;

            memcpy(p_uniform_data + ptr_offset, &materials[i].properties.diffuse, 3 * sizeof(float));
            ptr_offset+=3;
            memcpy(p_uniform_data + ptr_offset, &materials[i].properties.alpha, 3 * sizeof(float));
            ptr_offset+=1;

            memcpy(p_uniform_data + ptr_offset, &materials[i].properties.specular, 3 * sizeof(float));
            ptr_offset+=3;
            memcpy(p_uniform_data + ptr_offset, &materials[i].properties.specular_exponent, 3 * sizeof(float));
            ptr_offset+=1;

            memcpy(p_uniform_data + ptr_offset, &materials[i].properties.emissive, 3 * sizeof(float));
            ptr_offset+=3;

            memcpy(p_uniform_data + ptr_offset, padding.data(), padding_count * sizeof(float));
            ptr_offset+=padding_count;

            // dynamic offset
            materials[i].dynamic_offset=static_cast<uint32_t>(uniform_buffer_aligned_size * i);

        }

        // create device local uniform buffer
        p_uniform_buffer_=new base::Buffer(p_dev_,
                                           vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                           vk::MemoryPropertyFlagBits::eDeviceLocal,
                                           uniform_data_device_size,
                                           vk::SharingMode::eExclusive);

        desc_buffer_info_=vk::DescriptorBufferInfo(p_uniform_buffer_->buf, 0, VK_WHOLE_SIZE);

        // allocate uniform buffer memory
        allocate_and_bind_buffer_memory(p_phy_dev_,
                                        p_dev_,
                                        uniform_buf_mem_,
                                        1,
                                        &p_uniform_buffer_);

        update_device_local_buffer_memory(p_phy_dev_,
                                          p_dev_,
                                          p_uniform_buffer_,
                                          uniform_buf_mem_,
                                          uniform_data_device_size,
                                          p_uniform_data,
                                          0,
                                          vk::PipelineStageFlagBits::eTopOfPipe,
                                          vk::PipelineStageFlagBits::eFragmentShader,
                                          {}, vk::AccessFlagBits::eShaderRead,
                                          cmd_buffers[0]);

        /* descriptor pool */

        std::vector<vk::DescriptorPoolSize> pool_sizes;
        pool_sizes.emplace_back(vk::DescriptorType::eUniformBufferDynamic, 1);
        pool_sizes.emplace_back(vk::DescriptorType::eCombinedImageSampler, materials.size() * 4);

        desc_pool_=p_dev_->dev.createDescriptorPool(
            vk::DescriptorPoolCreateInfo({},
                                         static_cast<uint32_t>(materials.size() * 4 + 1),
                                         static_cast<uint32_t>(pool_sizes.size()),
                                         pool_sizes.data()));

        /* descriptor set layout */

        // uniform
        vk::DescriptorSetLayoutBinding layout_binding{
            0,
            vk::DescriptorType::eUniformBufferDynamic,
            1,
            vk::ShaderStageFlagBits::eFragment
        };

        desc_set_layout_uniform=p_dev_->dev.createDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo(
        {},
                1,
                &layout_binding));

        // sampler
        std::vector<vk::DescriptorSetLayoutBinding> layout_bindings={
            {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
            {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
            {2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
            {3, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment}
        };

        desc_set_layout_sampler=p_dev_->dev.createDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo(
        {},
                static_cast<uint32_t>(layout_bindings.size()),
                layout_bindings.data()));

        /* allocate desc sets */

        // uniform

        std::vector<vk::DescriptorSet> desc_sets=
            p_dev_->dev.allocateDescriptorSets(
                vk::DescriptorSetAllocateInfo(
                    desc_pool_,
                    1,
                    &desc_set_layout_uniform));
        desc_set_uniform=desc_sets[0];
        desc_sets.clear();

        vk::WriteDescriptorSet write{desc_set_uniform,
            0,
            0,
            1,
            vk::DescriptorType::eUniformBufferDynamic,
            nullptr,
            &desc_buffer_info_,
            nullptr};
        p_dev_->dev.updateDescriptorSets(1, &write, 0, nullptr);

        // sampler

        std::vector<vk::DescriptorSetLayout> layouts(materials.size(), desc_set_layout_sampler);

        desc_sets=p_dev_->dev.allocateDescriptorSets(
            vk::DescriptorSetAllocateInfo(
                desc_pool_,
                static_cast<uint32_t>(materials.size()),
                layouts.data()));

        std::vector<vk::WriteDescriptorSet> writes;
        for (size_t i=0; i < materials.size(); i++) {
            materials[i].desc_set_sampler=desc_sets[i];
            writes.emplace_back(materials[i].desc_set_sampler, // dst set
                                0, // dst binding
                                0, // dst array element
                                1, // desc set count
                                vk::DescriptorType::eCombinedImageSampler, // type
                                &materials[i].desc_image_info[0], // image info
                                nullptr, // buffer info
                                nullptr); // texel buffer view
            writes.emplace_back(materials[i].desc_set_sampler, // dst set
                                1, // dst binding
                                0, // dst array element
                                1, // desc set count
                                vk::DescriptorType::eCombinedImageSampler, // type
                                &materials[i].desc_image_info[1], // image info
                                nullptr, // buffer info
                                nullptr); // texel buffer view
            writes.emplace_back(materials[i].desc_set_sampler, // dst set
                                2, // dst binding
                                0, // dst array element
                                1, // desc set count
                                vk::DescriptorType::eCombinedImageSampler, // type
                                &materials[i].desc_image_info[2], // image info
                                nullptr, // buffer info
                                nullptr); // texel buffer view
            writes.emplace_back(materials[i].desc_set_sampler, // dst set
                                3, // dst binding
                                0, // dst array element
                                1, // desc set count
                                vk::DescriptorType::eCombinedImageSampler, // type
                                &materials[i].desc_image_info[3], // image info
                                nullptr, // buffer info
                                nullptr); // texel buffer view

            p_dev_->dev.updateDescriptorSets(
                static_cast<uint32_t>(writes.size()),
                writes.data(),
                0, nullptr);

            writes.clear();
        }
    }

    void setup_material_texture_(aiMaterial *p_m, aiTextureType ai_tex_type,
                                 std::shared_ptr<Material_texture2D> &p_tex,
                                 std::shared_ptr<Material_texture2D> &p_dummy_tex,
                                 bool has_compression,
                                 const std::string &tex_format_suffix,
                                 const std::string &dummy_tex_filename,
                                 vk::Format tex_format,
                                 vk::Format dummy_tex_format)
    {

        aiString ai_tex_filename;
        p_m->GetTexture(ai_tex_type, 0, &ai_tex_filename);

        if (p_m->GetTextureCount(ai_tex_type) > 0) {
            std::string tex_filename=std::string{ai_tex_filename.C_Str()};
            assert(base::ends_width(tex_filename, ".ktx"));

            // remove relative path
            auto relative_path=tex_filename.find_last_of("\\") + 1;
            if (relative_path < tex_filename.length())
                tex_filename=tex_filename.substr(relative_path, tex_filename.length() - relative_path);

            if (has_compression)
                tex_filename.insert(tex_filename.find(".ktx"), tex_format_suffix);

            // use asset_path
            auto tex_path=asset_path_ + "/" + tex_filename;
            assert(base::file_exists(tex_path));
            p_tex=std::make_shared<Material_texture2D>(p_phy_dev_, p_dev_,
                                                       tex_path,
                                                       graphics_cmd_pool_,
                                                       tex_format);
        }
        else {
            if (!p_dummy_tex) {
                auto tex_path=dummy_asset_path_ + "/" + dummy_tex_filename;
                assert(base::file_exists(tex_path));
                p_dummy_tex=std::make_shared<Material_texture2D>(p_phy_dev_, p_dev_,
                                                                 tex_path,
                                                                 graphics_cmd_pool_,
                                                                 dummy_tex_format);
            }
            p_tex=p_dummy_tex;
        }
    }

    void init_attributes_(const aiScene *p_scene,
                          const std::vector<vk::CommandBuffer> &cmd_buffers,
                          std::vector<float> &vdata,
                          std::vector<uint32_t> &idata,
                          float scale,
                          glm::vec3 xlate) override
    {
        base::Model::init_attributes_(p_scene, cmd_buffers, vdata, idata, scale, xlate);

        uint32_t idx_base=0;
        int32_t vert_offset=0;
        scene_parts.reserve(p_scene->mNumMeshes);
        for (uint32_t m=0; m < p_scene->mNumMeshes; m++) {
            auto p_mesh=p_scene->mMeshes[m];
            uint32_t idx_count=3 * p_mesh->mNumFaces;// triangulated
            scene_parts.emplace_back(&materials[p_mesh->mMaterialIndex],
                                     idx_base,
                                     idx_count,
                                     vert_offset);
            vert_offset+=p_mesh->mNumVertices;
            idx_base+=idx_count;
        }
    }
};
#undef MSG_PREFIX