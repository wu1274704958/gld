#version 430 core

#include "../lighting_4/light.glsl"

in vec3 oNormal; 
in vec3 oVpos; 
in vec2 oUv;
out vec4 color;

layout (std140,binding = 0) uniform DL{
    DirctLight dirct_light;
};

layout (std140,binding = 1) uniform PL{
    PointLight pointLight;
};

layout (std140,binding = 2) uniform SPL{
    SpotLight spotLight;
};

uniform sampler2D diffuseTex;
uniform sampler2D specularTex;
uniform float ambient_strength;
uniform float specular_strength;
uniform float shininess;
uniform int use_blinn;
//point light 

uniform vec3 view_pos;

vec3 calc_point_light(vec3 obj_color,vec3 view_dir,PointLight pl);
vec3 calc_direct_light(vec3 obj_color,vec3 view_dir,DirctLight dirct_light);
vec3 calc_spot_light(vec3 obj_color,vec3 view_dir,SpotLight spot_light);
vec3 calc_direct_light_blinn(vec3 obj_color,vec3 view_dir,DirctLight dirct_light);
vec3 calc_point_light_blinn(vec3 obj_color,vec3 view_dir,PointLight pl);


float blinn_direct_spec(vec3 view_dir)
{
    vec3 light_dirction = -normalize(dirct_light.dir);
    vec3 halfwayDir = normalize( light_dirction + view_dir );
    return pow(max(dot(oNormal,halfwayDir),0.0f),shininess);
}

float blinn_point_spec(PointLight pl,vec3 view_dir)
{
    vec3 pl_dir = normalize(pl.pos - oVpos);
    vec3 halfwayDir = normalize( pl_dir + view_dir );
    return pow(max(dot(oNormal,halfwayDir),0.0f),shininess);
}

void main() 
{ 
    vec3 obj_color = texture(diffuseTex, oUv).rgb;

    vec3 view_dir = normalize(view_pos - oVpos);
    if(use_blinn == 1)
    {
        color = vec4( (
        //calc_direct_light(obj_color,view_dir, dirct_light) //+ 
        calc_direct_light_blinn(obj_color,view_dir,dirct_light) +
        calc_point_light_blinn(obj_color,view_dir,pointLight) 
        //calc_spot_light(obj_color,view_dir,spotLight)
        ),1.0f);
    }else{
        color = vec4( (
        //calc_direct_light(obj_color,view_dir, dirct_light) //+ 
        calc_direct_light(obj_color,view_dir,dirct_light) +
        calc_point_light(obj_color,view_dir,pointLight) 
        //calc_spot_light(obj_color,view_dir,spotLight)
        ),1.0f);
    }
    
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

vec3 calc_point_light_blinn(vec3 obj_color,vec3 view_dir,PointLight pl)
{

    vec3 pl_dir = normalize(pl.pos - oVpos);

    float dist = length(pl.pos - oVpos);

    float attenuation = 1.0 / (pl.constant + pl.linear * dist + 
                pl.quadratic * (dist * dist));

    vec3 pl_diffuse = (pl.color *  obj_color) * (max(dot(pl_dir,oNormal),0.0f) * attenuation);

    vec3 pl_reflect = reflect(-pl_dir,oNormal);

    float pl_spec = blinn_point_spec(pl,view_dir);

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

vec3 calc_direct_light_blinn(vec3 obj_color,vec3 view_dir,DirctLight dirct_light)
{
    vec3 light_dirction = -normalize(dirct_light.dir);

    vec3 ambient = dirct_light.color * ambient_strength;

    vec3 diffuse = (obj_color * dirct_light.color) * max(dot(light_dirction,oNormal),0.0f);

    vec3 light_reflect = reflect(-light_dirction,oNormal);

    float spec = blinn_direct_spec(view_dir);

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