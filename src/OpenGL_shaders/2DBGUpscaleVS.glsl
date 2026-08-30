#version 140

in vec2 vPosition;

void main()
{
    gl_Position = vec4((vPosition * 2) - 1, 0, 1);
}
