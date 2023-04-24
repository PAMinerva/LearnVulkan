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
#cd ../../bin
#glslangValidator -V ../samples/01D-VkHelloUniforms/data/shaders/triangle.vert -o ../samples/01D-VkHelloUniforms/data/shaders/triangle.vert.spv
#glslangValidator -V ../samples/01D-VkHelloUniforms/data/shaders/triangle.frag -o ../samples/01D-VkHelloUniforms/data/shaders/triangle.frag.spv
..\..\bin\glslangValidator -V .\data\shaders\triangle.vert -o .\data\shaders\triangle.vert.spv
..\..\bin\glslangValidator -V .\data\shaders\triangle.frag -o .\data\shaders\triangle.frag.spv

#cd ../samples/01D-VkHelloUniforms
echo Building project...

g++ -g src/*.cpp -o 01D-VkHelloUniforms.out $includes $defines $links

rm -f *.o