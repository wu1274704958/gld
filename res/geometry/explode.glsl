#version 430 core

layout (triangles) in;
layout (triangle_strip, max_vertices = 3) out;

in VS_OUT{
    vec3 oNormal; 
    vec3 oVpos;
    vec2 oUv;
}gs_in[];

out GS_OUT{
    vec3 goNormal; 
    vec3 goVpos;
    vec2 goUv;
};

uniform float time;

vec3 GetNormal()
{
   vec3 a = vec3(gl_in[0].gl_Position) - vec3(gl_in[1].gl_Position);
   vec3 b = vec3(gl_in[2].gl_Position) - vec3(gl_in[1].gl_Position);
   return normalize(cross(a, b));
}

vec4 explode(vec4 position, vec3 normal)
{
    float magnitude = 3.0;
    vec3 direction = normal * ((sin(time) + 1.0) / 2.0) * magnitude; 
    return position + vec4(direction, 0.0);
}

void main() {
        
    vec3 normal = GetNormal();//gs_in[0].oNormal;
    //for(uint i = 0;i < (uint)3;++i)
    {
        gl_Position = explode(gl_in[0].gl_Position, normal);
        goNormal = gs_in[0].oNormal;
        goUv = gs_in[0].oUv;
        goVpos = gs_in[0].oVpos;
        EmitVertex();

        gl_Position = explode(gl_in[1].gl_Position, normal);
        goNormal = gs_in[1].oNormal;
        goUv = gs_in[1].oUv;
        goVpos = gs_in[1].oVpos;
        EmitVertex();

        gl_Position = explode(gl_in[2].gl_Position, normal);
        goNormal = gs_in[2].oNormal;
        goUv = gs_in[2].oUv;
        goVpos = gs_in[2].oVpos;
        EmitVertex();
        EndPrimitive();
    }
}