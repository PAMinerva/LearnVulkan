#version 450

layout (quads, fractional_odd_spacing, ccw) in;

layout (location = 0) in vec3 inPos[];

layout(std140, set = 0, binding = 0) uniform buf {
    mat4 viewMatrix;
    mat4 projMatrix;
} uBuf;

layout(std140, set = 0, binding = 1) uniform dynbuf {
    mat4 worldMatrix;
    vec4 solidColor;
} dynBuf;

// gl_Position and other built-in variables are provided through the following built-in structures,
// which means you don't need to define the following structures to access the built-in variables available to the TES.
// Observe that gl_in is an array but the output block is not, as the TES generates primitives vertex by vertex.
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
// };


void BernsteinBasis(in float t, out vec4 basis)
{
    float invT = 1.0f - t;

    basis = vec4(invT * invT * invT, 3.0 * t * invT * invT, 3.0 * t * t * invT, t * t * t);
}

void main()
{
    vec4 basisU, basisV;
    BernsteinBasis(gl_TessCoord.x, basisU);
	BernsteinBasis(gl_TessCoord.y, basisV);

    vec3 pos = basisU.x * (basisV.x * inPos[0]  + basisV.y * inPos[1]  + basisV.z * inPos[2]  + basisV.w * inPos[3] );
    pos += basisU.y * (basisV.x * inPos[4] + basisV.y * inPos[5] + basisV.z * inPos[6] + basisV.w * inPos[7] );
    pos += basisU.z * (basisV.x * inPos[8] + basisV.y * inPos[9] + basisV.z * inPos[10] + basisV.w * inPos[11]);
    pos += basisU.w * (basisV.x * inPos[12] + basisV.y * inPos[13] + basisV.z * inPos[14] + basisV.w * inPos[15]);
	
    vec4 worldPos = dynBuf.worldMatrix * vec4(pos, 1.0);     // Local to World
    vec4 viewPos = uBuf.viewMatrix * worldPos;               // World to View
    gl_Position = uBuf.projMatrix * viewPos;                 // View to Clip
}