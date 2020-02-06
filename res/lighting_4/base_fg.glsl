#version 330 core
in vec3 oNormal; 
in vec3 oVpos; 
in vec2 oUv;
out vec4 color; 
uniform sampler2D diffuseTex;
uniform sampler2D specularTex;
uniform vec3 light_color;
uniform float ambient_strength;
uniform float specular_strength;
uniform float shininess;

uniform vec3 light_dir; 
uniform vec3 view_pos;
void main() 
{ 
    vec3 obj_color = texture(diffuseTex, oUv).rgb;
    
    vec3 light_dirction = -normalize(light_dir);

    vec3 view_dir = normalize(view_pos - oVpos);

    vec3 ambient = light_color * ambient_strength;

    vec3 diffuse = obj_color * max(dot(light_dirction,oNormal),0.0f);

    vec3 light_reflect = reflect(-light_dirction,oNormal);

    float spec = pow(max(dot(view_dir,light_reflect),0.0f),shininess);

    vec3 specular = specular_strength * spec * (light_color * texture(specularTex,oUv).rgb);

    color = vec4( (ambient + diffuse + specular),1.0f);
}