#!/bin/bash

FILE=/usr/include/vulkan/vulkan.h
if [ -f "$FILE" ]; then
    VULKAN_INCLUDE=/usr/include/vulkan/
else 
    VULKAN_INCLUDE=../../external/include/vulkan/
fi

includes="-Iinc -I$VULKAN_INCLUDE"

defines="-DDEBUG -DVK_USE_PLATFORM_XLIB_KHR"

links="-lX11 -lvulkan"

echo Compiling shader...

/../../bin/glslangValidator -V -g ./data/shaders/triangle.vert -o ./data/shaders/triangle.vert.spv
/../../bin/glslangValidator -V -g ./data/shaders/triangle.frag -o ./data/shaders/triangle.frag.spv

echo Building project...

g++ -g src/*.cpp -o 01D-VkHelloUniforms.out $includes $defines $links

rm -f *.o