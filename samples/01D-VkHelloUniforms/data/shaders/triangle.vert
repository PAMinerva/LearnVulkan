#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec4 inColor;

layout(set = 0, binding = 0) uniform buf {
        vec4 displacement;
} uBuf;

layout (location = 0) out vec4 outColor;

void main() 
{
	outColor = inColor;                                  // Pass color to the next stage
	gl_Position = vec4(inPos, 1.0) + uBuf.displacement;  // Shift vertex position
	gl_Position.y = -gl_Position.y;	                     // Flip y-coords.
}