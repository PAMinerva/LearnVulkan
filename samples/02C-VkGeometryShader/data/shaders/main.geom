#version 450

layout(std140, set = 0, binding = 0) uniform buf {
    mat4 View;
    mat4 Projection;
    vec4 lightDir;
    vec4 lightColor;
} uBuf;

layout(std140, set = 0, binding = 1) uniform dynbuf {
    mat4 World;
    vec4 solidColor;
} dynBuf;

layout(triangles) in;
layout(line_strip, max_vertices = 2) out;

layout (location = 0) in vec4 inPos[];

// gl_Position and other built-in variables are provided through the following built-in structures,
// which means you don't need to define the following structures to access the built-in variables available to the TSC.
// Observe that gl_in is an array but the output block is not, as the geometry shader emits primitives vertex by vertex.
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


void main()
{
    // Get the vertex local positions of the input triangle
    vec4 v0 = inPos[0];
    vec4 v1 = inPos[1];
    vec4 v2 = inPos[2];

    // Calculate the sides of the input triangle
    vec3 e1 = normalize(v1 - v0).xyz;
    vec3 e2 = normalize(v2 - v0).xyz;

    // The normal is the cross product of the sides.
    // v0, v1 and v2 in counterclockwise order so it's e1 x e2
    vec3 normal = normalize(cross(e1, e2));

    // Calculate the center of the input triangle as the arithmetic 
    // mean of the three vertex positions.
    // (the normal will be displayed at this point)
    vec4 center = (v0 + v1 + v2) / 3.0f;

    // We need to transform the two points of the line segment representing
    // the normal from local to homogeneous clip space.
    mat4 mVP = ((uBuf.Projection * uBuf.View) * dynBuf.World);

    // Build a line segment representing the normal of the input triangle.
    // The first iteration of the loop will emit a vertex at the center of the input 
    // triangle as the first endpoint of the line segment.
    // The second iteration will emit a vertex at a small offset from the center along 
    // the normal as the second endpoint of the line segment.
    for (int i = 0; i < 2; ++i)
    {
        center.xyz += normal * 0.3f * float(i);
        gl_Position = (mVP * center);
        EmitVertex();
    }
}