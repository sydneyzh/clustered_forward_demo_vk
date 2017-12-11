#pragma once
#include <Prog_info_base.hpp>
#include <algorithm>
#include <string>

class Prog_info : public base::Prog_info_base
{
public:
    uint32_t MAX_WIDTH{1920};
    uint32_t MAX_HEIGHT{1080};

    uint32_t MAX_NUM_LIGHTS{2048};
    uint32_t num_lights{0};

    uint32_t TILE_WIDTH{64};
    uint32_t TILE_HEIGHT{64};

    uint32_t tile_count_x{0};
    uint32_t tile_count_y{0};
    uint32_t TILE_COUNT_Z{256};

    Prog_info()
    {
        update_tile_counts();
        num_lights=MAX_NUM_LIGHTS;
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

private:
    uint32_t width_{800};
    uint32_t height_{600};
    std::string prog_name_{"Clustered forward demo"};
};