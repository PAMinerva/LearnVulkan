#version 450

layout (location = 0) in vec3 inNormal;
layout (location = 0) out vec4 outFragColor;

layout (constant_id = 0) const uint LIGHT_NUM = 2;
layout (constant_id = 1) const float ALPHA = 0.0;

layout(push_constant) uniform push {
    vec4 lightDirs[LIGHT_NUM];
    vec4 lightColors[LIGHT_NUM];
} pushConsts;


// Fragment shader applying Lambertian lighting using 2 directional lights
void main() 
{
    vec4 finalColor = {0.0, 0.0, 0.0, 0.0};
    
    //do N-dot-L lighting for LIGHT_NUM light sources
    for( int i=0; i< LIGHT_NUM; i++ )
    {
        finalColor += clamp(dot(pushConsts.lightDirs[i].xyz, inNormal) * pushConsts.lightColors[i], 0.0, 1.0);
    }
    finalColor.a = ALPHA;

  outFragColor = finalColor;
}