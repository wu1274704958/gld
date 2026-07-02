#version 330 core
uniform mat4 perspective;
uniform mat4 world;
uniform mat4 model;

layout(location = 0) in vec3 vposition;
layout(location = 1) in vec3 vcolor;
layout(location = 2) in float vsize;   // only enabled for the point VAO

out vec3 f_color;

void main()
{
    gl_Position  = perspective * world * model * vec4(vposition, 1.0);
    gl_PointSize = vsize;               // ignored when drawing GL_LINES
    f_color      = vcolor;
}
