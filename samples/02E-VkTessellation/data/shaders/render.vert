#version 450

layout (location = 0) in vec3 inPos;

layout (location = 0) out vec3 outPos;


void main()
{
    outPos = inPos;
}