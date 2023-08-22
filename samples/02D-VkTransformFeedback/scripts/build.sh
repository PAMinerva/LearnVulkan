#!/bin/bash

FILE=/usr/include/vulkan/vulkan.h
if [ -f "$FILE" ]; then
    VULKAN_INCLUDE=/usr/include/vulkan/
else 
    VULKAN_INCLUDE=../../external/include/vulkan/
fi

includes="-Iinc -I./external/include/ -I$VULKAN_INCLUDE"
#-I../../external/include/
defines="-DDEBUG -DVK_USE_PLATFORM_XLIB_KHR"

links="-lX11 -lvulkan"

echo Compiling shader...

./bin/glslangValidator -V -g ./data/shaders/transformFeedback.vert -o ./data/shaders/transformFeedback.vert.spv
./bin/glslangValidator -V -g ./data/shaders/render.vert -o ./data/shaders/render.vert.spv
./bin/glslangValidator -V -g ./data/shaders/render.geom -o ./data/shaders/render.geom.spv
./bin/glslangValidator -V -g ./data/shaders/render.frag -o ./data/shaders/render.frag.spv

#/../../bin/glslangValidator -V -g ./data/shaders/transformFeedback.vert -o ./data/shaders/transformFeedback.vert.spv
#/../../bin/glslangValidator -V -g ./data/shaders/render.vert -o ./data/shaders/render.vert.spv
#/../../bin/glslangValidator -V -g ./data/shaders/render.geom -o ./data/shaders/render.geom.spv
#/../../bin/glslangValidator -V -g ./data/shaders/render.frag -o ./data/shaders/render.frag.spv

echo Building project...

g++ -g src/*.cpp -o 02D-VkTransformFeedback.out $includes $defines $links

rm -f *.o