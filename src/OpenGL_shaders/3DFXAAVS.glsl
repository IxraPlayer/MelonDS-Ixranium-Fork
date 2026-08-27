#version 140

in vec2 vPosition;
out vec2 fTexcoord;

void main()
{
    fTexcoord = vPosition * 0.5 + 0.5;
    gl_Position = vec4(vPosition, 0.0, 1.0);
}
