cmake_minimum_required(VERSION 3.8)

project(clustered_forward_demo_vk)

############################################################
# paths
############################################################

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

set(CMAKE_BINARY_DIR "${CMAKE_SOURCE_DIR}/build/${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}/${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(SCRIPT_DIR ${CMAKE_SOURCE_DIR}/script)
set(MODEL_DIR ${CMAKE_SOURCE_DIR}/data/models)
set(FONT_DIR ${CMAKE_SOURCE_DIR}/data/fonts)

############################################################
# flags
############################################################

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD 11)

############################################################
# Vulkan loader
############################################################

if (WIN32)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WIN32_KHR")
else (WIN32)
  message(FATAL_ERROR "unimplemented platform")
endif (WIN32)

# find the static Vulkan loader lib
find_package(Vulkan REQUIRED)
message(STATUS "dependency Vulkan_LIBRARY: ${Vulkan_LIBRARY}")
message(STATUS "dependency Vulkan_INCLUDE_DIR: ${Vulkan_INCLUDE_DIR}")

find_program(GLSLANG_VALIDATOR
    NAMES glslangValidator
    HINTS "${VULKAN_SDK}/Bin"
    )
message(STATUS "dependency GLSLANG_VALIDATOR: ${GLSLANG_VALIDATOR}")

############################################################
# other dependencies
############################################################

# python3
set(PYTHON3_EXECUTABLE_DIR "C:/msys64/usr/bin" CACHE dependencies "python3 executable directory")
find_program(PYTHON3_EXECUTABLE REQUIRED
    NAMES python3
    HINTS "${PYTHON3_EXECUTABLE_DIR}"
    )
message(STATUS "dependency PYTHON3_EXECUTABLE: ${PYTHON3_EXECUTABLE}")

# assimp
add_subdirectory(extern/assimp EXCLUDE_FROM_ALL)

############################################################
# include headers
############################################################

include_directories(
    ${Vulkan_INCLUDE_DIR}
    extern/glm
    extern/gli
    extern/imgui
    extern/assimp/include
    ${CMAKE_CACHEFILE_DIR}/extern/assimp/include 
    base
)

############################################################
# definitions
############################################################

add_definitions(
    -DGLM_FORCE_RADIANS
    -DGLM_FORCE_DEPTH_ZERO_TO_ONE
    -D_CRT_SECURE_NO_WARNINGS
    -DNOMINMAX
    -DMODEL_DIR="${MODEL_DIR}"
    -DFONT_DIR="${FONT_DIR}"
    -DENABLE_VULKAN_RENDERDOC_CAPTURE
)

############################################################
# macros
############################################################

macro(glsl_to_spirv src dst)
  add_custom_command(OUTPUT ${src}.h
      COMMAND ${PYTHON3_EXECUTABLE} ${SCRIPT_DIR}/glsl-to-spirv ${CMAKE_CURRENT_SOURCE_DIR}/${src} ${dst}/${src}.h ${GLSLANG_VALIDATOR}
      DEPENDS ${SCRIPT_DIR}/glsl-to-spirv ${CMAKE_CURRENT_SOURCE_DIR}/${src} ${GLSLANG_VALIDATOR}
      )
endmacro()

############################################################
# targets
############################################################

add_subdirectory(base)
add_subdirectory(demo)
