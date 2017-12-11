#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace base
{
#ifdef VK_USE_PLATFORM_WIN32_KHR
class Timer
{
public:
    Timer()
    {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq); // from high resolution counter
        freq_=static_cast<double>(freq.QuadPart);
        reset();
    }
    void reset()
    {
        QueryPerformanceCounter(&start_);
    }
    double get() const
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return static_cast<double>(now.QuadPart - start_.QuadPart) / freq_;
    }
private:
    double freq_;
    LARGE_INTEGER start_;
};
#endif
} // namespace