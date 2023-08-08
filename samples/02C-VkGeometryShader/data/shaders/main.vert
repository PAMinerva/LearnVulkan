#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;

layout(std140, set = 0, binding = 0) uniform buf {
    mat4 View;
    mat4 Projection;
    vec4 lightDir;
    vec4 lightColor;
} uBuf;

layout(std140, set = 0, binding = 1) uniform dynbuf {
    mat4 World;
    vec4 solidColor;
} dynBuf;

layout (location = 0) out vec3 outNormal;

void main() 
{
    outNormal = mat3(dynBuf.World) * inNormal;           // Transforms the normal vector and pass it to the next stage
    vec4 worldPos = dynBuf.World * vec4(inPos, 1.0);     // Local to World
    vec4 viewPos = uBuf.View * worldPos;                 // World to View
    gl_Position = uBuf.Projection * viewPos;             // View to Clip
}