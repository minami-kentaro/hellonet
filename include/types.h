#pragma once

#include <cstdint>
#include <stddef.h>
#include <string.h>

struct HNetAddr
{
    uint32_t host;
    uint16_t port;
};

inline bool operator!=(const HNetAddr& a, const HNetAddr& b)
{
    return (a.host != b.host) || (a.port != b.port);
}

struct HNetBuffer
{
    void* data;
    size_t dataLength;
};

#define HNET_HOST_ANY       0
#define HNET_HOST_BROADCAST 0xFFFFFFFFU