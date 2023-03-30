#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec4 inColor;

layout (location = 0) out vec4 outColor;

out gl_PerVertex 
{
    vec4 gl_Position;   
};


void main() 
{
	outColor = inColor;
	gl_Position = vec4(inPos, 1.0);
	gl_Position.y = -gl_Position.y;	// Flip y-coords.
}
