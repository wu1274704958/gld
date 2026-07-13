#version 330 core

out vec4 color;

in vec2 oUv;
in vec4 oColor;
in vec4 oMParam0;

uniform sampler2D diffuseTex;

void main()
{
    float mask = texture(diffuseTex, oUv).r;
    if (mask < 0.035)
        discard;

    float emissive = max(oMParam0.x, 1.0);
    color = vec4(oColor.rgb * emissive * mask , 1);
}
