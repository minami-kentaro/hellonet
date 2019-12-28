#pragma once

#include "types.h"

struct HNetPacket;

using HNetPacketFreeCallback = void(*)(HNetPacket*);

#define HNET_PACKET_FLAG_RELIABLE            (1 << 0)
#define HNET_PACKET_FLAG_UNSEQUENCED         (1 << 1)
#define HNET_PACKET_FLAG_NO_ALLOCATE         (1 << 2)
#define HNET_PACKET_FLAG_UNRELIABLE_FRAGMENT (1 << 3)
#define HNET_PACKET_FLAG_SEND                (1 << 8)

struct HNetPacket
{
    size_t refCount;
    uint32_t flags;
    uint8_t* data;
    size_t dataLength;
    HNetPacketFreeCallback freeCallback;
    void* userData;
};
