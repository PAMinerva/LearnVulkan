#pragma once

#include "glm/glm.hpp"

// Return a matrix that reflect points or directions with respect to a plane
inline glm::mat4 MatrixReflect(glm::vec4 plane)
{
    glm::mat4 refl;
    refl[0] = { 1-2*plane.x*plane.x, -2*plane.x*plane.y, -2*plane.x*plane.z, 0 };
    refl[1] = { -2*plane.y*plane.x, 1-2*plane.y*plane.y, -2*plane.y*plane.z, 0 };
    refl[2] = { -2*plane.z*plane.x, -2*plane.z*plane.y, 1-2*plane.z*plane.z, 0 };
    refl[3] = { -2*plane.x*plane.w, -2*plane.y*plane.w, -2*plane.z*plane.w, 1 };

    return refl;
}

// Return a matrix that project points onto a plane along a specific direction
inline glm::mat4 MatrixShadow(glm::vec4 plane, glm::vec4 light)
{
    float dot = glm::dot(plane, light);
    glm::mat4 shadow;
    shadow[0] = { dot+plane.w*light.w-plane.x*light.x, -plane.x*light.y, -plane.x*light.z, -plane.x*light.w };
    shadow[1] = { -plane.y*light.x, dot+plane.w*light.w-plane.y*light.y, -plane.y*light.z, -plane.y*light.w };
    shadow[2] = { -plane.z*light.x, -plane.z*light.y, dot+plane.w*light.w-plane.z*light.z, -plane.z*light.w };
    shadow[3] = { -plane.w*light.x, -plane.w*light.y, -plane.w*light.z, dot };

    return shadow;
}