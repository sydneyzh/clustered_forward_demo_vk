#version 450 core

layout(location = 0) in vec3 pos_in;
layout(location = 1) in vec3 normal_in;
layout(location = 2) in vec2 uv_in;
layout(location = 3) in vec3 tangent_in;
layout(location = 4) in vec3 bitangent_in;

layout(set = 2, binding = 0) uniform UBO
{
    mat4 view;
    mat4 normal;
    mat4 model;
    mat4 projection_clip;

    vec2 tile_size; // xy
    uvec2 grid_dim; // xy

    vec3 cam_pos;
    float cam_far;

    vec2 resolution;
    uint num_lights;
} ubo_in;

out gl_PerVertex
{
    vec4 gl_Position;
};

layout (location= 0) out vec4 world_pos_out;
layout (location= 1) out vec3 world_normal_out;
layout (location= 2) out vec3 world_tangent_out;
layout (location= 3) out vec3 world_bitangent_out;
layout (location= 4) out vec2 uv_out;

void main()
{
    world_pos_out = ubo_in.model * vec4(pos_in, 1.f);
    world_normal_out = normalize((ubo_in.normal * vec4(normal_in, 0.f)).xyz);
    world_tangent_out = normalize((ubo_in.normal * vec4(tangent_in, 0.f)).xyz);
    world_bitangent_out = normalize((ubo_in.normal * vec4(bitangent_in, 0.f)).xyz);
    uv_out = uv_in;
    gl_Position = ubo_in.projection_clip * ubo_in.view * world_pos_out;
}
