#pragma once
#include <vulkan/vulkan.hpp>
#include "Prog_info_base.hpp"
#ifdef VK_USE_PLATFORM_WIN32_KHR
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#error "uninplemented platform"
#endif

namespace base
{
enum Key
{
    KEY_WHEEL_UP,
    KEY_WHEEL_DOWN,
    KEY_SHUTDOWN,
    KEY_UNKNOWN,
    KEY_ESC,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_SPACE,
    KEY_A,
    KEY_W,
    KEY_S,
    KEY_D,
    KEY_R,
    KEY_F,
    KEY_F1,
    KEY_F2,
    KEY_F3,
    KEY_F4,
    KEY_F5,
    KEY_F6,
    KEY_F7,
    KEY_F8,
    KEY_F9,
    KEY_F10,
    KEY_F11,
    KEY_F12,
    KEY_NUM_1,
    KEY_NUM_2,
    KEY_NUM_3,
    KEY_NUM_4,
    KEY_NUM_5,
    KEY_NUM_6,
    KEY_NUM_7,
    KEY_NUM_8,
    KEY_NUM_9,
    KEY_NUM_0
};

class Shell_base
{

public:

#ifdef VK_USE_PLATFORM_WIN32_KHR
    HINSTANCE hinstance;
    HWND hwnd;
#else
#error "uninplemented platform"
#endif

    Shell_base()=delete;

    explicit Shell_base(Prog_info_base* p_info)
        :p_info_base_(p_info)
    {}
    virtual ~Shell_base()=default;

    void destroy_window()
    {
#ifdef VK_USE_PLATFORM_WIN32_KHR
        DestroyWindow(hwnd);
#else
#error "uninplemented platform"
#endif
    }

    VkBool32 can_present(vk::PhysicalDevice phy_dev, uint32_t queue_family)
    {
#ifdef VK_USE_PLATFORM_WIN32_KHR
        return phy_dev.getWin32PresentationSupportKHR(queue_family);
#else
#error "uninplemented platform"
#endif
    }

    void init_window()
    {
#ifdef VK_USE_PLATFORM_WIN32_KHR
        const std::string class_name(p_info_base_->prog_name() + "WindowClass");
        hinstance=GetModuleHandle(nullptr);
        WNDCLASSEX win_class={};
        win_class.cbSize=sizeof(WNDCLASSEX);
        win_class.style=CS_HREDRAW | CS_VREDRAW;
        win_class.lpfnWndProc=window_proc_;
        win_class.hInstance=hinstance;
        win_class.hCursor=LoadCursor(nullptr, IDC_ARROW);
        win_class.lpszClassName=class_name.c_str();
        RegisterClassEx(&win_class);

        const DWORD win_style=WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VISIBLE | WS_OVERLAPPEDWINDOW;

        long left=100;
        long top=100;
        long width=static_cast<u_long>(p_info_base_->width());
        long height=static_cast<u_long>(p_info_base_->height());
        RECT win_rect={
            left, top,
            left + width,
            top + height};
        AdjustWindowRect(&win_rect, win_style, false);

        hwnd=CreateWindowEx(
            WS_EX_APPWINDOW,
            class_name.c_str(),
            p_info_base_->prog_name().c_str(),
            win_style,
            left, top,
            width, height,
            nullptr,
            nullptr,
            hinstance,
            nullptr);

        SetForegroundWindow(hwnd);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)this);
#else
#error "uninplemented platform"
#endif
    }

    void post_quit_msg()
    {
#ifdef VK_USE_PLATFORM_WIN32_KHR
        PostQuitMessage(0);
#else
#error "uninplemented platform"
#endif
    }

    virtual void on_key(Key key)
    {
        switch (key) {
            case KEY_SHUTDOWN:
            case KEY_ESC:post_quit_msg();
                break;
            default:break;
        }
    }

private:

#ifdef VK_USE_PLATFORM_WIN32_KHR
    static LRESULT CALLBACK window_proc_(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        auto* shell=reinterpret_cast<Shell_base*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (!shell) return DefWindowProc(hwnd, uMsg, wParam, lParam);
        return shell->handle_message_(uMsg, wParam, lParam);
    }

    LRESULT handle_message_(UINT msg, WPARAM wparam, LPARAM lparam)
    {
        switch (msg) {
            case WM_SIZE:
            {
                UINT w=LOWORD(lparam);
                UINT h=HIWORD(lparam);
                window_resize_(w, h);
            }
            break;
            case WM_MOUSEWHEEL:
            {
                auto zDelta=GET_WHEEL_DELTA_WPARAM(wparam);
                on_key(zDelta > 0 ? KEY_WHEEL_UP : KEY_WHEEL_DOWN);
            }
            break;
            case WM_KEYDOWN:
            {
                Key key;
                switch (wparam) {
                    case VK_ESCAPE:key=KEY_ESC;
                        break;
                    case VK_UP:key=KEY_UP;
                        break;
                    case VK_DOWN:key=KEY_DOWN;
                        break;
                    case VK_LEFT:key=KEY_LEFT;
                        break;
                    case VK_RIGHT:key=KEY_RIGHT;
                        break;
                    case VK_SPACE:key=KEY_SPACE;
                        break;
                    case VK_F1:key=KEY_F1;
                        break;
                    case VK_F2:key=KEY_F2;
                        break;
                    case VK_F3:key=KEY_F3;
                        break;
                    case VK_F4:key=KEY_F4;
                        break;
                    case VK_F5:key=KEY_F5;
                        break;
                    case VK_F6:key=KEY_F6;
                        break;
                    case VK_F7:key=KEY_F7;
                        break;
                    case VK_F8:key=KEY_F8;
                        break;
                    case VK_F9:key=KEY_F9;
                        break;
                    case VK_F10:key=KEY_F10;
                        break;
                    case VK_F11:key=KEY_F11;
                        break;
                    case VK_F12:key=KEY_F12;
                        break;
                    case 0x31:key=KEY_NUM_1;
                        break;
                    case 0x32:key=KEY_NUM_2;
                        break;
                    case 0x33:key=KEY_NUM_3;
                        break;
                    case 0x34:key=KEY_NUM_4;
                        break;
                    case 0x35:key=KEY_NUM_5;
                        break;
                    case 0x36:key=KEY_NUM_6;
                        break;
                    case 0x37:key=KEY_NUM_7;
                        break;
                    case 0x38:key=KEY_NUM_8;
                        break;
                    case 0x39:key=KEY_NUM_9;
                        break;
                    case 0x30:key=KEY_NUM_0;
                        break;
                    case 'A':key=KEY_A;
                        break;
                    case 'W':key=KEY_W;
                        break;
                    case 'S':key=KEY_S;
                        break;
                    case 'D':key=KEY_D;
                        break;
                    case 'R':key=KEY_R;
                        break;
                    case 'F':key=KEY_F;
                        break;
                    default:key=KEY_UNKNOWN;
                        break;
                }
                on_key(key);
            }
            break;
            case WM_CLOSE:on_key(KEY_SHUTDOWN);
                break;
            case WM_DESTROY:post_quit_msg();
                break;
            default:return DefWindowProc(hwnd, msg, wparam, lparam);
        }
        return 0;
    }

#else
#error "uninplemented platform"
#endif

protected:

    Prog_info_base* p_info_base_;
    virtual void window_resize_(uint32_t width, uint32_t height)=0;

};
} // namespace base
