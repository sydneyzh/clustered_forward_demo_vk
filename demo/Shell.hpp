#pragma once
#include <Shell_base.hpp>
#include <Camera.hpp>
#include "Prog_info.hpp"

class Shell : public base::Shell_base
{
public:
    float orbit_speed=0.04f;
    float zoom_speed=0.1f;
    float pan_speed=0.05f;

    Shell(Prog_info* p_info, base::Camera* p_camera)
        :Shell_base(p_info),
        p_camera_(p_camera),
        p_info_(p_info)
    {}

    void on_key(base::Key key) override
    {
        switch (key) {
            // orbit(zoom, phi, theta)
            case base::KEY_UP:p_camera_->orbit(0.f, orbit_speed, 0.f);
                break;
            case base::KEY_DOWN:p_camera_->orbit(0.f, -orbit_speed, 0.f);
                break;
            case base::KEY_LEFT:p_camera_->orbit(0.f, 0.f, -orbit_speed);
                break;
            case base::KEY_RIGHT:p_camera_->orbit(0.f, 0.f, orbit_speed);
                break;
            case base::KEY_WHEEL_UP:p_camera_->orbit(-zoom_speed, 0.f, 0.f);
                break;
            case base::KEY_WHEEL_DOWN:p_camera_->orbit(zoom_speed, 0.f, 0.f);
                break;

            case base::KEY_A:p_camera_->pan(-pan_speed, 0.f); // pan left 
                break;
            case base::KEY_D:p_camera_->pan(pan_speed, 0.f); // pan right
                break;
            case base::KEY_R:p_camera_->pan(0.f, -pan_speed); // pan up
                break;
            case base::KEY_F:p_camera_->pan(0.f, pan_speed); // pan down
                break;
            case base::KEY_W:p_camera_->forward(-pan_speed); // move forward
                break;
            case base::KEY_S:p_camera_->forward(pan_speed); // move backward
                break;

            default:base::Shell_base::on_key(key);
                break;
        }
    }

private:
    base::Camera* p_camera_;
    Prog_info *p_info_;

    void window_resize_(uint32_t width, uint32_t height) override
    {
        p_info_base_->on_resize(width, height);
        p_camera_->update_aspect(width, height);
    }
};
