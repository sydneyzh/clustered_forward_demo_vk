set(LIBNAME base)

add_library(${LIBNAME} STATIC
    Aabb.hpp
    assert.hpp
    Buffer.hpp
    Camera.hpp
    color.hpp
    Device.hpp
    math.hpp
    Model.hpp
    Physical_device.hpp
    Prog_info_base.hpp
    Program_base.hpp
    random.hpp
    Render_pass.hpp
    Shader.hpp
    Shell_base.hpp
    Swapchain.hpp
    Texture.hpp
    Timer.hpp
    tools.hpp
    Render_target.hpp
)

target_link_libraries(${LIBNAME}
    ${Vulkan_LIBRARIES}
    assimp
)

set_target_properties(${LIBNAME} PROPERTIES LINKER_LANGUAGE CXX)

