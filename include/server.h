#pragma once

#include "host.h"
#include "types.h"

class HNetServer final : public HNetHost
{
public:
    static HNetServer* create(HNetAddr& addr, size_t peerCount, size_t channelLimit = 0, uint32_t incomingBandwidth = 0, uint32_t outgoingBandwidth = 0);
    static void destroy(HNetServer*& pServer);

private:
    HNetServer();
    ~HNetServer();
};
