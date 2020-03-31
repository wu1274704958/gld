#version 320 es
precision mediump float;

#include "../lighting_4/light.glsl"
#include "../lighting_6/comm.glsl"
in GS_OUT{
    vec3 goNormal; 
    vec3 goVpos;
    vec2 goUv;
};

out vec4 color;

layout (std140,binding = 1) uniform PL{
    PointLight pointLight[PL_LEN];
    uint pl_len;
};

layout (std140,binding = 2) uniform SPL{
    SpotLight spotLight;
};

layout (std140,binding = 0) uniform DL{
    DirctLight dirct_light;
};

uniform vec3 view_pos;
uniform samplerCube skybox;
uniform float refract_rate;

void main() 
{ 
    vec3 I = normalize( goVpos - view_pos );
    vec3 R = refract(I,normalize(goNormal),refract_rate);


    vec3 obj_color = texture(skybox, R).rgb;

    color = vec4( 
        obj_color
        ,1.0f);
}