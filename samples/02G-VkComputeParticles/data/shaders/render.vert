#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec2 inSize;
layout (location = 2) in float inSpeed;

layout (location = 0) out outVS
{
    vec3 outPos;
    vec2 outSize;
    float outSpeed;
};


void main()
{
    gl_PointSize = 1.0f;

    outPos = inPos;
    outSize = inSize;
    outSpeed = inSpeed;
}