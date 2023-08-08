#version 450

layout (location = 0) in vec3 inPos;
layout (location = 0) out vec4 outPos;

void main()
{
    outPos = vec4(inPos, 1.0);
}