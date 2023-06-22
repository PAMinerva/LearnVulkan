#!/bin/bash

FILE=/usr/include/vulkan/vulkan.h
if [ -f "$FILE" ]; then
    VULKAN_INCLUDE=/usr/include/vulkan/
else 
    VULKAN_INCLUDE=../../external/include/vulkan/
fi

includes="-Iinc -I../../external/include/ -I$VULKAN_INCLUDE"

defines="-DDEBUG -DVK_USE_PLATFORM_XLIB_KHR"

links="-lX11 -lvulkan"

echo Compiling shader...

/../../bin/glslangValidator -V -g ./data/shaders/main.vert -o ./data/shaders/main.vert.spv
/../../bin/glslangValidator -V -g ./data/shaders/main.frag -o ./data/shaders/main.frag.spv

echo Building project...

g++ -g src/*.cpp -o 01G-VkHelloTransformations.out $includes $defines $links

rm -f *.o