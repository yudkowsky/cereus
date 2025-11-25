@echo off

set "VULKAN_SDK=C:\VulkanSDK\1.4.321.1"
set "VK_SDK_PATH=%VULKAN_SDK%"

pushd ..\data\shaders\src
glslc tri.vert -o ..\spirv\tri.vert.spv
glslc tri.frag -o ..\spirv\tri.frag.spv 
popd

IF NOT EXIST ..\..\build_cereus mkdir ..\..\build_cereus
pushd ..\..\build_cereus
xcopy /E /I /Y ..\cereus\data .\data >nul
cl -nologo -Z7 /W4 /D_CRT_SECURE_NO_WARNINGS /wd4090 ..\cereus\code\win32_cereus.c ..\cereus\code\renderer_cereus.c ..\cereus\code\cereus.c ^
    /I "%VULKAN_SDK%\Include" ^
    /link /OUT:cereus.exe /LIBPATH:"%VULKAN_SDK%\Lib" vulkan-1.lib user32.lib /DEBUG /PDB:cereus.pdb

popd

