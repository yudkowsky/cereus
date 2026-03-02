#pragma once
#include "types.h"
#include "worldstate_structs.h"

void vulkanInitialize(RendererPlatformHandles, DisplayInfo);
void vulkanResize(uint32 width, uint32 height);
