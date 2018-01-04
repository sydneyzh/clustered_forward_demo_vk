#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

#include <tools.hpp>
#include <Physical_device.hpp>
#include <Device.hpp>
#include <Buffer.hpp>
#include <Shader.hpp>
#include <Texture.hpp>
#include "textoverlay.vert.h"
#include "textoverlay.frag.h"

#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>
#include <regex>
#include <cassert>
#define TEXT_OVERLAY_MAX_CHAR_COUNT 2048

struct Character
{
public:
    uint32_t id; // ascii code, field id
    float tex_coord[2]; // in uv, field x/xscale, y/yscale
    float tex_coord_size[2]; // in uv, field width/xscale, height/yscale
    uint32_t size[2]; // in pixel, field width, height
    int baseline_offset[2]; // in pixel, field xoffset, yoffset
    uint32_t x_advance; // in pixel, field xadvance

    Character() {}
    explicit Character(
        unsigned short id,
        float u, float v,
        float u_width, float u_height,
        uint32_t width, uint32_t height,
        int xoffset, int yoffset,
        uint32_t xadvance) :
        id(id),
        tex_coord{u, v},
        tex_coord_size{u_width, u_height},
        size{width, height},
        baseline_offset{xoffset, yoffset},
        x_advance(xadvance) {}
};

class Font
{
public:
    std::map<char, Character> characters;
    uint32_t line_height; // in pixel
    uint32_t base_height; // in pixel
    uint32_t font_size; // in pixel
    uint32_t char_count;

    base::Texture2D *p_tex;

    ~Font()
    {
        delete p_tex;
        characters.clear();
    }

    static bool regex_search_and_get_result(const std::string &line, std::string &res, const std::regex &patt)
    {
        std::smatch match;
        std::regex_search(line, match, patt);
        if (match.size()) {
            res=match[match.size() - 1].str();
            return true;
        }
        else {
            return false;
        }
    }

    static int string_to_int(const std::string &str)
    {
        int res;
        std::stringstream(str) >> res;
        return res;
    }

    static float string_to_float(const std::string &str)
    {
        float res;
        std::stringstream(str) >> res;
        return res;
    }

    void load(base::Physical_device *p_phy_dev, base::Device *p_dev, vk::CommandPool cmd_pool, const std::string &file_path)
    {
        std::string font_path=file_path + ".fnt";
        assert(base::file_exists(font_path));
        std::ifstream file(font_path);

        const std::regex font_size_patt("size=([0-9]+)");
        const std::regex line_height_patt("lineHeight=([0-9]+)");
        const std::regex base_height_patt("base=([0-9]+)");
        const std::regex char_count_patt("count=([0-9]+)");
        const std::regex xscale_patt("scaleH=([0-9]+)");
        const std::regex yscale_patt("scaleW=([0-9]+)");

        std::string line;
        std::string res;

        uint32_t xscale, yscale;

        while (std::getline(file, line)) {
            // font size
            if (regex_search_and_get_result(line, res, font_size_patt)) {
                font_size=stoi(res);
            }

            // line height
            if (regex_search_and_get_result(line, res, line_height_patt)) {
                line_height=stoi(res);
            }

            // base height
            if (regex_search_and_get_result(line, res, base_height_patt)) {
                base_height=stoi(res);
            }

            // xscale 
            if (regex_search_and_get_result(line, res, xscale_patt)) {
                xscale=stoi(res);
            }

            // yscale 
            if (regex_search_and_get_result(line, res, yscale_patt)) {
                yscale=stoi(res);
            }

            // char count
            if (regex_search_and_get_result(line, res, char_count_patt)) {
                char_count=stoi(res);
                break;
            }
        }

        const std::regex char_line_patt("^char\\s");
        const std::regex id_patt("id=([0-9]+)");
        const std::regex x_patt("x=([0-9]+)");
        const std::regex y_patt("y=([0-9]+)");
        const std::regex xoffset_patt("xoffset=(-*[0-9]+)");
        const std::regex yoffset_patt("yoffset=(-*[0-9]+)");
        const std::regex width_patt("width=([0-9]+)");
        const std::regex height_patt("height=([0-9]+)");
        const std::regex xadvance_patt("xadvance=([0-9]+)");

        uint32_t count=0;
        while (std::getline(file, line)) {
            if (regex_search_and_get_result(line, res, char_line_patt)) {
                count++;

                unsigned short id;
                float u, v, u_width, v_height;
                uint32_t width, height, xadvance;
                int xoffset, yoffset;

                // id
                assert(regex_search_and_get_result(line, res, id_patt));
                id=stoi(res);

                // in uv

                // u 
                assert(regex_search_and_get_result(line, res, x_patt));
                u=string_to_float(res) / static_cast<float>(xscale);

                // v 
                assert(regex_search_and_get_result(line, res, y_patt));
                v=string_to_float(res) / static_cast<float>(yscale);

                // u_width 
                assert(regex_search_and_get_result(line, res, width_patt));
                width=stoi(res);
                u_width=string_to_float(res) / static_cast<float>(xscale);

                // v_height 
                assert(regex_search_and_get_result(line, res, height_patt));
                height=stoi(res);
                v_height=string_to_float(res) / static_cast<float>(yscale);

                // in pixel

                // xoffset 
                assert(regex_search_and_get_result(line, res, xoffset_patt));
                xoffset=string_to_int(res);

                // yoffset 
                assert(regex_search_and_get_result(line, res, yoffset_patt));
                yoffset=string_to_int(res);

                // xadvance
                assert(regex_search_and_get_result(line, res, xadvance_patt));
                xadvance=stoi(res);

                characters.emplace(id, Character(
                    id,
                    u, v,
                    u_width, v_height,
                    width, height,
                    xoffset, yoffset,
                    xadvance));
            }
        }

        // font texture
        p_tex=new base::Texture2D(p_phy_dev, p_dev);
        auto ktx_path=file_path + ".ktx";
        p_tex->load(ktx_path,
                    cmd_pool,
                    vk::Format::eR8G8B8A8Unorm,
                    vk::ImageUsageFlagBits::eSampled,
                    vk::ImageLayout::eShaderReadOnlyOptimal,
                    true);
    }

    void generate_text_data(const std::string &text,
                            float scr_u, float scr_v,
                            float scr_font_scale_x,
                            float scr_font_scale_y,
                            std::vector<glm::vec4> &vec_content,
                            std::vector<uint32_t> &idx_content,
                            uint32_t idx_offset)
    {
        vec_content.clear();
        idx_content.clear();
        vec_content.reserve(4 * text.size());
        idx_content.reserve(6 * text.size());

        const float scale_x=scr_font_scale_x / static_cast<float>(font_size);
        const float scale_y=scr_font_scale_y / static_cast<float>(font_size);

        // in screen uv
        float cursor_pos[2]={scr_u, scr_v};
        cursor_pos[1]+=base_height * scale_y;

        uint32_t i=0;
        for (auto t : text) {

            if (int(t) == int('\n')) {
                cursor_pos[0]=scr_u;
                cursor_pos[1]+=line_height * scale_y;
                continue;;
            }

            uint32_t id=int(t);
            Character ch=characters[id];
            assert(ch.id == id);

            // vec4:
            // pos[2] // in screen uv
            // uv[2] // in tex uv

            // a
            glm::vec4 vec_a={
                cursor_pos[0] + ch.baseline_offset[0] * scale_x,
                cursor_pos[1] + ch.baseline_offset[1] * scale_y,
                ch.tex_coord[0],
                ch.tex_coord[1]
            };

            // b
            glm::vec4 vec_b={
                vec_a[0],
                vec_a[1] + ch.size[1] * scale_y,
                vec_a[2],
                vec_a[3] + ch.tex_coord_size[1]
            };

            // c
            glm::vec4 vec_c={
                vec_a[0] + ch.size[0] * scale_x,
                vec_b[1],
                vec_a[2] + ch.tex_coord_size[0],
                vec_b[3]
            };

            // d
            glm::vec4 vec_d={
                vec_c[0],
                vec_a[1],
                vec_c[2],
                vec_a[3]
            };

            //screen uv pos to screen ndc pos
            vec_a[0]=2 * vec_a[0] - 1;
            vec_a[1]=2 * vec_a[1] - 1;
            vec_b[0]=2 * vec_b[0] - 1;
            vec_b[1]=2 * vec_b[1] - 1;
            vec_c[0]=2 * vec_c[0] - 1;
            vec_c[1]=2 * vec_c[1] - 1;
            vec_d[0]=2 * vec_d[0] - 1;
            vec_d[1]=2 * vec_d[1] - 1;

            // 6 idx per quad
            uint32_t base=idx_offset + i * 4;
            uint32_t idx[6]={base, base + 1, base + 3, base + 1, base + 2, base + 3};
            idx_content.insert(idx_content.end(), idx, idx + 6);

            // 4 vert per quad
            vec_content.push_back(vec_a);
            vec_content.push_back(vec_b);
            vec_content.push_back(vec_c);
            vec_content.push_back(vec_d);

            i++;

            cursor_pos[0]+=ch.x_advance * scale_x;
        }
    }
};

class Text_overlay
{
public:
    base::Buffer *p_vert_buf{nullptr};
    base::Buffer *p_idx_buf{nullptr};
    vk::DeviceMemory vert_buf_mem;
    vk::DeviceMemory idx_buf_mem;

    uint32_t draw_index_count{0};

    Font * p_font{nullptr};

    base::Shader *p_vs{nullptr};
    base::Shader *p_fs{nullptr};

    Text_overlay(
        base::Physical_device *p_phy_dev,
        base::Device *p_dev,
        vk::CommandPool cmd_pool,
        const std::string &file_path) :
        p_phy_dev_(p_phy_dev),
        p_dev_(p_dev),
        cmd_pool_(cmd_pool)
    {
        prepare_font_(file_path);
        prepare_mesh_();
        prepare_shaders_();
    }

    ~Text_overlay()
    {
        p_dev_->dev.waitIdle();
        delete p_vert_buf;
        delete p_idx_buf;
        p_dev_->dev.freeMemory(vert_buf_mem);
        p_dev_->dev.freeMemory(idx_buf_mem);
        delete p_vs;
        delete p_fs;
        delete p_font;
    }

    void update_text(
        const std::string &text,
        const float scr_u, const float scr_v,
        const uint32_t font_size,
        const uint32_t scr_width,
        const uint32_t scr_height) // in pixel
    {
        assert(text.size() <= TEXT_OVERLAY_MAX_CHAR_COUNT);

        std::vector<glm::vec4> vec_content;
        std::vector<uint32_t> idx_content;
        uint32_t idx_offset=0;
        // scale pixel size to screen uv
        float font_scale_x=static_cast<float>(font_size) / static_cast<float>(scr_width);
        float font_scale_y=static_cast<float>(font_size) / static_cast<float>(scr_height);
        p_font->generate_text_data(text,
                                   scr_u, scr_v,
                                   font_scale_x,
                                   font_scale_y,
                                   vec_content,
                                   idx_content,
                                   idx_offset);

        assert(p_idx_buf->mapped);
        assert(p_vert_buf->mapped);
        memcpy(p_idx_buf->mapped, idx_content.data(), idx_content.size() * sizeof(uint32_t));
        memcpy(p_vert_buf->mapped, vec_content.data(), vec_content.size() * sizeof(glm::vec4));

        draw_index_count=idx_content.size();
    }

protected:
    base::Physical_device *p_phy_dev_;
    base::Device *p_dev_;
    vk::CommandPool cmd_pool_;

    void prepare_font_(const std::string &file_path)
    {
        assert(cmd_pool_);
        p_font=new Font();
        p_font->load(p_phy_dev_, p_dev_, cmd_pool_, file_path);
    }

    void prepare_shaders_()
    {
        // shaders
        p_vs=new base::Shader(p_dev_, vk::ShaderStageFlagBits::eVertex);
        p_fs=new base::Shader(p_dev_, vk::ShaderStageFlagBits::eFragment);
        p_vs->generate(sizeof(textoverlay_vert), textoverlay_vert);
        p_fs->generate(sizeof(textoverlay_frag), textoverlay_frag);
    }

    void prepare_mesh_()
    {
        // 2 triangles per char
        // vertex buffer
        // posXY, uv
        const vk::DeviceSize vert_buf_size=4 * TEXT_OVERLAY_MAX_CHAR_COUNT * sizeof(glm::vec4);
        p_vert_buf=new base::Buffer(p_dev_,
                                    vk::BufferUsageFlagBits::eVertexBuffer,
                                    vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent,
                                    vert_buf_size);
        // idx buffer
        const vk::DeviceSize idx_buf_size=6 * TEXT_OVERLAY_MAX_CHAR_COUNT * sizeof(uint32_t);
        p_idx_buf=new base::Buffer(p_dev_,
                                   vk::BufferUsageFlagBits::eIndexBuffer,
                                   vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent,
                                   idx_buf_size);

        allocate_and_bind_buffer_memory(p_phy_dev_,
                                        p_dev_,
                                        vert_buf_mem, 1, &p_vert_buf);

        allocate_and_bind_buffer_memory(p_phy_dev_,
                                        p_dev_,
                                        idx_buf_mem, 1, &p_idx_buf);
    }
};
