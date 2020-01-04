#pragma once

#include "host.h"
#include "types.h"

struct HNetPeer;

class HNetServer final
{
public:
    static HNetServer* create(const char* pHostName, uint16_t port, size_t peerCount, size_t channelLimit = 0, uint32_t incomingBandwidth = 0, uint32_t outgoingBandwidth = 0);
    static void destroy(HNetServer*& pServer);
    void update();
    void sendPacket();

private:
    HNetServer();
    ~HNetServer();

private:
    HNetHost m_Host{};
    HNetPeer* m_pPeer{};
};
