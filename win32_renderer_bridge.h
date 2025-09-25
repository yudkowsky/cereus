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

void rendererInitialise(RendererPlatformHandles handles);
void rendererSubmitFrame(TextureToLoad textures_to_load[128], char* loaded_textures[128]); // are these two needed?
void rendererDraw(void);
void rendererResize(uint32 width, uint32 height);
void rendererShutdown(void);
