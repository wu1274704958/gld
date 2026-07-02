#version 330 core
in  vec3 f_color;
out vec4 color;

void main()
{
    // Solid opaque output — no alpha gradient, no tube edges, no moiré.
    color = vec4(f_color, 1.0);
}
