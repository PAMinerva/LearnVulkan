#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec2 inSize;
layout (location = 2) in float inSpeed;

// all xfb_ layout qualifiers except xfb_offset can be omitted in this case 
// (xfb_buffer = 0 global default, xfb_stride can be inferred)
layout(location = 0, xfb_buffer = 0, xfb_offset = 0, xfb_stride = 24) out outVS
{
    vec3 outPos;    // location 0, TF buffer 0, offset = 0
    vec2 outSize;   // location 1, TF buffer 0, offset = 12
    float outSpeed; // location 2, TF buffer 0, offset = 20
}; // If no instance name is defined, the variables in the block are scoped at the global level

// Equivalent to:
//layout (location = 0, xfb_buffer = 0, xfb_offset = 0) out vec3 outPos;
//layout (location = 1, xfb_buffer = 0, xfb_offset = 12) out vec2 outSize;
//layout (location = 2, xfb_buffer = 0, xfb_offset = 20) out float outSpeed;

layout(std140, set = 0, binding = 0) uniform buf {
    mat4 viewMatrix;
    mat4 projMatrix;
    vec3 cameraPos;
    float deltaTime;
} uBuf;

layout(std140, set = 0, binding = 1) uniform dynbuf {
    mat4 worldMatrix;
    vec4 solidColor;
} dynBuf;

void main()
{
    gl_PointSize = 1.0f;
    
    // Decrease the height of the point\particle over time based on its speed
    outPos = inPos;
    outPos.z -= (inSpeed * uBuf.deltaTime);
    
    // Reset the height of the point\particle
    if (outPos.z < -50.0f)
    {
    	outPos.z = 50.0f;
    }

    outSize = inSize;
    outSpeed = inSpeed;
}