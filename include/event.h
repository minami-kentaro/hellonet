#pragma once

struct HNetPeer;
struct HNetPacket;

enum class HNetEventType
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
