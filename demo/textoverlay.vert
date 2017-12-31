#version 450 core

layout (location = 0) in vec4 pos_in;
layout (location = 0) out vec2 uv_out;

out gl_PerVertex 
{
    vec4 gl_Position;   
};

void main(void)
{
    gl_Position = vec4(pos_in.xy, 0.0, 1.0);
    uv_out = pos_in.zw;
}
