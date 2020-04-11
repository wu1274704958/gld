#version 330 core
layout (location = 0) in vec3 position;

uniform mat4 perspective;
uniform mat4 world;
uniform mat4 model;

void main()
{
    gl_Position = perspective * model * vec4(position, 1.0f);
}