#include "Camera.hpp"
#include "Prog_info.hpp"
#include "Shell.hpp"
#include "Program.hpp"

int main()
{
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