#pragma once

#include "compressor.h"
#include "hnet.h"
#include "protocol.h"
#include "socket.h"
#include "types.h"

struct HNetPeer;

#define HNET_HOST_DEFAULT_MTU              1400
#define HNET_HOST_DEFAULT_MAX_PACKET_SIZE  (32 * 1024 * 1024)
#define HNET_HOST_DEFAULT_MAX_WAITING_DATA (32 * 1024 * 1024)

struct HNetHost
{
    HNetSocket socket;
    HNetAddress addr;
    uint32_t incomingBandwidth;
    uint32_t outgoingBandwidth;
    uint32_t bandwidthThrottleEpoch;
    uint32_t mtu;
    uint32_t randomSeed;
    int32_t recalculateBandwidthLimits;
    HNetPeer* peers;
    size_t peerCount;
    size_t channelLimit;
    uint32_t serviceTime;
    HNetQueue dispatchQueue;
    int32_t continueSending;
    size_t packetSize;
    uint16_t headerFlags;
    HNetProtocol commands[HNET_PROTOCOL_MAX_PACKET_COMMANDS];
    size_t commandCount;
    HNetBuffer buffers[HNET_BUFFER_MAX];
    size_t bufferCount;
    HNetChecksumCallback checksum;
    HNetCompressor compressor;
    uint8_t packetData[2][HNET_PROTOCOL_MAX_MTU];
    HNetAddress recvAddr;
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