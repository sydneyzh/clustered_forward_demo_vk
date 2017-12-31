#version 450 core

layout (location = 0) in vec2 uv_in;
layout (set = 0, binding = 0) uniform sampler2D font_tex;
layout (location = 0) out vec4 frag_color;

void main(void)
{
    vec4 color = texture(font_tex, vec2(uv_in.x, 1.f - uv_in.y) );
    frag_color = vec4(vec3(color.r), color.a);
}
