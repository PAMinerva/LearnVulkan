#version 450

layout (location = 0) in vec3 inNormal;
layout (location = 0) out vec4 outFragColor;

layout(std140, set = 0, binding = 0) uniform buf {
    mat4 View;
    mat4 Projection;
    vec4 lightDirs[2];
    vec4 lightColors[2];
} uBuf;

layout(std140, set = 0, binding = 1) uniform dynbuf {
    mat4 World;
    vec4 solidColor;
} dynBuf;

// Fragment shader applying Lambertian lighting using two directional lights
void main() 
{
    vec4 finalColor = {0.0, 0.0, 0.0, 0.0};
    
    //do N-dot-L lighting for 2 light sources
    for( int i=0; i< 2; i++ )
    {
        finalColor += clamp(dot(uBuf.lightDirs[i].xyz, inNormal) * uBuf.lightColors[i], 0.0, 1.0);
    }
    finalColor.a = 1;

  outFragColor = finalColor;
}