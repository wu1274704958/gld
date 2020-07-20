#version 330 core

layout (lines) in;
layout (triangle_strip, max_vertices = 4) out;

float LineWidth = 0.03;


out GS_OUT{
    vec2 goUv;
} gs_out;


void main() {
     vec3 l_dir = normalize(vec3(gl_in[1].gl_Position) - vec3(gl_in[0].gl_Position));
     vec3 dir = normalize(cross(vec3(0.f,0.f,1.f),l_dir));

     if( dot(vec3(0.f,0.f,1.f),l_dir) == 1.f )
     {
        dir = normalize(cross(vec3(1.f,0.f,0.f),l_dir));
     }
     
     vec3 up = LineWidth * 0.5f * dir;
     vec3 down = LineWidth * 0.5f * -dir;

     gl_Position = gl_in[0].gl_Position + vec4(up,0.f);
     gs_out.goUv = vec2(0.0f,0.0f);
     EmitVertex();    
     gl_Position = gl_in[0].gl_Position + vec4(down,0.f);
     gs_out.goUv = vec2(0.0f,1.0f);
     EmitVertex(); 
     gl_Position = gl_in[1].gl_Position + vec4(up,0.f);
     gs_out.goUv = vec2(1.0f,0.0f);
     EmitVertex();    
     gl_Position = gl_in[1].gl_Position + vec4(down,0.f);
     gs_out.goUv = vec2(1.0f,1.0f);
     EmitVertex();
     EndPrimitive();
}