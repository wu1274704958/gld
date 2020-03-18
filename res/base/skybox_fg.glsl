#version 330 core
out vec4 FragColor;

in vec3 TexCoords;

uniform samplerCube skybox;

void main()
{    
    vec4 c = texture(skybox, TexCoords);
    FragColor = vec4(1.0);
        //c;
}