#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D image;
uniform int  horizontal;   // 1 = horizontal pass, 0 = vertical pass
uniform vec2 texel;        // 1.0 / textureSize

// 9-tap Gaussian weights
const float weight[5] = float[](
    0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216
);

void main()
{
    vec3 result = texture(image, TexCoords).rgb * weight[0];

    if (horizontal == 1)
    {
        for (int i = 1; i < 5; ++i)
        {
            vec2 off = vec2(texel.x * float(i), 0.0);
            result += texture(image, TexCoords + off).rgb * weight[i];
            result += texture(image, TexCoords - off).rgb * weight[i];
        }
    }
    else
    {
        for (int i = 1; i < 5; ++i)
        {
            vec2 off = vec2(0.0, texel.y * float(i));
            result += texture(image, TexCoords + off).rgb * weight[i];
            result += texture(image, TexCoords - off).rgb * weight[i];
        }
    }

    FragColor = vec4(result, 1.0);
}
