#include "server.h"
#include "socket.h"

HNetServer::HNetServer()
{}

HNetServer::~HNetServer()
{}

HNetServer* HNetServer::create(const char* pHostName, uint16_t port, size_t peerCount, size_t channelLimit, uint32_t incomingBandwidth, uint32_t outgoingBandwidth)
{
    HNetAddr addr{HNET_HOST_ANY, port};
    if (pHostName != nullptr) {
        if (!hnet_address_set_host(addr, pHostName)) {
            return nullptr;
        }
    }

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