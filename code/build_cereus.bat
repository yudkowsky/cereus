@echo off

set "VULKAN_SDK=C:\VulkanSDK\1.4.321.1"
set "VK_SDK_PATH=%VULKAN_SDK%"

pushd ..\data\shaders\src
glslc tri.vert -g -o ..\spirv\tri.vert.spv
glslc tri.frag -g -o ..\spirv\tri.frag.spv 
glslc outline-select.vert -g -o ..\spirv\outline-select.vert.spv
glslc outline-select.frag -g -o ..\spirv\outline-select.frag.spv
glslc laser-fill.vert -g -o ..\spirv\laser-fill.vert.spv
glslc laser-fill.frag -g -o ..\spirv\laser-fill.frag.spv 
glslc laser-outline.vert -g -o ..\spirv\laser-outline.vert.spv
glslc laser-outline.frag -g -o ..\spirv\laser-outline.frag.spv
glslc sprite.vert -g -o ..\spirv\sprite.vert.spv
glslc sprite.frag -g -o ..\spirv\sprite.frag.spv
glslc model.vert -g -o ..\spirv\model.vert.spv
glslc model.frag -g -o ..\spirv\model.frag.spv
glslc model-blackline.vert -g -o ..\spirv\model-blackline.vert.spv
glslc model-blackline.frag -g -o ..\spirv\model-blackline.frag.spv
glslc outline-post.vert -g -o ..\spirv\outline-post.vert.spv
glslc outline-post.frag -g -o ..\spirv\outline-post.frag.spv
glslc water-distortion.vert -g -o ..\spirv\water-distortion.vert.spv
glslc water-distortion.frag -g -o ..\spirv\water-distortion.frag.spv
glslc water-raytrace.comp -g -o ..\spirv\water-raytrace.comp.spv
popd

IF EXIST ..\..\build_cereus rmdir \S \Q ..\..\build_cereus 2>nul
IF NOT EXIST ..\..\build_cereus mkdir ..\..\build_cereus
pushd ..\..\build_cereus

xcopy /E /I /Y ..\cereus\data .\data >nul
cl -nologo -Z7 /W4 /wd4295 /D_CRT_SECURE_NO_WARNINGS /wd4090 ..\cereus\code\win32_cereus.c ..\cereus\code\renderer_cereus.c ..\cereus\code\cereus.c ^
    /I "%VULKAN_SDK%\Include" ^
    /link /OUT:cereus.exe /LIBPATH:"%VULKAN_SDK%\Lib" vulkan-1.lib user32.lib /DEBUG /PDB:cereus.pdb

popd
