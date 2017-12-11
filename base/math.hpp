#pragma once
#include <algorithm>
#include <glm/glm.hpp>
#define PI 3.1415926
#define EPS 0.0001

namespace base
{
float clamp(float num, float min, float max)
{
    return std::min(max, std::max(min, num));
}

class Spherical
{
public:
    glm::vec3 el;
    Spherical() : el(glm::vec3(0.f)) {}
    Spherical(float r, float phi, float theta) : el(glm::vec3(r, phi, theta)) {}
    void set_from_vec(glm::vec3 v)
    {
        el.x=glm::length(v); // r
        if (v.x == 0.f) {
            el.y=0.f;
            el.z=0.f;
        }
        else {
            el.y=acosf(clamp(-v.y / el.x, -1.f, 1.f)); // phi
            el.z=atan2f(-v.z, v.x); // theta
        }
    }
    void restrict(){
        // restrict phi to the range of EPS ~ PI - EPS
        el.y=std::fmax(EPS, std::fmin(PI - EPS, el.y));
    }
        glm::vec3 get_vec()
    {
        float x=el.x *sinf(el.y) * cosf(el.z);
        float y=-el.x * cosf(el.y);
        float z=-el.x * sinf(el.y) * sinf(el.z);
        return glm::vec3(x, y, z);
    }
};
} // namespace base

#undef PI
#undef EPS