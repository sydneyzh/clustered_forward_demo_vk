#pragma once
#include <glm/glm.hpp>
#include "math.hpp"

namespace base
{
void hue_to_rgb(glm::vec3 &out, float hue)
{
    const float s=hue * 6.0f;
    float r0=clamp(s - 4.0f, 0.0f, 1.0f);
    float g0=clamp(s - 0.0f, 0.0f, 1.0f);
    float b0=clamp(s - 2.0f, 0.0f, 1.0f);

    float r1=clamp(2.0f - s, 0.0f, 1.0f);
    float g1=clamp(4.0f - s, 0.0f, 1.0f);
    float b1=clamp(6.0f - s, 0.0f, 1.0f);

    out[0]=r0 + r1;
    out[1]=g0 * g1;
    out[2]=b0 * b1;
}

void float_to_rgbunorm(glm::u8vec4 &out, const glm::vec3 &fcol)
{
    out[0]=static_cast<uint8_t>(std::round(fcol[0] * 255.f));
    out[1]=static_cast<uint8_t>(std::round(fcol[1] * 255.f));
    out[2]=static_cast<uint8_t>(std::round(fcol[2] * 255.f));
    out[3]=0;
}
} // namespace base