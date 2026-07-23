#version 330 core

out vec4 color;
in vec2 oUv;
in vec4 oColor;
uniform sampler2D diffuseTex;

void main()
{
    vec4 base = texture(diffuseTex, oUv);
    if (base.a < 0.01) discard;
    color = base * oColor;
}
