#version 330 core

out vec4 color; 

in GS_OUT{
    vec2 goUv;
    vec3 goColor;
} gs_out;

float gray()
{
    if(gs_out.goUv.y > 0.5f)
        return (1.f - gs_out.goUv.y) * 2.f;
    if(gs_out.goUv.y <= 0.5f)
        return (gs_out.goUv.y) * 2.f;
}


float random (vec2 st) {
    return fract(sin(dot(st.xy,vec2(12.9898,78.233)))*43758.5453123);
}


highp float rand(float n){
    return fract(sin(mod(dot(n, 12.9898),3.14))*43758.5453);
}

highp float easeOutExpo(float t, float b, float c, float d)
{
	return t ==  d ? d : d - pow(2.f,-10.f * t);
}

highp float linear(float t, float b, float c, float d)
{
    return c * t / d + b;
}

highp float easeOut(float t, float b, float c, float d)
{
    return (t == d) ? b + c : c * (-pow(2.f, -10.f * t / d) + 1.f) + b;
}


highp float f1(float n){
    if(n > 0.9f) return 1.f;
    return linear(n,0.f,1.f,1.f);
}

void main() 
{ 

    vec3 fill_color = gs_out.goColor;
    float g = gray();
    if(g > 0.86f)
    {
        color = vec4( fill_color + (vec3(1.f) * 0.16f * f1(g)) ,1.0f);
        return;
    }
    if(g > 0.1f)
    {
        color = vec4( fill_color * f1(g) ,1.0f);
    }else{
        if(f1(g) * random(gs_out.goUv) <= 0.0f)
        {
            discard;
        }else
        color = vec4( fill_color * f1(g) * random(gs_out.goUv) ,1.0f);
    }
}