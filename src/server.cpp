#include "server.h"
#include "socket.h"

HNetServer::HNetServer()
{}

HNetServer::~HNetServer()
{}

HNetServer* HNetServer::create(const char* pHostName, uint16_t port, size_t peerCount, size_t channelLimit, uint32_t incomingBandwidth, uint32_t outgoingBandwidth)
{
    HNetAddr addr{};
    if (!hnet_host_get_addr(pHostName, port, addr)) {
        return nullptr;
    }

    HNetServer* pServer = new HNetServer();
    if (!hnet_host_initialize(pServer->m_Host, &addr, peerCount, channelLimit, incomingBandwidth, outgoingBandwidth)) {
        delete pServer;
        return nullptr;
    }

    return pServer;
}

void HNetServer::destroy(HNetServer*& pServer)
{
    if (pServer != nullptr) {
        hnet_host_finalize(pServer->m_Host);
        delete pServer;
        pServer = nullptr;
    }
}