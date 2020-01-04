#include <iostream>
#include "client.h"
#include "event.h"
#include "packet.h"
#include "peer.h"

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

void HNetClient::update()
{
    HNetEvent event;
    int32_t ret = hnet_host_service(m_Host, event);
    if (ret > 0) {
        switch (event.type) {
        case HNetEventType::Connect:
            std::cout << "HNetClient connected" << std::endl;
            break;
        case HNetEventType::Disconnect:
            std::cout << "HNetClient disconnected" << std::endl;
            break;
        case HNetEventType::Receive:
            printf("A packet of length %zu containing %s was received on channel %u.\n",
                event.packet->dataLength,
                event.packet->data,
                event.channelId);
            hnet_packet_destroy(event.packet);
            break;
        default:
            break;
        }
    }
}

void HNetClient::sendPacket()
{
    static uint8_t counter = 0;
    char msg[128]{};
    snprintf(msg, 128, "msg from client: %d", counter++);
    HNetPacket* pPacket = hnet_packet_create(reinterpret_cast<uint8_t*>(msg), strlen(msg) + 1, HNET_PACKET_FLAG_RELIABLE);
    if (pPacket != nullptr) {
        hnet_peer_send(*m_pCurrentPeer, 0, *pPacket);
    }
}