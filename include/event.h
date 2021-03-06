#pragma once

#include "types.h"

struct HNetPeer;
struct HNetPacket;

enum class HNetEventType : uint8_t
{
    None,
    Connect,
    Disconnect,
    Receive,
};

struct HNetEvent
{
    HNetEventType type;
    HNetPeer* peer;
    uint8_t channelId;
    uint32_t data;
    HNetPacket* packet;
};
