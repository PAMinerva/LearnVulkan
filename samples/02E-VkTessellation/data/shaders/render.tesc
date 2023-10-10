#version 450

layout (vertices = 16) out;

layout (location = 0) in vec3 inPos[];

layout (location = 0) out vec3 outPos[16];

// gl_Position and other built-in variables are provided through the following built-in structures,
// which means you don't need to define the following structures to use gl_in and gl_out as arrays 
// to access the built-in variables available to the TCS.
//
// in gl_PerVertex
// {
//   vec4 gl_Position;
//   float gl_PointSize;
//   float gl_ClipDistance[];
// } gl_in[];
//
// out gl_PerVertex
// {
//   vec4 gl_Position;
//   float gl_PointSize;
//   float gl_ClipDistance[];
// } gl_out[];


void main()
{
    if (gl_InvocationID == 0)
    {
        gl_TessLevelInner[0] = 25.0;
        gl_TessLevelInner[1] = 25.0;

        gl_TessLevelOuter[0] = 25.0;
        gl_TessLevelOuter[1] = 25.0;
        gl_TessLevelOuter[2] = 25.0;
        gl_TessLevelOuter[3] = 25.0;
    }

    outPos[gl_InvocationID] = inPos[gl_InvocationID];
}