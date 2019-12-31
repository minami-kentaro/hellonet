#pragma once

#include "types.h"

struct HNetCompressor
{
    void* context;
    size_t (*compress)(void* context, const HNetBuffer* inBuffers, size_t inBufferCount, size_t inLimit, uint8_t* outData, size_t outLimit);
    size_t (*decompress)(void* context, const uint8_t* inData, size_t inLimit, uint8_t* outData, size_t outLimit);
    void (*destroy)(void* context);
};