#pragma once
#include "types.h"
#include "worldstate_structs.h"

#define local_persist static
#define global_variable static
#define internal static

typedef struct
{
    void* module_handle;
    void* window_handle;
}
RendererPlatformHandles;

void rendererInitialize(RendererPlatformHandles handles);
void rendererSubmitFrame(DrawCommand* draw_commands, int32 draw_command_count, Camera camera); // are these two needed?
void rendererDraw(void);
void rendererResize(uint32 width, uint32 height);
void rendererShutdown(void);
