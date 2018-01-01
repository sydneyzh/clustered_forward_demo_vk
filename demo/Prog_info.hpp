#pragma once
#include <Prog_info_base.hpp>
#include <algorithm>
#include <string>

class Prog_info : public base::Prog_info_base
{
public:
    uint32_t MAX_WIDTH{1920};
    uint32_t MAX_HEIGHT{1080};

    uint32_t MIN_NUM_LIGHTS{1024};
    uint32_t MAX_NUM_LIGHTS{600000};
    uint32_t num_lights{0};
    bool gen_lights{false};

    uint32_t TILE_WIDTH{64};
    uint32_t TILE_HEIGHT{64};

    uint32_t tile_count_x{0};
    uint32_t tile_count_y{0};
    uint32_t TILE_COUNT_Z{256};

    Prog_info()
    {
        update_tile_counts();
        num_lights=2048;
    }

    void update_tile_counts()
    {
        tile_count_x=(width_ - 1) / TILE_WIDTH + 1;
        tile_count_y=(height_ - 1) / TILE_HEIGHT + 1;
    }

    uint32_t width() const override
    {
        return width_;
    }

    uint32_t height() const override
    {
        return height_;
    }

    const std::string& prog_name() const override
    {
        return prog_name_;
    }

    void on_resize(uint32_t width, uint32_t height) override
    {
        width_=std::min(MAX_WIDTH, width);
        height_=std::min(MAX_HEIGHT, height);
        update_tile_counts();
        resize_flag=true;
    }

    void increase_num_lights()
    {
        num_lights=std::min(MAX_NUM_LIGHTS, num_lights * 2);
        gen_lights=true;
    }

    void decrease_num_lights()
    {
        num_lights=std::max(MIN_NUM_LIGHTS, num_lights / 2);
        gen_lights=true;
    }

private:
    uint32_t width_{800};
    uint32_t height_{600};
    std::string prog_name_{"Clustered forward demo"};
};