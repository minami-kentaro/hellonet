#pragma once

#include "compressor.h"
#include "list.h"
#include "protocol.h"
#include "socket.h"
#include "types.h"

struct HNetEvent;
struct HNetHost;
struct HNetPeer;

#define HNET_HOST_RECV_BUFFER_SIZE         (256 * 1024)
#define HNET_HOST_SEND_BUFFER_SIZE         (256 * 1024)
#define HNET_HOST_DEFAULT_MTU              1400
#define HNET_HOST_DEFAULT_MAX_PACKET_SIZE  (32 * 1024 * 1024)
#define HNET_HOST_DEFAULT_MAX_WAITING_DATA (32 * 1024 * 1024)
#define HNET_BUFFER_MAX                    (1 + 2 * HNET_PROTOCOL_MAX_PACKET_COMMANDS)

using HNetChecksumCallback = uint32_t(*)(const HNetBuffer* pBuffers, size_t bufferCount);
using HNetInterceptCallback = int32_t(*)(HNetHost* pHost, HNetEvent* pEvent);

struct HNetHost
{
    HNetSocket socket;
    HNetAddr addr;
    uint32_t incomingBandwidth;
    uint32_t outgoingBandwidth;
    uint32_t bandwidthThrottleEpoch;
    uint32_t mtu;
    uint32_t randomSeed;
    bool recalculateBandwidthLimits;
    HNetPeer* peers;
    size_t peerCount;
    size_t channelLimit;
    uint32_t serviceTime;
    HNetList dispatchQueue;
    bool continueSending;
    size_t packetSize;
    uint16_t headerFlags;
    HNetProtocol commands[HNET_PROTOCOL_MAX_PACKET_COMMANDS];
    size_t commandCount;
    HNetBuffer buffers[HNET_BUFFER_MAX];
    size_t bufferCount;
    HNetChecksumCallback checksum;
    HNetCompressor compressor;
    uint8_t packetData[2][HNET_PROTOCOL_MAX_MTU];
    HNetAddr recvAddr;
    uint8_t* recvData;
    size_t recvDataLength;
    uint32_t totalSentData;
    uint32_t totalSentPackets;
    uint32_t totalRecvData;
    uint32_t totalRecvPackets;
    HNetInterceptCallback intercept;
    size_t connectedPeers;
    size_t bandwidthLimitedPeers;
    size_t duplicatePeers;
    size_t maxPacketSize;
    size_t maxWaitingData;
};

bool hnet_host_initialize(HNetHost& host, HNetAddr* pAddr, size_t peerCount, size_t channelLimit, uint32_t incomingBandwidth, uint32_t outgoingBandwidth);
void hnet_host_finalize(HNetHost& host);
int32_t hnet_host_service(HNetHost& host, HNetEvent* pEvent);
HNetPeer* hnet_host_connect(HNetHost& host, const HNetAddr& addr, size_t channelCount, uint32_t data);
bool hnet_host_get_addr(const char* pHostName, uint16_t port, HNetAddr& addr);
