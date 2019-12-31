#include <algorithm>
#include "allocator.h"
#include "event.h"
#include "hnet.h"
#include "hnet_time.h"
#include "host.h"
#include "peer.h"
#include "protocol.h"
#include "socket.h"

static uint32_t hnet_host_random_seed()
{
    return static_cast<uint32_t>(hnet_time_now_sec());
}

static HNetSocket hnet_host_create_socket()
{
    HNetSocket socket = hnet_socket_create(HNetSocketType::DataGram);
    if (socket != HNET_SOCKET_NULL) {
        hnet_socket_set_option(socket, HNetSocketOption::NONBLOCK, 1);
        hnet_socket_set_option(socket, HNetSocketOption::BROADCAST, 1);
        hnet_socket_set_option(socket, HNetSocketOption::RCVBUF, HNET_HOST_RECV_BUFFER_SIZE);
        hnet_socket_set_option(socket, HNetSocketOption::SNDBUF, HNET_HOST_SEND_BUFFER_SIZE);
    }
    return socket;
}

static bool hnet_host_bind_socket(HNetHost& host, HNetSocket socket, HNetAddr& addr)
{
    if (!hnet_socket_bind(socket, addr)) {
        return false;
    }
    if (!hnet_socket_get_addr(socket, host.addr)) {
        host.addr = addr;
    }
    return true;
}

static HNetPeer* hnet_host_create_peers(HNetHost& host, size_t peerCount)
{
    if (peerCount > HNET_PROTOCOL_MAX_PEER_ID) {
        return nullptr;
    }

    HNetPeer* pPeers = static_cast<HNetPeer*>(hnet_malloc(peerCount * sizeof(HNetPeer)));
    if (pPeers == nullptr) {
        return nullptr;
    }
    memset(pPeers, 0, peerCount * sizeof(HNetPeer));

    for (size_t i = 0; i < peerCount; i++) {
        HNetPeer& peer = pPeers[i];
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

    return pPeers;
}

static HNetChannel* hnet_host_create_channels(size_t& channelCount)
{
    channelCount = std::clamp<size_t>(channelCount, HNET_PROTOCOL_MIN_CHANNEL_COUNT, HNET_PROTOCOL_MAX_CHANNEL_COUNT);
    HNetChannel* pChannels = static_cast<HNetChannel*>(hnet_malloc(channelCount * sizeof(HNetChannel)));
    if (pChannels == nullptr) {
        return nullptr;
    }

    for (size_t i = 0; i < channelCount; i++) {
        HNetChannel& channel = pChannels[i];
        channel.outgoingReliableSeqNumber = 0;
        channel.outgoingUnreliableSeqNumber = 0;
        channel.incomingReliableSeqNumber = 0;
        channel.incomingUnreliableSeqNumber = 0;
        channel.incomingReliableCommands.clear();
        channel.incomingUnreliableCommands.clear();
        channel.usedReliableWindows = 0;
        memset(channel.reliableWindows, 0, sizeof(channel.reliableWindows));
    }

    return pChannels;
}

static HNetPeer* hnet_host_find_available_peer(HNetHost& host)
{
    HNetPeer* pPeer = nullptr;
    for (size_t i = 0; i < host.peerCount; i++) {
        HNetPeer& peer = host.peers[i];
        if (peer.state == HNetPeerState::Disconnected) {
            pPeer = &peer;
            break;
        }
    }
    return pPeer;
}

uint32_t hnet_host_get_init_window_size(uint32_t outgoingBandwidth)
{
    uint32_t windowSize = HNET_PROTOCOL_MAX_WINDOW_SIZE;
    if (outgoingBandwidth > 0) {
        windowSize = outgoingBandwidth / HNET_PEER_WINDOW_SIZE_SCALE * HNET_PROTOCOL_MIN_WINDOW_SIZE;
    }
    return std::clamp<uint32_t>(windowSize, HNET_PROTOCOL_MIN_WINDOW_SIZE, HNET_PROTOCOL_MAX_WINDOW_SIZE);
}

bool hnet_host_initialize(HNetHost& host, HNetAddr* pAddr, size_t peerCount, size_t channelLimit, uint32_t incomingBandwidth, uint32_t outgoingBandwidth)
{
    HNetSocket socket = hnet_host_create_socket();
    if (socket == HNET_SOCKET_NULL) {
        return false;
    }

    if(pAddr != nullptr && !hnet_host_bind_socket(host, socket, *pAddr)) {
        hnet_socket_destroy(socket);
        return false;
    }

    HNetPeer* pPeers = hnet_host_create_peers(host, peerCount);
    if (pPeers == nullptr) {
        hnet_socket_destroy(socket);
        return false;
    }

    if (channelLimit == 0 || channelLimit > HNET_PROTOCOL_MAX_CHANNEL_COUNT) {
        channelLimit = HNET_PROTOCOL_MAX_CHANNEL_COUNT;
    }

    host.socket = socket;
    host.randomSeed = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&host));
    host.randomSeed += hnet_host_random_seed();
    host.randomSeed = (host.randomSeed << 16) | (host.randomSeed >> 16);
    host.channelLimit = channelLimit;
    host.incomingBandwidth = incomingBandwidth;
    host.outgoingBandwidth = outgoingBandwidth;
    host.bandwidthThrottleEpoch = 0;
    host.recalculateBandwidthLimits = false;
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

int32_t hnet_host_service(HNetHost& host, HNetEvent& event)
{
    host.serviceTime = static_cast<uint32_t>(hnet_time_now_msec());
    event.type = HNetEventType::None;
    event.peer = nullptr;
    event.packet = nullptr;

    int32_t ret = hnet_protocol_dispatch_incoming_commands(host, event);
    if (ret != 0) {
        return ret;
    }

    ret = hnet_protocol_send_outgoing_commands(host, &event, true);
    if (ret != 0) {
        return ret;
    }

    ret = hnet_protocol_recv_incoming_commands(host, event);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

HNetPeer* hnet_host_connect(HNetHost& host, const HNetAddr& addr, size_t channelCount, uint32_t data)
{
    HNetPeer* pCurrentPeer = hnet_host_find_available_peer(host);
    if (pCurrentPeer == nullptr) {
        return nullptr;
    }

    HNetChannel* pChannels = hnet_host_create_channels(channelCount);
    if (pChannels == nullptr) {
        return nullptr;
    }

    pCurrentPeer->channels = pChannels;
    pCurrentPeer->channelCount = channelCount;
    pCurrentPeer->state = HNetPeerState::Connecting;
    pCurrentPeer->addr = addr;
    pCurrentPeer->connectId = ++host.randomSeed;
    pCurrentPeer->windowSize = hnet_host_get_init_window_size(host.outgoingBandwidth);

    HNetProtocol cmd;
    hnet_protocol_init_connect_command(host, *pCurrentPeer, data, cmd);

    if (!hnet_peer_queue_outgoing_command(*pCurrentPeer, cmd, nullptr, 0, 0)) {
        hnet_free(pChannels);
        return nullptr;
    }

    return pCurrentPeer;
}

void hnet_host_flush(HNetHost& host)
{
    host.serviceTime = hnet_time_now_msec();
    hnet_protocol_send_outgoing_commands(host, nullptr, false);
}

bool hnet_host_get_addr(const char* pHostName, uint16_t port, HNetAddr& addr)
{
    addr.host = HNET_HOST_ANY;
    addr.port = port;
    if (pHostName != nullptr) {
        if (!hnet_address_set_host(addr, pHostName)) {
            return false;
        }
    }
    return true;
}