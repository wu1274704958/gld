#ifndef BASE_LIGHT
#define BASE_LIGHT

struct DirctLight{
    vec3 color;
    vec3 dir;
};

struct PointLight{
    
    float constant;
    float linear;
    float quadratic;
    vec3 color;
    vec3 pos;
};

struct SpotLight{
    float constant;
    float linear;
    float quadratic;
    float cut_off;
    vec3 color;
    vec3 pos;
    vec3 dir;
    float outer_cut_off;
};

#endif