#pragma once
#include "math.hpp"
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtx/rotate_vector.hpp>

namespace base
{
class Camera
{
public:
    glm::vec3 up{0.f, 1.f, 0.f};
    glm::vec3 eye_pos{0.f, 0.f, -4.f};
    glm::vec3 target{0.f, 0.f, 0.f};
    glm::mat4 view{glm::mat4(1.f)};
    glm::mat4 projection{glm::mat4(1.f)};
    float fov{glm::radians(45.f)};
    float cam_near{0.1f};
    float cam_far{10000.f};
    float aspect{1.333f};
    const glm::mat4
        clip{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f};

    Camera()
    {
        update();
    }

    Camera(glm::vec3 eye_pos, glm::vec3 target)
        : eye_pos(eye_pos),
        target(target)
    {
        update();
    }

    void update()
    {
        view=glm::lookAt(eye_pos, target, up);
        projection=glm::perspective(fov, aspect, cam_near, cam_far);
    }

    void update_aspect(uint32_t width, uint32_t height)
    {
        aspect=static_cast<float>(width) / static_cast<float>(height);
        update();
    }

    void orbit(float delta_zoom, float delta_phi, float delta_theta)
    {
        auto target_to_eye=eye_pos - target;
        Spherical s;
        s.set_from_vec(target_to_eye);
        s.el.x*=(1.f + delta_zoom);
        s.el.y+=delta_phi;
        s.el.z+=delta_theta;
        s.restrict();
        eye_pos=s.get_vec() + target;
        update();
    };

    void pan(float delta_x, float delta_y)
    {
        auto world=glm::inverse(view);
        auto right=glm::vec3(world[0][0], world[0][1], world[0][2]);
        auto up=glm::vec3(world[1][0], world[1][1], world[1][2]);
        target+=delta_x * right - delta_y * up;
        eye_pos+=delta_x * right - delta_y * up;
        update();
    };

    void forward(float delta_z)
    {
        auto world=glm::inverse(view);
        auto front=glm::vec3(world[2][0], world[2][1], world[2][2]);
        target+=delta_z * front;
        eye_pos+=delta_z * front;
        update();
    }

    void print_stat()
    {
        std::cout << "camera position " <<
            "(" << eye_pos.x << " " << eye_pos.y << " " << eye_pos.z << ")" <<
            ", target " <<
            "(" << target.x << " " << target.y << " " << target.z << ")" <<
            std::endl;
    }
};
} // namespace base
