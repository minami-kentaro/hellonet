#pragma once

#include "host.h"
#include "types.h"

class HNetClient final : public HNetHost
{
public:
    static HNetClient* create(uint32_t incomingBandwidth = 0, uint32_t outgoingBandwidth = 0);
    static void destroy(HNetClient*& client);

private:
    HNetClient();
    ~HNetClient();
};
