#include <iostream>
#include <algorithm>
#include <iomanip>

namespace base
{
class FPS_log
{
public:
    void update(float delta_time)
    {
        float milsec=delta_time * 1000.f;

        if (frame_counts_ > 0) {
            frame_time_max_=std::max(frame_time_max_, milsec);
            frame_time_min_=std::min(frame_time_min_, milsec);
            frame_time_avg_=(frame_time_avg_ * frame_counts_ + milsec) / (frame_counts_ + 1);
        }
        else {
            frame_time_max_=milsec;
            frame_time_min_=milsec;
            frame_time_avg_=milsec;
        }
        frame_counts_++;

        if (frame_counts_ == log_counts_) {
            std::cout << std::fixed << std::setprecision(1) <<
                "frame time min: " << frame_time_min_ <<
                ", max: " << frame_time_max_ <<
                ", avg: " << frame_time_avg_ <<
                ", fps: " << static_cast<int>(1000.f / frame_time_avg_) << std::endl;
            frame_counts_=0;
        }
    }
private:
    uint32_t frame_counts_{0};
    uint32_t log_counts_{180};
    float frame_time_max_{0.f};
    float frame_time_min_{0.f};
    float frame_time_avg_{0.f};
};
} // namespace base