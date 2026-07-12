#version 430 core

in vec2 oUv;
out vec4 FragColor;

uniform sampler2D tex;
uniform int hasTex;
uniform vec4 color;
uniform float emissiveStrength;

void main()
{
    vec4 base = (hasTex == 1) ? texture(tex, oUv) : vec4(1.0);
    vec4 c = base * color;
    FragColor = vec4(c.rgb * emissiveStrength, c.a);
}
