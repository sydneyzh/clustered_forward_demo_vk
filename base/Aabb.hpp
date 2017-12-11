#pragma once
#include <glm/glm.hpp>

namespace base
{
class Aabb
{
public:
    glm::vec3 min;
    glm::vec3 max;

    Aabb(const glm::vec3& min, const glm::vec3& max)
        :min(min), max(max)
    {}

    Aabb(const glm::vec3& position, const float radius)
    {
        min=position - radius;
        max=position + radius;
    }

    const glm::vec3 gen_center() const
    {
        return (min + max)*.5f;
    }

    const glm::vec3 get_half_size() const
    {
        return (max - min)*.5f;
    }

    const glm::vec3 get_diagonal() const
    {
        return max - min;
    }

    float get_volume() const
    {
        glm::vec3 d=get_diagonal();
        return d.x*d.y*d.z;
    }

    float get_surface_area() const
    {
        glm::vec3 d=get_diagonal();
        return 2.f*(d.x*d.y + d.y*d.x + d.z*d.x);
    }

    bool inside(const glm::vec3& pt)
    {
        return max.x > pt.x && min.x<pt.x
            && max.y>pt.y && min.y<pt.y
            && max.z>pt.z && min.z < pt.z;
    }
};

Aabb combine(const Aabb& a, const Aabb& b)
{
    return Aabb{min(a.min, b.min), max(a.max, b.max)};
}

Aabb combine(const Aabb& a, const glm::vec3& pt)
{
    return Aabb{min(a.min, pt), max(a.max, pt)};
}

bool overlaps(const Aabb& a, const Aabb& b)
{
    return a.max.x > b.min.x && a.min.x<b.max.x
        && a.max.y>b.min.y && a.min.y<b.max.y
        && a.max.z>b.min.z && a.min.z < b.max.z;

}

Aabb operator*(const glm::mat4& transform, const Aabb& a);
} // namespace base
