#version 450

layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

layout (location = 0) in inGS
{
    vec3 inPos;
    vec2 inSize;
    float inSpeed;
} inPoints[];

layout(std140, set = 0, binding = 0) uniform bufUniform {
    mat4 viewMatrix;
    mat4 projMatrix;
    vec3 cameraPos;
    float deltaTime;
} uBuf;

layout(std140, set = 0, binding = 1) uniform bufDynamic {
    mat4 worldMatrix;
    vec4 solidColor;
} dynBuf;


void main()
{
    // World coordinates of the input point\particle
    vec3 worldPos = (dynBuf.worldMatrix * vec4(inPoints[0].inPos, 1.0f)).xyz;

    // We need the up direction of the world space, and right direction with respect to the quad.
    // We can use the projection of the front vector onto the xy-plane to calculate the right direction.
    vec3 up = vec3(0.0f, 0.0f, 1.0f);
    vec3 front = uBuf.cameraPos - worldPos;
    front.z = 0.0f; // front.y = 0.0f; for the interstellar travel effect
    front = normalize(front);
    vec3 right = cross(up, front);

    // Half-size of the input point\particle
    float hw = 0.5f * inPoints[0].inSize.x;
    float hh = 0.5f * inPoints[0].inSize.y;

    // Compute the world coordinates of the four corners of the quad from the point\particle position.
    // The vertices of the two triangles composing the quad are included in the array in a counter-clockwise order, 
    // according to the triangle strip order.
    vec4 quadVertices[4] = 
    {
        vec4(worldPos + (hw * right) - (hh * up), 1.0f), 
        vec4(worldPos + (hw * right) + (hh * up), 1.0f), 
        vec4(worldPos - (hw * right) - (hh * up), 1.0f), 
        vec4(worldPos - (hw * right) + (hh * up), 1.0f)
    };

    // Transform the four vertices of the quad from world to clip space, and
    // emit them as a triangle strip.
    for (int i = 0; i < 4; ++i)
    {
        gl_Position = (uBuf.viewMatrix * quadVertices[i]);
        gl_Position = (uBuf.projMatrix * gl_Position);
        EmitVertex();
    }
}