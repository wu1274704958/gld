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

#endif