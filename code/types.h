#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef struct Vec2
{
    float x, y;
}
Vec2;
typedef struct Vec3
{
    float x, y, z;
}
Vec3;
typedef struct Vec4
{
    float x, y, z, w;
}
Vec4;

typedef struct Int2
{
    int32 x, y;
}
Int2;

typedef struct Int3
{
    int32 x, y, z;
}
Int3;
