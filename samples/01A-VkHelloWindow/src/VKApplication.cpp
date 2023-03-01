#include "stdafx.h"
#include "VKApplication.hpp"
#include "VKSample.hpp"

VKSample* VKApplication::m_pVKSample = nullptr;
std::vector<const char*> VKApplication::m_args;
WindowParameters VKApplication::winParams{};
Settings VKApplication::settings{};
bool VKApplication::resizing = false;


#if defined(_WIN32)
// Win32 : Sets up a console window and redirects standard output to it
void setupConsole(std::string title)
{
    AllocConsole();
    AttachConsole(GetCurrentProcessId());
    FILE* stream;
    freopen_s(&stream, "CONIN$", "r", stdin);
    freopen_s(&stream, "CONOUT$", "w+", stdout);
    freopen_s(&stream, "CONOUT$", "w+", stderr);
    SetConsoleTitle(title.c_str());
}

// Main message handler for the sample.
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    VKSample* pSample = reinterpret_cast<VKSample*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    //VKSample* pSample = GetVKSample();

    switch (message)
    {
        case WM_CREATE:
        {
            // Save the VKSample* passed in to CreateWindow.
            LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
        }
        return 0;

        case WM_PAINT:
            if (pSample && pSample->IsInitialized() && !IsIconic(VKApplication::winParams.hWindow))
            {
                pSample->OnUpdate();
                pSample->OnRender();

                if (pSample->GetFrameCounter() % 100 == 0)
                    SetWindowText(VKApplication::winParams.hWindow, pSample->GetWindowTitle().c_str());
            }
            return 0;

        case WM_KEYDOWN:
            switch (wParam) 
            {
                case VK_ESCAPE:
                    PostQuitMessage(0);
                    break;
                /* case VK_LEFT:
                    demo.spin_angle -= demo.spin_increment;
                    break;
                case VK_RIGHT:
                    demo.spin_angle += demo.spin_increment;
                    break;
                case VK_SPACE:
                    demo.pause = !demo.pause;
                    break; */
            }
            if (pSample)
            {
                pSample->OnKeyDown(static_cast<UINT8>(wParam));
            }
            return 0;

        case WM_KEYUP:
            if (pSample)
            {
                pSample->OnKeyUp(static_cast<UINT8>(wParam));
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_SIZE:
            if ((pSample->IsInitialized()) && (wParam != SIZE_MINIMIZED))
            {
                if ((VKApplication::resizing) || ((wParam == SIZE_MAXIMIZED) || (wParam == SIZE_RESTORED)))
                {
                    pSample->WindowResize(LOWORD(lParam), HIWORD(lParam));
                }
            }
            return 0;;

        case WM_GETMINMAXINFO:
        {
            LPMINMAXINFO minMaxInfo = (LPMINMAXINFO)lParam;
            minMaxInfo->ptMinTrackSize.x = 640;
            minMaxInfo->ptMinTrackSize.y = 360;
        }
        return 0;

        case WM_ENTERSIZEMOVE:
            VKApplication::resizing = true;			
        return 0;;

        case WM_EXITSIZEMOVE:
            VKApplication::resizing = false;
        return 0;
    }

    // Handle any messages the switch statement didn't.
    return DefWindowProc(hWnd, message, wParam, lParam);
}
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
void HandleX11Event(XEvent& event)
{
        VKSample* pSample = VKApplication::GetVKSample();

    switch (event.type)
    {
        case ClientMessage:
            if ((Atom)event.xclient.data.l[0] == VKApplication::winParams.xlib_wm_delete_window) 
            {
                VKApplication::winParams.quit = true;
            }
            break;

        case KeyPress:
        {
            switch (event.xkey.keycode)
            {
                case 0x9:  // Escape
                    VKApplication::winParams.quit = true;
                    break;
                /* case 0x71:  // left arrow key
                    spin_angle -= spin_increment;
                    break;
                case 0x72:  // right arrow key
                    spin_angle += spin_increment;
                    break;
                case 0x41:  // space bar
                    pause = !pause;
                    break; */
            }
            if (pSample)
            {
                pSample->OnKeyDown(static_cast<uint8_t>(event.xkey.keycode));
            }
        }
        break;

        case KeyRelease:
        {
            if (pSample)
            {
                pSample->OnKeyDown(static_cast<uint8_t>(event.xkey.keycode));
            }
        }
        break;

        case DestroyNotify:
            VKApplication::winParams.quit = true;
            break;

        case ConfigureNotify:
        {
            if ((pSample->IsInitialized()) && ((event.xconfigure.width != pSample->GetWidth()) || (event.xconfigure.height != pSample->GetHeight())))
            {
                if (pSample && (event.xconfigure.width > 0) && (event.xconfigure.height > 0))
                {
                    pSample->WindowResize(event.xconfigure.width, event.xconfigure.height);
                }
            }
        }
        break;
        
        default:
            break;
    }
}
#endif

void VKApplication::Setup(VKSample* pSample, bool enableValidation, void* hInstance, int nCmdShow)
{
    m_pVKSample = pSample;
    settings.validation = enableValidation;

    char assetsPath[512] = {};
    strncpy(assetsPath, GetArgs()->data()[0], (size_t)511);
#if defined(_WIN32)
    char* lastSlash = strrchr(assetsPath, '\\');
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
    char* lastSlash = strrchr(assetsPath, '/');
#endif

    if (lastSlash)
    {
        *(lastSlash + 1) = L'\0';
    }
    m_pVKSample->SetAssetsPath(assetsPath);

#if defined(_WIN32)
    if (settings.validation)
        setupConsole("Vulkan Sample");

    winParams.hInstance = (HINSTANCE)hInstance;

    // Initialize the window class.
    WNDCLASSEX windowClass = { 0 };
    windowClass.cbSize = sizeof(WNDCLASSEX);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = (HINSTANCE)hInstance;
    windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    windowClass.lpszClassName = "VKSampleClass";
    RegisterClassEx(&windowClass);

    RECT windowRect = { 0, 0, static_cast<LONG>(pSample->GetWidth()), static_cast<LONG>(pSample->GetHeight()) };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    // Create the window and store a handle to it.
    winParams.hWindow = CreateWindow(
        windowClass.lpszClassName,
        pSample->GetTitle(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,        // We have no parent window.
        nullptr,        // We aren't using menus.
        (HINSTANCE)hInstance,
        pSample);

    if (settings.fullscreen)
    {
        // Make the window borderless so that the client area can fill the screen.
        SetWindowLong(winParams.hWindow, GWL_STYLE, WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU | WS_THICKFRAME));

        RECT fullscreenWindowRect;

        // Get the settings of the primary display
        DEVMODE devMode = {};
        devMode.dmSize = sizeof(DEVMODE);
        EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &devMode);

        fullscreenWindowRect = {
            devMode.dmPosition.x,
            devMode.dmPosition.y,
            devMode.dmPosition.x + static_cast<LONG>(devMode.dmPelsWidth),
            devMode.dmPosition.y + static_cast<LONG>(devMode.dmPelsHeight)
        };

        SetWindowPos(
            winParams.hWindow,
            HWND_TOPMOST,
            fullscreenWindowRect.left,
            fullscreenWindowRect.top,
            fullscreenWindowRect.right,
            fullscreenWindowRect.bottom,
            SWP_FRAMECHANGED | SWP_NOACTIVATE);

        m_pVKSample->SetWidth(devMode.dmPelsWidth);
        m_pVKSample->SetHeight(devMode.dmPelsHeight);
    }

    // Show the window
    ShowWindow(winParams.hWindow, nCmdShow);

    // Initialize the sample. OnInit is defined in each child-implementation of VKSample.
    pSample->OnInit();

#elif defined(VK_USE_PLATFORM_XLIB_KHR)
    const char *display_envar = getenv("DISPLAY");
    if (display_envar == nullptr || display_envar[0] == '\0') {
        printf("Environment variable DISPLAY requires a valid value.\nExiting ...\n");
        fflush(stdout);
        exit(1);
    }

    winParams.DisplayPtr = XOpenDisplay(nullptr);

    unsigned long black = BlackPixel(winParams.DisplayPtr, DefaultScreen(winParams.DisplayPtr));
    winParams.Handle = XCreateSimpleWindow(winParams.DisplayPtr, DefaultRootWindow(winParams.DisplayPtr), 0, 0, pSample->GetWidth(), pSample->GetHeight(), 0, black, black);

    XSelectInput(winParams.DisplayPtr, winParams.Handle, KeyPressMask | KeyReleaseMask | StructureNotifyMask | ExposureMask);

    Atom wm_protocols   = XInternAtom(winParams.DisplayPtr, "WM_PROTOCOLS", true);
    winParams.xlib_wm_delete_window = XInternAtom(winParams.DisplayPtr, "WM_DELETE_WINDOW", true);

    XSetWMProtocols(winParams.DisplayPtr, winParams.Handle, &winParams.xlib_wm_delete_window, 1);

    XSetStandardProperties(winParams.DisplayPtr, 
                        winParams.Handle, 
                        m_pVKSample->GetTitle(), m_pVKSample->GetTitle(), 
                        None, nullptr, 0, nullptr);

    // Setup the Size Hints with the minimum window size.
    XSizeHints sizehints;
    sizehints.flags = PMinSize;
    sizehints.min_width = 640;
    sizehints.min_height = 360;

    // Tell the Window Manager our hints about the minimum window size.
      XSetWMSizeHints(winParams.DisplayPtr, winParams.Handle,  &sizehints, XA_WM_NORMAL_HINTS);

    if (settings.fullscreen)
    {
        Screen* screen;
        screen = ScreenOfDisplay(winParams.DisplayPtr, DefaultScreen(winParams.DisplayPtr));

        pSample->SetWidth(screen->width);
        pSample->SetHeight(screen->height);

        Atom wm_state   = XInternAtom(winParams.DisplayPtr, "_NET_WM_STATE", true);
        Atom wm_fullscreen = XInternAtom(winParams.DisplayPtr, "_NET_WM_STATE_FULLSCREEN", true);

        XChangeProperty(winParams.DisplayPtr, 
                        winParams.Handle, 
                        wm_state, 
                        XA_ATOM, 32, PropModeReplace, 
                        (unsigned char *)&wm_fullscreen, 1);
    }

    // Initialize the sample. OnInit is defined in each child-implementation of VKSample.
    pSample->OnInit();

    XMapWindow(winParams.DisplayPtr, winParams.Handle);
    XFlush(winParams.DisplayPtr);
#endif
}

int VKApplication::RenderLoop()
{
    // Main sample loop
#if defined(_WIN32)
    MSG msg;
    bool quitMessageReceived = false;
    while (!quitMessageReceived) 
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) 
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                quitMessageReceived = true;
                break;
            }
        }
    }

    m_pVKSample->OnDestroy();

    // Return this part of the WM_QUIT message to Windows.
    return static_cast<char>(msg.wParam);

#elif defined(VK_USE_PLATFORM_XLIB_KHR)
    while (!winParams.quit)
    {
        XEvent event;
        while ((XPending(winParams.DisplayPtr) > 0))
        {
            XNextEvent(winParams.DisplayPtr, &event);
            HandleX11Event(event);
        }

        if (!winParams.quit && m_pVKSample->IsInitialized())
        {
            m_pVKSample->OnUpdate();
            m_pVKSample->OnRender();
        }

        if (m_pVKSample->GetFrameCounter() % 1000 == 0)
        {
            std::string windowTitle = m_pVKSample->GetWindowTitle();

            XSetStandardProperties(winParams.DisplayPtr, 
                        winParams.Handle, 
                        windowTitle.c_str(), windowTitle.c_str(), 
                        None, nullptr, 0, nullptr);
        
            XFlush(winParams.DisplayPtr);
        }
    }

    m_pVKSample->OnDestroy();
    return 0;
#endif
}
