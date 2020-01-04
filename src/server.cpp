#include <iostream>
#include "event.h"
#include "packet.h"
#include "peer.h"
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

void HNetServer::update()
{
    HNetEvent event;
    int32_t ret = hnet_host_service(m_Host, event);
    if (ret > 0) {
        switch (event.type) {
        case HNetEventType::Connect:
            std::cout << "HNetSever connected" << std::endl;
            m_pPeer = event.peer;
            break;
        case HNetEventType::Disconnect:
            std::cout << "HNetServer disconnected" << std::endl;
            m_pPeer = nullptr;
            break;
        case HNetEventType::Receive:
            printf("A packet of length %zu containing %s was received on channel %u.\n",
                event.packet->dataLength,
                reinterpret_cast<const char*>(event.packet->data),
                event.channelId);
            hnet_packet_destroy(event.packet);
            break;
        default:
            break;
        }
    }
}

void HNetServer::sendPacket()
{
    static uint8_t counter = 0;
    char msg[128]{};
    snprintf(msg, 128, "msg from server: %d", counter++);
    HNetPacket* pPacket = hnet_packet_create(reinterpret_cast<uint8_t*>(msg), strlen(msg) + 1, HNET_PACKET_FLAG_RELIABLE);
    if (pPacket != nullptr && m_pPeer != nullptr) {
        hnet_peer_send(*m_pPeer, 0, *pPacket);
    }
}
