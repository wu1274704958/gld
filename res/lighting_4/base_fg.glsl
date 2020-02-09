#version 450 core

#include "light.glsl"

in vec3 oNormal; 
in vec3 oVpos; 
in vec2 oUv;
in vec3 o_pl_pos;
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
uniform float pl_constant;
uniform float pl_linear;
uniform float pl_quadratic;
uniform vec3  pl_color;

uniform vec3 view_pos;
void main() 
{ 
    vec3 obj_color = texture(diffuseTex, oUv).rgb;
    
    vec3 light_dirction = -normalize(dirct_light.dir);

    vec3 view_dir = normalize(view_pos - oVpos);

    vec3 ambient = dirct_light.color * ambient_strength;

    vec3 diffuse = (obj_color * dirct_light.color) * max(dot(light_dirction,oNormal),0.0f);

    vec3 light_reflect = reflect(-light_dirction,oNormal);

    float spec = pow(max(dot(view_dir,light_reflect),0.0f),shininess);

    vec3 specular = specular_strength * spec * (dirct_light.color * texture(specularTex,oUv).rgb);

    //point light calc
    vec3 pl_dir = normalize(o_pl_pos - oVpos);
    float dist = length(o_pl_pos - oVpos);

    float attenuation = 1.0 / (pl_constant + pl_linear * dist + 
                pl_quadratic * (dist * dist));

    vec3 pl_diffuse = (pl_color *  obj_color) * (max(dot(pl_dir,oNormal),0.0f) * attenuation);

    vec3 pl_reflect = reflect(-pl_dir,oNormal);

    float pl_spec = pow(max(dot(view_dir,pl_reflect),0.0f),shininess);

    vec3 pl_specular = attenuation * specular_strength * pl_spec * (pl_color * texture(specularTex,oUv).rgb);

    color = vec4( (ambient + diffuse + specular + pl_diffuse + pl_specular),1.0f);
}