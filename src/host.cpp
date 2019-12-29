#include <chrono>
#include "allocator.h"
#include "hnet.h"
#include "host.h"
#include "peer.h"
#include "protocol.h"
#include "socket.h"

static uint32_t hnet_host_random_seed()
{
    using namespace std::chrono;
    auto now = system_clock::now();
    return static_cast<uint32_t>(duration_cast<seconds>(now.time_since_epoch()).count());
}

bool hnet_host_initialize(HNetHost& host, HNetAddr* pAddr, size_t peerCount, size_t channelLimit, uint32_t incomingBandwidth, uint32_t outgoingBandwidth)
{
    if (peerCount > HNET_PROTOCOL_MAX_PEER_ID) {
        return false;
    }

    HNetSocket socket = hnet_socket_create(HNetSocketType::DataGram);
    if (socket == HNET_SOCKET_NULL) {
        return false;
    }

    if(pAddr != nullptr) {
        if (!hnet_socket_bind(socket, *pAddr)) {
            hnet_socket_destroy(socket);
            return false;
        }
        if (!hnet_socket_get_addr(socket, host.addr)) {
            host.addr = *pAddr;
        }
    }

    hnet_socket_set_option(socket, HNetSocketOption::NONBLOCK, 1);
    hnet_socket_set_option(socket, HNetSocketOption::BROADCAST, 1);
    hnet_socket_set_option(socket, HNetSocketOption::RCVBUF, HNET_HOST_RECV_BUFFER_SIZE);
    hnet_socket_set_option(socket, HNetSocketOption::SNDBUF, HNET_HOST_SEND_BUFFER_SIZE);

    if (channelLimit == 0 || channelLimit > HNET_PROTOCOL_MAX_CHANNEL_COUNT) {
        channelLimit = HNET_PROTOCOL_MAX_CHANNEL_COUNT;
    }

    HNetPeer* pPeers = static_cast<HNetPeer*>(hnet_malloc(peerCount * sizeof(HNetPeer)));
    if (pPeers == nullptr) {
        hnet_socket_destroy(socket);
        return false;
    }
    memset(pPeers, 0, peerCount * sizeof(HNetPeer));

    host.socket = socket;
    host.randomSeed = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&host));
    host.randomSeed += hnet_host_random_seed();
    host.randomSeed = (host.randomSeed << 16) | (host.randomSeed >> 16);
    host.channelLimit = channelLimit;
    host.incomingBandwidth = incomingBandwidth;
    host.outgoingBandwidth = outgoingBandwidth;
    host.bandwidthThrottleEpoch = 0;
    host.recalculateBandwidthLimits = 0;
    host.mtu = HNET_HOST_DEFAULT_MTU;
    host.peers = pPeers;
    host.peerCount = peerCount;
    host.commandCount = 0;
    host.bufferCount = 0;
    host.checksum = nullptr;
    host.recvAddr.host = HNET_HOST_ANY;
    host.recvAddr.port = 0;
    host.recvData = nullptr;
    host.recvDataLength = 0;
    host.totalSentData = 0;
    host.totalSentPackets = 0;
    host.totalRecvData = 0;
    host.totalRecvPackets = 0;
    host.connectedPeers = 0;
    host.bandwidthLimitedPeers = 0;
    host.duplicatePeers = HNET_PROTOCOL_MAX_PEER_ID;
    host.maxPacketSize = HNET_HOST_DEFAULT_MAX_PACKET_SIZE;
    host.maxWaitingData = HNET_HOST_DEFAULT_MAX_WAITING_DATA;
    host.compressor.context = nullptr;
    host.compressor.compress = nullptr;
    host.compressor.decompress = nullptr;
    host.compressor.destroy = nullptr;
    host.intercept = nullptr;

    for (size_t i = 0; i < peerCount; i++) {
        HNetPeer& peer = host.peers[i];
        peer.host = &host;
        peer.incomingPeerId = i;
        peer.outgoingSessionId = peer.incomingSessionId = 0xFF;
        peer.data = nullptr;
        peer.acks.clear();
        peer.sentReliableCommands.clear();
        peer.sentUnreliableCommands.clear();
        peer.outgoingReliableCommands.clear();
        peer.outgoingUnreliableCommands.clear();
        peer.dispatchedCommands.clear();
        hnet_peer_reset(peer);
    }

    return true;
}

void hnet_host_finalize(HNetHost& host)
{
    hnet_socket_destroy(host.socket);
    for (size_t i = 0; i < host.peerCount; i++) {
        hnet_peer_reset(host.peers[i]);
    }
    hnet_free(host.peers);
}
