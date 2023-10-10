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

/../../bin/glslangValidator -V -g ./data/shaders/render.vert -o ./data/shaders/render.vert.spv
/../../bin/glslangValidator -V -g ./data/shaders/render.tesc -o ./data/shaders/render.tesc.spv
/../../bin/glslangValidator -V -g ./data/shaders/render.tese -o ./data/shaders/render.tese.spv
/../../bin/glslangValidator -V -g ./data/shaders/render.frag -o ./data/shaders/render.frag.spv

echo Building project...

g++ -g src/*.cpp -o 02E-VkTessellation.out $includes $defines $links

rm -f *.o