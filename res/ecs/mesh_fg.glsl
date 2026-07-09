#version 330 core
in vec2 oUv;
out vec4 FragColor;

uniform sampler2D tex;
uniform int hasTex;
uniform vec4 color;

void main()
{
    vec4 base = (hasTex == 1) ? texture(tex, oUv) : vec4(1.0);
    FragColor = base * color;
}
