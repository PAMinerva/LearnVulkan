@echo off

if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
) else (
    echo WARNING: You need to install the MSVC toolset, or update the path of vcvars64.bat in build.dat
    exit
)

if exist "%VULKAN_SDK%\Include\vulkan\vulkan.h" (
    SET VULKAN_INCLUDE=%VULKAN_SDK%\Include\vulkan
) else (
    SET VULKAN_INCLUDE=..\..\external\include\vulkan
)

SET includes=/I inc /I ..\..\external\include ^
/I %VULKAN_INCLUDE%

SET defines=/D DEBUG /D _WIN32 /D VK_USE_PLATFORM_WIN32_KHR /D _CRT_SECURE_NO_WARNINGS

if exist "%VULKAN_SDK%\Lib\vulkan-1.lib" (
    SET VULKAN_LIB=%VULKAN_SDK%\Lib
) else (
    SET VULKAN_LIB=..\..\external\lib\vulkan
)

SET links=/link kernel32.lib user32.lib shell32.lib gdi32.lib vulkan-1.lib ^
/LIBPATH:%VULKAN_LIB% /SUBSYSTEM:WINDOWS /OUT:02A-VkAlphaBlending.exe /PDB:02A-VkAlphaBlending.pdb

echo Compiling shader...

..\..\bin\glslangValidator -V -g .\data\shaders\main.vert -o .\data\shaders\main.vert.spv
..\..\bin\glslangValidator -V -g .\data\shaders\solid.frag -o .\data\shaders\solid.frag.spv
..\..\bin\glslangValidator -V -g .\data\shaders\interpolated.frag -o .\data\shaders\interpolated.frag.spv

echo Building project...

cl src/*.cpp /MDd /EHsc /JMC /ZI %includes% %defines% %links%

del *.obj vc*.idb vc*.pdb