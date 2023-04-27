#include "stdafx.h"
#include "VKApplication.hpp"
#include "VKHelloUniforms.hpp"

#if defined (_WIN32)
_Use_decl_annotations_
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, char*, int nCmdShow)
{
    for (size_t i = 0; i < __argc; i++)
    {
        VKApplication::GetArgs()->push_back(__argv[i]);
    };

    VKHelloUniforms sample(1280, 720, "VK Hello Uniforms");
    VKApplication::Setup(&sample, true, hInstance, nCmdShow);
    return VKApplication::RenderLoop();
}
#elif defined (VK_USE_PLATFORM_XLIB_KHR)
int main(const int argc, const char* argv[])
{
    for (int i = 0; i < argc; i++)
    {
        VKApplication::GetArgs()->push_back(argv[i]);
    };

    VKHelloUniforms sample(1280, 720, "VK Hello Uniforms");
    VKApplication::Setup(&sample, true);
    return VKApplication::RenderLoop();
}
#endif