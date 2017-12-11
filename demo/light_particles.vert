#version 450 core

layout(location = 0) in vec4 pos_range_in;
layout(location = 1) in vec4 color_in;

layout(set = 0, binding = 0) uniform UBO
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
    float gl_PointSize;
};

layout (location = 0) out vec3 color_out;

void main()
{
    color_out = color_in.xyz;
    gl_Position = ubo_in.projection_clip * ubo_in.view * vec4(pos_range_in.xyz, 1.f);
    gl_PointSize =  0.2f * ubo_in.cam_far / gl_Position.z;
}
