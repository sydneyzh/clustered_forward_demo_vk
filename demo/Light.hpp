#pragma once
#include <math.hpp>
#include <vector>

class Light
{
public:
    glm::vec3 position;
    glm::u8vec4 color;
    float range;
    glm::vec3 center{0.f};
    base::Spherical s;

    Light(const glm::vec3& position, glm::u8vec4 &color, float range)
        :position(position),
        color(color),
        range(range)
    {}

    Light(const glm::vec3& position, const Light& light)
        :position(position),
        color(light.color),
        range(light.range)
    {}
    void update(float elapsed_time)
    {
        s.set_from_vec(position - center);
        s.el.z+=0.001;
        s.restrict();
        auto v=s.get_vec();
        position=center + v;
    }
};

typedef std::vector<Light> Lights;

