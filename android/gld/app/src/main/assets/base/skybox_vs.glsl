#version 320 es
precision mediump float;
layout (location = 0) in vec3 aPos;

out vec3 TexCoords;

uniform mat4 perspective;
uniform mat4 world;

void main()
{
    TexCoords = aPos;
    vec4 pos = perspective * 
    //world * vec4(aPos, 1.0);
    mat4(mat3(world)) * vec4(aPos, 1.0);
    gl_Position = pos.xyzz;
}