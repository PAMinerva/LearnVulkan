#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec4 inTexCoord;

layout(std140, set = 0, binding = 0) uniform buf {
    vec4 displacement;
} uBuf;

layout (location = 0) out vec4 outTexCoord;

void main() 
{
    gl_Position = vec4(inPos, 1.0) + uBuf.displacement;    // Shift vertex position
    gl_Position.y = -gl_Position.y;	                       // Flip y-coords.
    outTexCoord = inTexCoord;                              // Pass texel coordinates to the next stage
}