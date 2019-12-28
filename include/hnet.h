#pragma once

#include "types.h"

struct HNetHost;
struct HNetPeer;

bool hnet_initialize();
void hnet_finalize();
void hnet_update();

HNetHost* hnet_server_create(HNetAddr& addr, size_t peerCount, size_t channelLimit = 0, uint32_t incomingBandwidth = 0, uint32_t outgoingBandwidth = 0);
HNetHost* hnet_client_create(uint32_t incomingBandwidth = 0, uint32_t outgoingBandwidth = 0);
HNetPeer* hnet_connect(const char* pAddr, uint16_t port);
bool hnet_write();
void hnet_close();
