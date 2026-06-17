@echo off

set "VK_SDK_PATH=%VULKAN_SDK%"

pushd ..\data\shaders\src
glslc cube.vert -g -o ..\spirv\cube.vert.spv
glslc cube.frag -g -o ..\spirv\cube.frag.spv 
glslc outline-select.vert -g -o ..\spirv\outline-select.vert.spv
glslc outline-select.frag -g -o ..\spirv\outline-select.frag.spv
glslc laser.vert -g -o ..\spirv\laser.vert.spv
glslc laser.frag -g -o ..\spirv\laser.frag.spv 
glslc oit-resolve.vert -g -o ..\spirv\oit-resolve.vert.spv
glslc oit-resolve.frag -g -o ..\spirv\oit-resolve.frag.spv
glslc sprite.vert -g -o ..\spirv\sprite.vert.spv
glslc sprite.frag -g -o ..\spirv\sprite.frag.spv
glslc model.vert -g -o ..\spirv\model.vert.spv
glslc model.frag -g -o ..\spirv\model.frag.spv
glslc outline-post.vert -g -o ..\spirv\outline-post.vert.spv
glslc outline-post.frag -g -o ..\spirv\outline-post.frag.spv
glslc water.vert -g -o ..\spirv\water.vert.spv
glslc water.frag -g -o ..\spirv\water.frag.spv
glslc shadow-cube.vert -g -o ..\spirv\shadow-cube.vert.spv
glslc shadow-model.vert -g -o ..\spirv\shadow-model.vert.spv
glslc fft-spectrum.comp -g -o ..\spirv\fft-spectrum.comp.spv
glslc fft-evolved.comp -g -o ..\spirv\fft-evolved.comp.spv
glslc fft-pass.comp -g -o ..\spirv\fft-pass.comp.spv
glslc fft-finalize.comp -g -o ..\spirv\fft-finalize.comp.spv
popd

IF EXIST ..\..\build_cereus rmdir \S \Q ..\..\build_cereus 2>nul
IF NOT EXIST ..\..\build_cereus mkdir ..\..\build_cereus
pushd ..\..\build_cereus

xcopy /E /I /Y ..\cereus\data .\data >nul
cl -nologo -Z7 /FC /W4 /wd4295 /D_CRT_SECURE_NO_WARNINGS /wd4090 ..\cereus\code\win32_cereus.c ..\cereus\code\renderer_cereus.c ..\cereus\code\cereus.c ^
    /I "%VULKAN_SDK%\Include" ^
    /link /OUT:cereus.exe /LIBPATH:"%VULKAN_SDK%\Lib" vulkan-1.lib user32.lib /DEBUG /PDB:cereus.pdb

popd
