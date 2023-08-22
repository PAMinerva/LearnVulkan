#pragma once

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#endif

#include <iostream>
#include <algorithm>
#include <string>
#include <vector>
#include <map>
#include <array>
#include <string.h>
#include <assert.h>

#include "vulkan.h"