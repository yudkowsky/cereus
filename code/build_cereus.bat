@echo off

set "VK_SDK_PATH=%VULKAN_SDK%"

set "MODE=%1"
if "%MODE%"=="" set "MODE=debug"

if /I "%MODE%"=="release" (
    set "CL_OPT=/O2"
    set "GLSLC_OPT=-O"
) else (
    set "CL_OPT=/Od"
    set "GLSLC_OPT=-g"
)

pushd ..\data\shaders\src
for %%f in (*.vert *.frag *.comp) do glslc %GLSLC_OPT% %%f -o ..\spirv\%%f.spv
popd

IF EXIST ..\..\build_cereus rmdir /S /Q ..\..\build_cereus 2>nul
IF NOT EXIST ..\..\build_cereus mkdir ..\..\build_cereus
pushd ..\..\build_cereus
xcopy /E /I /Y ..\cereus\data .\data >nul

cl -nologo -Z7 /FC /W4 /wd4295 /D_CRT_SECURE_NO_WARNINGS /wd4090 %CL_OPT% ^
    ..\cereus\code\win32_cereus.c ..\cereus\code\renderer_cereus.c ..\cereus\code\cereus.c ^
    /I "%VULKAN_SDK%\Include" ^
    /link /OUT:cereus.exe /LIBPATH:"%VULKAN_SDK%\Lib" vulkan-1.lib user32.lib /DEBUG /PDB:cereus.pdb

popd
