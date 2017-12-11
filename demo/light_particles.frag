#version 450 core

layout (location = 0) in vec3 color_in;
layout (location = 0) out vec4 frag_color;

void main()
{
    float r = distance(gl_PointCoord.xy, vec2(0.5f));
    if (r > .5f) discard;
    frag_color = vec4(color_in, 4.f * pow(0.5f - r, 2.f) );
}
