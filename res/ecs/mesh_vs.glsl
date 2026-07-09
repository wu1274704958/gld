#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

out vec2 oUv;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    oUv = aUv;
}
