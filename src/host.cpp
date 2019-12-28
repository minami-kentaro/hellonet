#include <chrono>
#include "hnet.h"
#include "host.h"
#include "peer.h"
#include "protocol.h"

#define HNET_HOST_RECV_BUFFER_SIZE (256 * 1024)
#define HNET_HOST_SEND_BUFFER_SIZE (256 * 1024)

static uint32_t hnet_host_random_seed()
{
    using namespace std::chrono;
    auto now = system_clock::now();
    return static_cast<uint32_t>(duration_cast<seconds>(now.time_since_epoch()).count());
}

static HNetHost* hnet_host_create(HNetAddr* pAddr, size_t peerCount, size_t channelLimit, uint32_t incomingBandwidth, uint32_t outgoingBandwidth)
{
    if (peerCount > HNET_PROTOCOL_MAX_PEER_ID) {
        return nullptr;
    }

    HNetHost* pHost = new HNetHost{};
    HNetPeer* pPeers = new HNetPeer[peerCount]{};

    HNetSocket socket = hnet_socket_create(HNetSocketType::DataGram);
    if (socket == HNET_SOCKET_NULL) {
        return nullptr;
    }

    if(pAddr != nullptr) {
        if (!hnet_socket_bind(socket, *pAddr)) {
            hnet_socket_destroy(socket);
            return nullptr;
        }
        if (!hnet_socket_get_addr(socket, pHost->addr)) {
            pHost->addr = *pAddr;
        }
    }

    hnet_socket_set_option(socket, HNetSocketOption::NONBLOCK, 1);
    hnet_socket_set_option(socket, HNetSocketOption::BROADCAST, 1);
    hnet_socket_set_option(socket, HNetSocketOption::RCVBUF, HNET_HOST_RECV_BUFFER_SIZE);
    hnet_socket_set_option(socket, HNetSocketOption::SNDBUF, HNET_HOST_SEND_BUFFER_SIZE);

    if (channelLimit == 0 || channelLimit > HNET_PROTOCOL_MAX_CHANNEL_COUNT) {
        channelLimit = HNET_PROTOCOL_MAX_CHANNEL_COUNT;
    }

    pHost->socket = socket;
    pHost->randomSeed = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pHost));
    pHost->randomSeed += hnet_host_random_seed();
    pHost->randomSeed = (pHost->randomSeed << 16) | (pHost->randomSeed >> 16);
    pHost->channelLimit = channelLimit;
    pHost->incomingBandwidth = incomingBandwidth;
    pHost->outgoingBandwidth = outgoingBandwidth;
    pHost->bandwidthThrottleEpoch = 0;
    pHost->recalculateBandwidthLimits = 0;
    pHost->mtu = HNET_HOST_DEFAULT_MTU;
    pHost->peers = pPeers;
    pHost->peerCount = peerCount;
    pHost->commandCount = 0;
    pHost->bufferCount = 0;
    pHost->checksum = nullptr;
    pHost->recvAddr.host = HNET_HOST_ANY;
    pHost->recvAddr.port = 0;
    pHost->recvData = nullptr;
    pHost->recvDataLength = 0;
    pHost->totalSentData = 0;
    pHost->totalSentPackets = 0;
    pHost->totalRecvData = 0;
    pHost->totalRecvPackets = 0;
    pHost->connectedPeers = 0;
    pHost->bandwidthLimitedPeers = 0;
    pHost->duplicatePeers = HNET_PROTOCOL_MAX_PEER_ID;
    pHost->maxPacketSize = HNET_HOST_DEFAULT_MAX_PACKET_SIZE;
    pHost->maxWaitingData = HNET_HOST_DEFAULT_MAX_WAITING_DATA;
    pHost->compressor.context = nullptr;
    pHost->compressor.compress = nullptr;
    pHost->compressor.decompress = nullptr;
    pHost->compressor.destroy = nullptr;
    pHost->intercept = nullptr;

    for (size_t i = 0; i < peerCount; i++) {
        HNetPeer& peer = pHost->peers[i];
        peer.host = pHost;
        peer.incomingPeerId = i;
        peer.outgoingSessionId = peer.incomingSessionId = 0xFF;
        peer.data = nullptr;
        // @TODO: reset
    }

    return pHost;
}

HNetHost* hnet_server_create(HNetAddr& addr, size_t peerCount, size_t channelLimit, uint32_t incomingBandwidth, uint32_t outgoingBandwidth)
{
    return hnet_host_create(&addr, peerCount, channelLimit, incomingBandwidth, outgoingBandwidth);
}

HNetHost* hnet_client_create(uint32_t incomingBandwidth, uint32_t outgoingBandwidth)
{
    return hnet_host_create(nullptr, 1, 0, incomingBandwidth, outgoingBandwidth);
}