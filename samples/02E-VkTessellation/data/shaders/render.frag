#version 450

layout (location = 0) out vec4 outFragColor;


layout(std140, set = 0, binding = 1) uniform dynbuf {
    mat4 World;
    vec4 solidColor;
} dynBuf;

// Fragment shader applying solid color
void main() 
{
  outFragColor = dynBuf.solidColor;
}