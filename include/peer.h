#pragma once

#include "list.h"
#include "protocol.h"
#include "types.h"

struct HNetPacket;

#define HNET_PEER_DEFAULT_ROUND_TRIP_TIME      500
#define HNET_PEER_DEFAULT_PACKET_THROTTLE      32
#define HNET_PEER_PACKET_THROTTLE_SCALE        32
#define HNET_PEER_PACKET_THROTTLE_COUNTER      7
#define HNET_PEER_PACKET_THROTTLE_ACCELERATION 2
#define HNET_PEER_PACKET_THROTTLE_DECELERATION 2
#define HNET_PEER_PACKET_THROTTLE_INTERVAL     5000
#define HNET_PEER_PACKET_LOSS_SCALE            (1 << 16)
#define HNET_PEER_PACKET_LOSS_INTERVAL         10000
#define HNET_PEER_WINDOW_SIZE_SCALE            (64 * 1024)
#define HNET_PEER_TIMEOUT_LIMIT                32
#define HNET_PEER_TIMEOUT_MIN                  5000
#define HNET_PEER_TIMEOUT_MAX                  30000
#define HNET_PEER_PING_INTERVAL                500
#define HNET_PEER_UNSEQUENCED_WINDOWS          64
#define HNET_PEER_UNSEQUENCED_WINDOW_SIZE      1024
#define HNET_PEER_FREE_UNSEQUENCED_WINDOWS     32
#define HNET_PEER_RELIABLE_WINDOWS             16
#define HNET_PEER_RELIABLE_WINDOW_SIZE         0x1000
#define HNET_PEER_FREE_RELIABLE_WINDOWS        8

struct HNetHost;

enum class HNetPeerState : uint8_t
{
    Disconnected,
    Connecting,
    AckConnect,
    ConnectionPending,
    ConnectionSucceeded,
    Connected,
    DisconnectLater,
    Disconnecting,
    AckDisconnet,
    Zombie,
};

struct HNetChannel
{
    uint16_t outgoingReliableSeqNumber;
    uint16_t outgoingUnreliableSeqNumber;
    uint16_t usedReliableWindows;
    uint16_t reliableWindows[HNET_PEER_RELIABLE_WINDOWS];
    uint16_t incomingReliableSeqNumber;
    uint16_t incomingUnreliableSeqNumber;
    HNetList incomingReliableCommands;
    HNetList incomingUnreliableCommands;
};

struct HNetPeer final
{
    HNetListNode dispatchList;
    HNetHost* host;
    uint16_t outgoingPeerId;
    uint16_t incomingPeerId;
    uint32_t connectId;
    uint8_t outgoingSessionId;
    uint8_t incomingSessionId;
    HNetAddr addr;
    void* data;
    HNetPeerState state;
    HNetChannel* channels;
    size_t channelCount;
    uint32_t incomingBandwidth;
    uint32_t outgoingBandwidth;
    uint32_t incomingBandwidthThrottoleEpoch;
    uint32_t outgoingBandwidthThrottoleEpoch;
    uint32_t incomingDataTotal;
    uint32_t outgoingDataTotal;
    uint32_t lastSendTime;
    uint32_t lastRecvTime;
    uint32_t nextTimeout;
    uint32_t earliestTimeout;
    uint32_t packetLossEpoch;
    uint32_t packetsSent;
    uint32_t packetsLost;
    uint32_t packetLoss;
    uint32_t packetLossVariance;
    uint32_t packetThrottle;
    uint32_t packetThrottleLimit;
    uint32_t packetThrottleCounter;
    uint32_t packetThrottleEpoch;
    uint32_t packetThrottleAcceleration;
    uint32_t packetThrottleDeceleration;
    uint32_t packetThrottleInterval;
    uint32_t pingInterval;
    uint32_t timeoutLimit;
    uint32_t timeoutMin;
    uint32_t timeoutMax;
    uint32_t lastRoundTripTime;
    uint32_t lowestRoundTripTime;
    uint32_t lastRoundTripTimeVariance;
    uint32_t lowestRoundTripTimeVariance;
    uint32_t highestRoundTripTimeVariance;
    uint32_t roundTripTime;
    uint32_t roundTripTimeVariance;
    uint32_t mtu;
    uint32_t windowSize;
    uint32_t reliableDataInTransit;
    uint16_t outgoingReliableSeqNumber;
    HNetList acks;
    HNetList sentReliableCommands;
    HNetList sentUnreliableCommands;
    HNetList outgoingReliableCommands;
    HNetList outgoingUnreliableCommands;
    HNetList dispatchedCommands;
    bool needsDispatch;
    uint16_t incomingUnseqGroup;
    uint16_t outgoingUnseqGroup;
    uint32_t unseqWindow[HNET_PEER_UNSEQUENCED_WINDOW_SIZE / 32];
    uint32_t eventData;
    size_t totalWaitingData;
};

struct HNetAck final
{
    HNetListNode ackList;
    uint32_t sentTime;
    HNetProtocol command;
};

struct HNetOutgoingCommand final
{
    HNetListNode outgoingCommandList;
    uint16_t reliableSeqNumber;
    uint16_t unreliableSeqNumber;
    uint32_t sentTime;
    uint32_t roundTripTimeout;
    uint32_t roundTripTimeoutLimit;
    uint32_t fragmentOffset;
    uint16_t fragmentLength;
    uint16_t sendAttempts;
    HNetProtocol command;
    HNetPacket* packet;
};

struct HNetIncomingCommand final
{
    HNetListNode incomingCommandList;
    uint16_t reliableSeqNumber;
    uint16_t unreliableSeqNumber;
    HNetProtocol command;
    uint32_t fragmentCount;
    uint32_t fragmentsRemaining;
    uint32_t* fragments;
    HNetPacket* packet;
};

void hnet_peer_on_disconnect(HNetPeer& peer);
void hnet_peer_reset(HNetPeer& peer);
void hnet_peer_reset_queues(HNetPeer& peer);
