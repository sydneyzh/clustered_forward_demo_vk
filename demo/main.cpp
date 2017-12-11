#include "Camera.hpp"
#include "Prog_info.hpp"
#include "Shell.hpp"
#include "Program.hpp"

int main()
{
    /*
    auto p_info = new Prog_info {};
    auto p_camera = new base::Camera();
    auto p_shell = new Shell {p_info, p_camera};
    auto p_program = new Program {p_info, p_shell, true, p_camera};
    p_program->init();
    p_program->run();
    delete p_program;
    delete p_shell;
    delete p_camera;
    delete p_info;
    */
    {
        Prog_info prog_info{};
        base::Camera camera{};
        Shell shell{&prog_info, &camera};
        Program program{&prog_info, &shell, true, &camera};
        program.init();
        program.run();
    }
    printf("press any key...");
    getchar();
    return 0;
}