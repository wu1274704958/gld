#version 330 core

layout (lines) in;
//layout (line_strip, max_vertices = 28) out;
layout (triangle_strip, max_vertices = 52) out;

float LineWidth = 0.1;
int seg = 12;

in VS_OUT {
    vec3 f_color;
} vs_out[];

out GS_OUT{
    vec2 goUv;
    vec3 goColor;
} gs_out;


mat3 create_mat(vec3 n,float o)
{
    mat3 m = mat3(
        n.x * n.x * (1.f - cos(o)) + cos(o)       ,  n.x * n.y * (1.f - cos(o)) - n.z * sin(o) , n.x * n.z * (1.f - cos(o)) + n.y * sin(o),
        n.x * n.y * (1.f - cos(o)) + n.z * sin(o) ,  n.y * n.y * (1.f - cos(o)) + cos(o)       , n.y * n.z * (1.f - cos(o)) - n.x * sin(o),
        n.x * n.z * (1.f - cos(o)) - n.y * sin(o) ,  n.y * n.z * (1.f - cos(o)) + n.x * sin(o) , n.z * n.z * (1.f - cos(o)) + cos(o)
    );

    return m;
}

void  emit(vec4 p,vec2 uv,vec3 color)
{
     gl_Position = p;
     gs_out.goUv = uv;
     gs_out.goColor = color;
     EmitVertex();   
}

void main() {
     vec3 l_dir = normalize(vec3(gl_in[1].gl_Position) - vec3(gl_in[0].gl_Position));

     vec3 vp = vec3(0.f,0.f,0.f) - vec3(gl_in[1].gl_Position);

     vec3 dir = normalize(cross(vp,l_dir));

     float radius = LineWidth * 0.5f;
     float chang = length(l_dir);
     float hc_rate = radius / chang;
     
     vec3 up = radius * dir;
     vec3 down = radius * -dir;

     float ang = radians(180.f) / float(seg - 1);

     float curr_ang = 0.f;

     vec3 rot_dir = normalize(cross(dir,l_dir));

     for(int i = 0;i < seg;++i)
     {
        mat3 m = create_mat(rot_dir,curr_ang);
        vec3 first = down;
        first = m * first;
        //float dui = radius * sin(curr_ang);
        //float lin = radius * cos(-curr_ang);
        //vec2 uv = vec2( (1.f - dui / radius) * hc_rate , lin / radius);

        emit(gl_in[0].gl_Position + vec4(first, 0.f),vec2(0.0f,0.5f - cos(-curr_ang) * 0.5f ),vs_out[0].f_color);
        emit(gl_in[0].gl_Position ,vec2(hc_rate,0.5f),vs_out[0].f_color);

        curr_ang -= ang;
     }


     gl_Position = gl_in[0].gl_Position + vec4(up,0.f);
     gs_out.goUv = vec2(hc_rate,0.0f);
     gs_out.goColor = vs_out[0].f_color;
     EmitVertex();    
     gl_Position = gl_in[0].gl_Position + vec4(down,0.f);
     gs_out.goUv = vec2(hc_rate,1.0f);
     gs_out.goColor = vs_out[0].f_color;
     EmitVertex(); 
     gl_Position = gl_in[1].gl_Position + vec4(up,0.f);
     gs_out.goUv = vec2(1.0f - hc_rate,0.0f);
     gs_out.goColor = vs_out[1].f_color;
     EmitVertex();    
     gl_Position = gl_in[1].gl_Position + vec4(down,0.f);
     gs_out.goUv = vec2(1.0f - hc_rate,1.0f);
     gs_out.goColor = vs_out[1].f_color;
     EmitVertex();

     curr_ang = 0.f;

     for(int i = 0;i < seg;++i)
     {
        mat3 m = create_mat(rot_dir,curr_ang);
        vec3 first = up;
        first = m * first;

        emit(gl_in[1].gl_Position ,vec2(1.0f - hc_rate,0.5f),vs_out[1].f_color);
        emit(gl_in[1].gl_Position + vec4(first, 0.f),vec2(0.0f, 0.5f - cos(-curr_ang) * 0.5f ),vs_out[1].f_color);
        

        curr_ang -= ang;
     }


     EndPrimitive();
}