#version 330 core

layout (lines) in;
layout (triangle_strip, max_vertices = 4) out;

uniform float line_width;

in VS_OUT {
    vec3 f_color;
} vs_out[];

out GS_OUT{
    vec2 goUv;
    vec3 goColor;
} gs_out;


void main() {
     vec3 l_dir = normalize(vec3(gl_in[1].gl_Position) - vec3(gl_in[0].gl_Position));
     vec3 dir = normalize(cross(vec3(gl_in[1].gl_Position) - vec3(0.f,0.f,0.f),l_dir));

     //if( dot(vec3(0.f,0.f,1.f),l_dir) == 1.f )
     //{
     //   dir = normalize(cross(vec3(1.f,0.f,0.f),l_dir));
     //}
    

     vec3 up = line_width * 0.5f * dir;
     vec3 down = line_width * 0.5f * -dir;

     

     gl_Position = gl_in[0].gl_Position + vec4(up,0.f);
     gs_out.goUv = vec2(0.0f,0.0f);
     gs_out.goColor = vs_out[0].f_color;
     EmitVertex();    
     gl_Position = gl_in[0].gl_Position + vec4(down,0.f);
     gs_out.goUv = vec2(0.0f,1.0f);
     gs_out.goColor = vs_out[0].f_color;
     EmitVertex(); 
     gl_Position = gl_in[1].gl_Position + vec4(up,0.f);
     gs_out.goUv = vec2(1.0f,0.0f);
     gs_out.goColor = vs_out[1].f_color;
     EmitVertex();    
     gl_Position = gl_in[1].gl_Position + vec4(down,0.f);
     gs_out.goUv = vec2(1.0f,1.0f);
     gs_out.goColor = vs_out[1].f_color;
     EmitVertex();
     EndPrimitive();
}