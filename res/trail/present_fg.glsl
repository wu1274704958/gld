#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D image;  // accumulation tail to composite
uniform float factor;     // composite gain (decay * strength)

void main()
{
    FragColor = vec4(texture(image, TexCoords).rgb * factor, 1.0);
}
