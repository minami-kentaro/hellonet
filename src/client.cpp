#include "client.h"

HNetClient::HNetClient()
{}

HNetClient::~HNetClient()
{}

HNetClient* HNetClient::create(uint32_t incomingBandwidth, uint32_t outgoingBandwidth)
{
    HNetClient* pClient = new HNetClient();
    if (!hnet_host_initialize(pClient->m_Host, nullptr, 1, 0, incomingBandwidth, outgoingBandwidth)) {
        delete pClient;
        return nullptr;
    }
    return pClient;
}

void HNetClient::destroy(HNetClient*& pClient)
{
    if (pClient != nullptr) {
        hnet_host_finalize(pClient->m_Host);
        delete pClient;
        pClient = nullptr;
    }
}

bool HNetClient::connect(const char* pHostName, uint16_t port)
{
    HNetAddr addr{};
    if (!hnet_host_get_addr(pHostName, port, addr)) {
        return false;
    }

    m_pCurrentPeer = hnet_host_connect(m_Host, addr, 0xFF, 0);
    return true;
}