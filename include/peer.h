#pragma once

#include "types.h"

#define HNET_PEER_RELIABLE_WINDOWS        16
#define HNET_PEER_UNSEQUENCED_WINDOW_SIZE 1024

struct HNetHost;

enum class HNetPeerState : uint32_t
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

struct HNetPeer
{
    HNetList dispatchList;
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