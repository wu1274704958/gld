#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D screenTexture;

uniform float TexWidth;
uniform float TexHeight;


vec3 f1();
vec3 f2();
vec3 f3(vec2 offsets[9]);
vec3 f4(vec2 offsets[9]);
vec3 f5(vec2 offsets[9]);

void main()
{ 

    float offsetx = 1.0 / TexWidth;
    float offsety = 1.0 / TexHeight;

    vec2 offsets[9] = vec2[](
        vec2(-offsetx,  offsety     ), // 左上
        vec2( 0.0f,     offsety     ), // 正上
        vec2( offsetx,  offsety     ), // 右上
        vec2(-offsetx,  0.0f        ),   // 左
        vec2( 0.0f,     0.0f        ),   // 中
        vec2( offsetx,  0.0f        ),   // 右
        vec2(-offsetx,  -offsety    ), // 左下
        vec2( 0.0f,    -offsety     ), // 正下
        vec2( offsetx,  -offsety    )  // 右下
    );
    if(gl_FragCoord.x < TexWidth / 2 && gl_FragCoord.y < TexHeight / 2 )
    {
        FragColor = vec4(f2(),1.0);
    }else
    if(gl_FragCoord.x > TexWidth / 2 && gl_FragCoord.y < TexHeight / 2 )
    {
        FragColor = vec4(f3(offsets),1.0);
    }
    else
    if(gl_FragCoord.x < TexWidth / 2 && gl_FragCoord.y > TexHeight / 2 )
    {
        FragColor = vec4(f4(offsets),1.0);
    }else{
        FragColor = vec4(f5(offsets),1.0);
    }

    // FragColor = 
    //     vec4(
    //         //f1()
    //         //f3(offsets)
    //         //f4(offsets)
    //         f5(offsets)
    //         ,1.0);
}

vec3 f1()
{
    return vec3(1.0) -  vec3(texture(screenTexture, TexCoords));
}

vec3 f2()
{
    FragColor = texture(screenTexture, TexCoords);
    float average = (0.2126*FragColor.r + 0.7152 * FragColor.g + 0.0722 * FragColor.b);
    return vec4(average, average, average, 1.0);
}

vec3 color_for_kernel(float kernel[9],vec2 offsets[9])
{
    vec3 col = vec3(0.0);
    for(int i = 0; i < 9; i++)
    {
        col +=  kernel[i] * vec3(texture(screenTexture, TexCoords.st + offsets[i]));
    }
    return col;
}

vec3 f3(vec2 offsets[9])
{
    float kernel[9] = float[](
        -1, -1, -1,
        -1,  9, -1,
        -1, -1, -1
    );
    return color_for_kernel(kernel,offsets);
}

vec3 f4(vec2 offsets[9])
{
    float kernel[9] = float[](
        1.0 / 16, 2.0 / 16, 1.0 / 16,
    2.0 / 16, 4.0 / 16, 2.0 / 16,
    1.0 / 16, 2.0 / 16, 1.0 / 16 
    );
    return color_for_kernel(kernel,offsets);
}

vec3 f5(vec2 offsets[9])
{
    float kernel[9] = float[](
        1.0,1.0,1.0,
        1.0,-8.0,1.0,
        1.0,1.0,1.0
    );
    return color_for_kernel(kernel,offsets);
}