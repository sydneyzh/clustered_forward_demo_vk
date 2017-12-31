set(TARGET_NAME demo)
set(SPIRV_DIR ${CMAKE_CURRENT_SOURCE_DIR})

glsl_to_spirv(simple.vert ${SPIRV_DIR})

glsl_to_spirv(clustering.vert ${SPIRV_DIR})
glsl_to_spirv(clustering.frag ${SPIRV_DIR})

glsl_to_spirv(cluster_forward.vert ${SPIRV_DIR})
glsl_to_spirv(cluster_forward.frag ${SPIRV_DIR})

glsl_to_spirv(calc_light_grids.comp ${SPIRV_DIR})
glsl_to_spirv(calc_grid_offsets.comp ${SPIRV_DIR})
glsl_to_spirv(calc_light_list.comp ${SPIRV_DIR})

glsl_to_spirv(light_particles.vert ${SPIRV_DIR})
glsl_to_spirv(light_particles.frag ${SPIRV_DIR})

glsl_to_spirv(textoverlay.vert ${SPIRV_DIR})
glsl_to_spirv(textoverlay.frag ${SPIRV_DIR})

add_executable(${TARGET_NAME}
    main.cpp
    Prog_info.hpp
    Shell.hpp
    Light.hpp
    Swapchain.hpp
    Model.hpp
    Text_overlay.hpp
    Program.hpp
    simple.vert.h
    clustering.vert.h
    clustering.frag.h
    cluster_forward.vert.h
    cluster_forward.frag.h
    light_particles.vert.h
    light_particles.frag.h
    textoverlay.vert.h
    textoverlay.frag.h
    calc_light_grids.comp.h
    calc_grid_offsets.comp.h
    calc_light_list.comp.h
    )
target_link_libraries(${TARGET_NAME}
    ${Vulkan_LIBRARY}
    assimp
    )
