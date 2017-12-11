#pragma once
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include "Physical_device.hpp"
#include "Device.hpp"
#include "Buffer.hpp"
#include "Aabb.hpp"
#include "tools.hpp"
#include <string>
#define MSG_PREFIX "-- MODEL: "

namespace base
{
typedef enum Vertex_component
{
    VERT_COMP_COLOR4,
    VERT_COMP_COLOR3,
    VERT_COMP_POSITION,
    VERT_COMP_NORMAL,
    VERT_COMP_TANGENT,
    VERT_COMP_BITANGENT,
    VERT_COMP_UV,
    VERT_COMP_FLOAT
} Vertex_component;

struct Vertex_layout
{
    std::vector<Vertex_component> comps;

    Vertex_layout()=default;
    explicit Vertex_layout(std::vector<Vertex_component> &comps)
    {
        this->comps=std::move(comps);
    }

    uint32_t get_stride()
    {
        uint32_t res=0;
        for (auto &comp : comps) {
            switch (comp) {
                case VERT_COMP_COLOR4:res+=4 * sizeof(float);
                    break;
                case VERT_COMP_POSITION:
                case VERT_COMP_NORMAL:
                case VERT_COMP_TANGENT:
                case VERT_COMP_BITANGENT:
                case VERT_COMP_COLOR3:res+=3 * sizeof(float);
                    break;
                case VERT_COMP_UV:res+=2 * sizeof(float);
                    break;
                case VERT_COMP_FLOAT:res+=sizeof(float);
                    break;
                default:throw std::runtime_error("invalid vertex component");
            }
        }
        return res;
    }
};

class Model
{
public:
    glm::mat4 model_matrix{1.f};
    glm::mat4 normal_matrix{1.f};

    Aabb aabb={glm::vec3(0.f), glm::vec3(0.f)};

    Buffer *p_vert_buffer{nullptr};
    Buffer *p_idx_buffer{nullptr};
    vk::DeviceMemory vert_buffer_mem;
    vk::DeviceMemory idx_buffer_mem;

    Vertex_layout vertex_layout{};
    uint32_t stride{0};
    uint32_t indices{0};

    vk::VertexInputBindingDescription vi_binding;
    std::vector<vk::VertexInputAttributeDescription> vi_attribs;
    uint32_t vi_bind_id{0};

    vk::PipelineVertexInputStateCreateInfo input_state;

    Model(Physical_device *p_phy_dev,
          Device *p_dev,
          vk::CommandPool graphics_cmd_pool)
        : p_phy_dev_(p_phy_dev),
        p_dev_(p_dev),
        graphics_cmd_pool_(graphics_cmd_pool)
    {}

    virtual ~Model()
    {
        p_dev_->dev.freeMemory(vert_buffer_mem);
        p_dev_->dev.freeMemory(idx_buffer_mem);
        delete p_vert_buffer;
        delete p_idx_buffer;
    }

    void load(std::string &model_path,
              Vertex_layout &layout,
              float scale=1.f,
              glm::vec3 xlate=glm::vec3(0.f),
              int ai_flags=0)
    {

        assert(file_exists(model_path));
        model_path_=model_path;
        std::cout << MSG_PREFIX << "reading file " << model_path << std::endl;

        vertex_layout=layout;
        stride=vertex_layout.get_stride();

        std::vector<float> vdata;
        std::vector<uint32_t> idata;

        Assimp::Importer importer;
        const int flags=
            aiProcess_PreTransformVertices |
            aiProcess_Triangulate |
            aiProcess_GenNormals |
            aiProcess_RemoveRedundantMaterials | ai_flags;
        const aiScene *p_scene=importer.ReadFile(model_path_.c_str(), flags);
        assert(p_scene);
        std::cout << MSG_PREFIX << "Assimp scene loaded" << std::endl;

        // copy cmd buffer
        std::vector<vk::CommandBuffer> cmd_buffers=
            p_dev_->dev.allocateCommandBuffers(
                vk::CommandBufferAllocateInfo(
                    graphics_cmd_pool_,
                    vk::CommandBufferLevel::ePrimary,
                    1));

        load_materials_(p_scene, cmd_buffers);
        init_attributes_(p_scene, cmd_buffers, vdata, idata, scale, xlate);

        vdata.clear();
        idata.clear();

        p_dev_->dev.freeCommandBuffers(graphics_cmd_pool_, cmd_buffers);
        cmd_buffers.clear();

        // vi bindings
        vi_binding=vk::VertexInputBindingDescription(0, stride, vk::VertexInputRate::eVertex);

        // vi attribs
        uint32_t idx=0;
        vk::DeviceSize offset=0;
        for (auto &comp : vertex_layout.comps) {
            switch (comp) {
                case VERT_COMP_FLOAT:
                    vi_attribs.emplace_back(idx++,
                                            vi_bind_id,
                                            vk::Format::eR32Sfloat,
                                            offset);
                    offset+=sizeof(float);
                    break;
                case VERT_COMP_UV:
                    vi_attribs.emplace_back(idx++,
                                            vi_bind_id,
                                            vk::Format::eR32G32Sfloat,
                                            offset);
                    offset+=sizeof(float) * 2;
                    break;
                case VERT_COMP_POSITION:
                case VERT_COMP_NORMAL:
                case VERT_COMP_TANGENT:
                case VERT_COMP_BITANGENT:
                case VERT_COMP_COLOR3:
                    vi_attribs.emplace_back(idx++,
                                            vi_bind_id,
                                            vk::Format::eR32G32B32Sfloat,
                                            offset);
                    offset+=sizeof(float) * 3;
                    break;
                case VERT_COMP_COLOR4:
                    vi_attribs.emplace_back(idx++,
                                            vi_bind_id,
                                            vk::Format::eR32G32B32A32Sfloat,
                                            offset);
                    offset+=sizeof(float) * 4;
                    break;
                default:throw std::runtime_error("invalid vertex component");
            }
        }

        // input_state
        input_state=vk::PipelineVertexInputStateCreateInfo(
        {}, 1, &vi_binding,
            static_cast<uint32_t>(vi_attribs.size()),
            vi_attribs.data());
    }

    Aabb get_aabb()
    {
        return aabb;
    }

protected:
    base::Physical_device *p_phy_dev_;
    base::Device *p_dev_;

    // for allocating device local memory
    vk::CommandPool graphics_cmd_pool_;

    std::string model_path_;

    virtual void load_materials_(const aiScene *p_scene,
                                 const std::vector<vk::CommandBuffer> &cmd_buffers)
    {};

    virtual void init_attributes_(const aiScene *p_scene,
                                  const std::vector<vk::CommandBuffer> &cmd_buffers,
                                  std::vector<float> &vdata,
                                  std::vector<uint32_t> &idata,
                                  float scale,
                                  glm::vec3 xlate)
    {
        glm::vec3 min{FLT_MAX};
        glm::vec3 max(FLT_MIN);
        for (uint32_t m=0; m < p_scene->mNumMeshes; m++) {
            auto p_mesh=p_scene->mMeshes[m];

            bool has_color=p_mesh->HasVertexColors(0);
            bool has_uv=p_mesh->HasTextureCoords(0);
            bool has_tangent=p_mesh->HasTangentsAndBitangents();

            for (uint32_t v=0; v < p_mesh->mNumVertices; v++) {
                for (auto &comp : vertex_layout.comps) {
                    switch (comp) {
                        case VERT_COMP_POSITION:
                        {
                            float x=(p_mesh->mVertices[v].x) * scale + xlate[0];
                            float y=(p_mesh->mVertices[v].y) * scale + xlate[1];
                            float z=(p_mesh->mVertices[v].z) * scale + xlate[2];
                            vdata.push_back(x);
                            vdata.push_back(y);
                            vdata.push_back(z);
                            min.x=std::min(x, min.x);
                            min.y=std::min(y, min.y);
                            min.z=std::min(z, min.z);
                            max.x=std::max(x, max.x);
                            max.y=std::max(y, max.y);
                            max.z=std::max(z, max.z);
                        }
                        break;
                        case VERT_COMP_NORMAL:
                        {
                            vdata.push_back(p_mesh->mNormals[v].x);
                            vdata.push_back(p_mesh->mNormals[v].y);
                            vdata.push_back(p_mesh->mNormals[v].z);
                        }
                        break;
                        case VERT_COMP_COLOR3:
                            if (has_color) {
                                vdata.push_back(p_mesh->mColors[v]->r);
                                vdata.push_back(p_mesh->mColors[v]->g);
                                vdata.push_back(p_mesh->mColors[v]->b);
                            }
                            else {
                                vdata.push_back(1.f);
                                vdata.push_back(1.f);
                                vdata.push_back(1.f);
                            }
                            break;
                        case VERT_COMP_COLOR4:
                            if (has_color) {
                                vdata.push_back(p_mesh->mColors[v]->r);
                                vdata.push_back(p_mesh->mColors[v]->g);
                                vdata.push_back(p_mesh->mColors[v]->b);
                                vdata.push_back(p_mesh->mColors[v]->a);
                            }
                            else {
                                vdata.push_back(1.f);
                                vdata.push_back(1.f);
                                vdata.push_back(1.f);
                                vdata.push_back(1.f);
                            }
                            break;
                        case VERT_COMP_UV:
                        {
                            assert(has_uv);
                            aiVector3D pTexCoord=p_mesh->mTextureCoords[0][v];
                            vdata.push_back(pTexCoord.x);
                            vdata.push_back(pTexCoord.y);
                        }
                        break;
                        case VERT_COMP_TANGENT:
                        {
                            assert(has_tangent);
                            vdata.push_back(p_mesh->mTangents[v].x);
                            vdata.push_back(p_mesh->mTangents[v].y);
                            vdata.push_back(p_mesh->mTangents[v].z);
                        }
                        break;
                        case VERT_COMP_BITANGENT:
                        {
                            assert(has_tangent);
                            vdata.push_back(p_mesh->mBitangents[v].x);
                            vdata.push_back(p_mesh->mBitangents[v].y);
                            vdata.push_back(p_mesh->mBitangents[v].z);
                        }
                        break;
                        default:throw std::runtime_error("invalid vertex component");
                    } // switch component
                } // loop components
            } // loop vertices 

            for (uint32_t f=0; f < p_mesh->mNumFaces; f++) {
                for (uint32_t j=0; j < 3; j++) { // triangulate
                    idata.emplace_back(p_mesh->mFaces[f].mIndices[j]);
                }
            }
        } // loop meshes

        aabb={min, max};
        flip_model_();

        indices=static_cast<uint32_t>(idata.size());
        std::cout << MSG_PREFIX << "index count: " << indices << std::endl;

        // attribute buffers

        const vk::DeviceSize vert_buf_size=vdata.size() * sizeof(vdata[0]);
        const vk::DeviceSize idx_buf_size=idata.size() * sizeof(idata[0]);
        std::cout << MSG_PREFIX << "vertex buffer size: " << vert_buf_size << std::endl;
        std::cout << MSG_PREFIX << "index buffer size: " << idx_buf_size << std::endl;

        // create device local buffers
        p_vert_buffer=new Buffer(p_dev_,
                                 vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                 vk::MemoryPropertyFlagBits::eDeviceLocal,
                                 vert_buf_size,
                                 vk::SharingMode::eExclusive);
        p_idx_buffer=new Buffer(p_dev_,
                                vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                vk::MemoryPropertyFlagBits::eDeviceLocal,
                                idx_buf_size,
                                vk::SharingMode::eExclusive);

        allocate_and_bind_buffer_memory(p_phy_dev_,
                                        p_dev_,
                                        vert_buffer_mem, 1, &p_vert_buffer);

        allocate_and_bind_buffer_memory(p_phy_dev_,
                                        p_dev_,
                                        idx_buffer_mem, 1, &p_idx_buffer);

        update_device_local_buffer_memory(
            p_phy_dev_,
            p_dev_,
            p_vert_buffer,
            vert_buffer_mem,
            vert_buf_size,
            vdata.data(), 0,
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eVertexInput,
            vk::AccessFlags(), vk::AccessFlagBits::eVertexAttributeRead,
            cmd_buffers[0]);

        update_device_local_buffer_memory(
            p_phy_dev_,
            p_dev_,
            p_idx_buffer,
            idx_buffer_mem,
            idx_buf_size,
            idata.data(), 0,
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eVertexInput,
            vk::AccessFlags(), vk::AccessFlagBits::eIndexRead,
            cmd_buffers[0]);
    }

    void flip_model_()
    {
        model_matrix=glm::rotate(model_matrix, 3.1415926f, glm::vec3(1.f, 0.f, 0.f));
        normal_matrix=glm::transpose(glm::inverse(model_matrix));
        auto min=aabb.min;
        auto max=aabb.max;
        aabb={glm::vec3(min.x, -max.y, -max.z), glm::vec3(max.x, -min.y, -min.z)};
        std::cout << MSG_PREFIX << "aabb min: (" << aabb.min.x << ", " << aabb.min.y << ", " << aabb.min.z << ") max: (" << aabb.max.x << ", " << aabb.max.y << ", " << aabb.max.z << ")" << std::endl;
    }
};
} // namespace base
#undef MSG_PREFIX