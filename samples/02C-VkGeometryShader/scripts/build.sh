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
/../../bin/glslangValidator -V -g ./data/shaders/passthrough.vert -o ./data/shaders/passthrough.vert.spv
/../../bin/glslangValidator -V -g ./data/shaders/main.geom -o ./data/shaders/main.geom.spv
/../../bin/glslangValidator -V -g ./data/shaders/solid.frag -o ./data/shaders/solid.frag.spv
/../../bin/glslangValidator -V -g ./data/shaders/lambertian.frag -o ./data/shaders/lambertian.frag.spv

echo Building project...

g++ -g src/*.cpp -o 02C-VkGeometryShader.out $includes $defines $links

rm -f *.o