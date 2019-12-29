#pragma once

#include <cstdint>
#include <stddef.h>
#include <string.h>

struct HNetAddr
{
    uint32_t host;
    uint16_t port;
};

struct HNetBuffer
{
    void* data;
    size_t dataLength;
};

#define HNET_HOST_ANY 0