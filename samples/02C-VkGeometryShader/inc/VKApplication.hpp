#pragma once

/** @brief Example settings that can be changed e.g. by command line arguments */
struct Settings {
    /** @brief Activates validation layers (and message output) when set to true */
    bool validation = false;
    /** @brief Set to true if fullscreen mode has been requested via command line */
    bool fullscreen = false;
    /** @brief Set to true if v-sync will be forced for the swapchain */
    bool vsync = false;
    };

struct WindowParameters {
#if defined(_WIN32)
    HWND hWindow;
    HINSTANCE hInstance;
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
    bool quit = false;
    Display *DisplayPtr;
    Window Handle;
    Atom xlib_wm_delete_window = 0;
#endif
};

class VKSample;

class VKApplication
{
public:
    static void Setup(VKSample* pSample, bool enableValidation, void* hInstance = nullptr, int nCmdShow = 0);
    static int RenderLoop();
    static std::vector<const char*>* GetArgs() { return &m_args; }
    static VKSample* GetVKSample() { return m_pVKSample; }
    static Settings settings;
    static WindowParameters winParams;
    static bool resizing;

private:
    static VKSample* m_pVKSample;
    static std::vector<const char*> m_args;
};