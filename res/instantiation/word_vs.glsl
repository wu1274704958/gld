#version 330 core
uniform mat4 perspective;
uniform mat4 world; 
layout(location = 0) in vec3 vposition;
layout(location = 1) in vec4 vuv;
layout(location = 2) in vec4 vuv2;
layout(location = 3) in vec4 vcolor;
layout(location = 4) in mat4 vmodel;
layout(location = 8) in mat4 local;

out vec2 oUv; 
out vec4 oColor;
void main() 
{
    gl_Position = perspective * world * vmodel * local * vec4(vposition,1.0f);
    if(gl_VertexID == 0)
    {
        oUv = vuv.xy;
    }else if(gl_VertexID == 1)
    {
        oUv = vuv.zw;
    }else if(gl_VertexID == 2)
    {
        oUv = vuv2.xy;
    }else if(gl_VertexID == 3)
    {
        oUv = vuv2.zw;
    }
    
    oColor = vcolor;
}