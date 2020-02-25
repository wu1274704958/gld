#version 320 es
precision mediump float;

#include "../lighting_4/light.glsl"
#include "comm.glsl"
in vec3 oNormal; 
in vec3 oVpos; 
in vec2 oUv;
in PointLight o_pl[PL_LEN];
in SpotLight o_spl;
flat in uint o_pl_len;
out vec4 color;

layout (std140,binding = 0) uniform DL{
    DirctLight dirct_light;
};

uniform sampler2D diffuseTex;
uniform sampler2D specularTex;
uniform float ambient_strength;
uniform float specular_strength;
uniform float shininess;
//point light 

uniform vec3 view_pos;

vec3 calc_point_light(vec3 obj_color,vec3 view_dir,PointLight pl);
vec3 calc_direct_light(vec3 obj_color,vec3 view_dir,DirctLight dirct_light);
vec3 calc_spot_light(vec3 obj_color,vec3 view_dir,SpotLight spot_light);

void main() 
{ 
    vec3 obj_color = texture(diffuseTex, oUv).rgb;

    vec3 view_dir = normalize(view_pos - oVpos);

    vec3 pl_color = vec3(0.0f,0.0f,0.0f);

    for(uint i = 0;i < o_pl_len;++i)
    {
        pl_color += calc_point_light(obj_color,view_dir,o_pl[i]);
    }

    color = vec4( (
        calc_direct_light(obj_color,view_dir, dirct_light) + 
        pl_color //+ 
        //calc_spot_light(obj_color,view_dir,o_spl)
        ),1.0f);
}

vec3 calc_point_light(vec3 obj_color,vec3 view_dir,PointLight pl)
{

    vec3 pl_dir = normalize(pl.pos - oVpos);

    float dist = length(pl.pos - oVpos);

    float attenuation = 1.0 / (pl.constant + pl.linear * dist + 
                pl.quadratic * (dist * dist));

    vec3 pl_diffuse = (pl.color *  obj_color) * (max(dot(pl_dir,oNormal),0.0f) * attenuation);

    vec3 pl_reflect = reflect(-pl_dir,oNormal);

    float pl_spec = pow(max(dot(view_dir,pl_reflect),0.0f),shininess);

    vec3 pl_specular = attenuation * specular_strength * pl_spec * (pl.color * texture(specularTex,oUv).rgb);
    
    return pl_diffuse + pl_specular;
}

vec3 calc_direct_light(vec3 obj_color,vec3 view_dir,DirctLight dirct_light)
{
    vec3 light_dirction = -normalize(dirct_light.dir);

    vec3 ambient = dirct_light.color * ambient_strength;

    vec3 diffuse = (obj_color * dirct_light.color) * max(dot(light_dirction,oNormal),0.0f);

    vec3 light_reflect = reflect(-light_dirction,oNormal);

    float spec = pow(max(dot(view_dir,light_reflect),0.0f),shininess);

    vec3 specular = specular_strength * spec * (dirct_light.color * texture(specularTex,oUv).rgb);

    return ambient + diffuse + specular;
}

vec3 calc_spot_light(vec3 obj_color,vec3 view_dir,SpotLight spot_light)
{
    vec3 pl_dir = normalize(spot_light.pos - oVpos);

    float theta = dot(-pl_dir,normalize(spot_light.dir));
    
    float epsilon   = spot_light.cut_off - spot_light.outer_cut_off;
    float intensity = clamp((theta - spot_light.outer_cut_off) / epsilon, 0.0, 1.0);   

    float dist = length(spot_light.pos - oVpos);

    float attenuation = 1.0 / (spot_light.constant + spot_light.linear * dist + spot_light.quadratic * (dist * dist));

    vec3 pl_diffuse = (spot_light.color *  obj_color) * (max(dot(pl_dir,oNormal),0.0f) * attenuation);

    vec3 pl_reflect = reflect(-pl_dir,oNormal);

    float pl_spec = pow(max(dot(view_dir,pl_reflect),0.0f),shininess);

    vec3 pl_specular = attenuation * specular_strength * pl_spec * (spot_light.color * texture(specularTex,oUv).rgb);

    return (intensity * pl_diffuse) + (intensity * pl_specular);
    
}