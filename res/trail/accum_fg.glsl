#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D curr;   // this frame's trail-object render
uniform sampler2D prev;   // previous accumulation
uniform float decay;      // tail fade factor [0,1)

void main()
{
    vec3 c = texture(curr, TexCoords).rgb;
    vec3 p = texture(prev, TexCoords).rgb * decay;
    // Phosphor/echo accumulation: bright head stays stable, past positions fade.
    FragColor = vec4(max(c, p), 1.0);
}
