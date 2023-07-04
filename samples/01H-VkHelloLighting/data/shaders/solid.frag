#version 450

layout (location = 0) in vec3 inNormal;
layout (location = 0) out vec4 outFragColor;

layout(std140, set = 0, binding = 0) uniform buf {
    mat4 View;
    mat4 Projection;
    vec4 lightDirs[2];
    vec4 lightColors[2];
} uBuf;

layout(std140, set = 0, binding = 1) uniform dynbuf {
    mat4 World;
    vec4 solidColor;
} dynBuf;

// Fragment shader applying solid color
void main() 
{
  outFragColor = dynBuf.solidColor;
}