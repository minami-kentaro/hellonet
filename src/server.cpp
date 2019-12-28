#include "server.h"

HNetServer::HNetServer()
{}

HNetServer::~HNetServer()
{}

HNetServer* HNetServer::create(HNetAddr& addr, size_t peerCount, size_t channelLimit, uint32_t incomingBandwidth, uint32_t outgoingBandwidth)
{
    HNetServer* pServer = new HNetServer();
    if (!hnet_host_initialize(pServer, &addr, peerCount, channelLimit, incomingBandwidth, outgoingBandwidth)) {
        delete pServer;
        return nullptr;
    }
    return pServer;
}

void HNetServer::destroy(HNetServer*& pServer)
{
    // @TODO: finalize
    delete pServer;
    pServer = nullptr;
}