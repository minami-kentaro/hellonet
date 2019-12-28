#pragma once

#include "types.h"
#include "socket.h"

struct HNetEvent;
struct HNetHost;
struct HNetPeer;

using HNetChecksumCallback = uint32_t(*)(const HNetBuffer* pBuffers, size_t bufferCount);
using HNetInterceptCallback = int32_t(*)(HNetHost* pHost, HNetEvent* pEvent);

bool hnet_initialize();
void hnet_finalize();
void hnet_update();

HNetHost* hnet_server_create(HNetAddress& addr, size_t peerCount, size_t channelLimit = 0, uint32_t incomingBandwidth = 0, uint32_t outgoingBandwidth = 0);
HNetPeer* hnet_connect(const char* pAddr, uint16_t port);
bool hnet_write();
void hnet_close();
