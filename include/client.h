#pragma once

#include "host.h"
#include "types.h"

struct HNetPeer;

class HNetClient final
{
public:
    static HNetClient* create(uint32_t incomingBandwidth = 0, uint32_t outgoingBandwidth = 0);
    static void destroy(HNetClient*& client);

    bool connect(const char* pHostName, uint16_t port);
    void update();
    void sendPacket();

private:
    HNetClient();
    ~HNetClient();

private:
    HNetHost m_Host{};
    HNetPeer* m_pCurrentPeer{};
};
