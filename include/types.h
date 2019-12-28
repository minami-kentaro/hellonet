#pragma once

#include <cstdint>
#include <list>

using HNetList = std::list<void*>;

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