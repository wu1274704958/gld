#version 330 core

out vec4 color;

in vec2 oUv;
in vec4 oColor;

uniform sampler2D diffuseTex;

void main()
{
    vec4 shadow = texture(diffuseTex, oUv);
    float strength = shadow.r;
    if (strength < 0.01)
        discard;

    color = vec4(0.0, 0.0, 0.0, strength * 0.62 * oColor.a);
}
