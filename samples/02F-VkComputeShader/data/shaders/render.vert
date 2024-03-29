#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec2 inTextCoord;

layout (location = 0) out vec2 outTextCoord;

layout(std140, set = 0, binding = 0) uniform buf {
    mat4 View;
    mat4 Projection;
} uBuf;

layout(std140, set = 0, binding = 1) uniform dynbuf {
    mat4 World;
} dynBuf;


void main() 
{
    outTextCoord = inTextCoord;                          // Pass texture coordinates to the next stage
    vec4 worldPos = dynBuf.World * vec4(inPos, 1.0);     // Local to World
    vec4 viewPos = uBuf.View * worldPos;                 // World to View
    gl_Position = uBuf.Projection * viewPos;             // View to Clip
}