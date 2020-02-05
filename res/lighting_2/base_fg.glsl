#version 330 core
in vec3 oNormal; 
in vec3 oVpos; 
in vec2 oUv;
out vec4 color; 
uniform sampler2D diffuseTex;
uniform vec3 light_color;
uniform float ambient_strength;
uniform float specular_strength;
uniform float shininess;

uniform vec3 light_pos; 
uniform vec3 view_pos;
void main() 
{ 
    vec3 obj_color = vec3(texture(diffuseTex, oUv));
    
    vec3 light_dir = normalize(light_pos - oVpos);

    vec3 view_dir = normalize(view_pos - oVpos);

    vec3 ambient = light_color * ambient_strength;

    vec3 diffuse = obj_color * max(dot(light_dir,oNormal),0.0f);

    vec3 light_reflect = reflect(-light_dir,oNormal);

    float spec = pow(max(dot(view_dir,light_reflect),0.0f),shininess);

    vec3 specular = specular_strength * spec * light_color;

    color = vec4( (ambient + diffuse + specular),1.0f);
}