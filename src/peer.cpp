#include "allocator.h"
#include "host.h"
#include "packet.h"
#include "peer.h"

static void hnet_peer_reset_outgoing_commands(HNetList& queue)
{
    while (!queue.empty()) {
        HNetListNode* pNode = queue.front();
        queue.remove(pNode);
        HNetOutgoingCommand* pCmd = reinterpret_cast<HNetOutgoingCommand*>(pNode);
        if (pCmd->packet != nullptr) {
            size_t refCount = --pCmd->packet->refCount;
            if (refCount == 0) {
                hnet_packet_destroy(pCmd->packet);
            }
        }
        hnet_free(pCmd);
    }
}

static void hnet_peer_remove_incoming_commands(HNetList& queue, HNetListNode* pStart, HNetListNode* pEnd)
{
    if (pStart == nullptr || pEnd == nullptr) {
        return;
    }

    for (HNetListNode* pNode = pStart; pNode != pEnd; ) {
        HNetIncomingCommand* pCmd = reinterpret_cast<HNetIncomingCommand*>(pNode);
        pNode = pNode->next;
        HNetList::remove(&pCmd->incomingCommandList);
        if (pCmd->packet != nullptr) {
            size_t refCount = --pCmd->packet->refCount;
            if (refCount == 0) {
                hnet_packet_destroy(pCmd->packet);
            }
        }
        if (pCmd->fragments != nullptr) {
            hnet_free(pCmd->fragments);
        }
        hnet_free(pCmd);
    }
}

static void hnet_peer_reset_incoming_commands(HNetList& queue)
{
    hnet_peer_remove_incoming_commands(queue, queue.begin(), queue.end());
}

void hnet_peer_on_disconnect(HNetPeer& peer)
{
    if (peer.state == HNetPeerState::Connected || peer.state == HNetPeerState::DisconnectLater) {
        if (peer.incomingBandwidth != 0) {
            --peer.host->bandwidthLimitedPeers;
        }
        --peer.host->connectedPeers;
    }
}

void hnet_peer_reset(HNetPeer& peer)
{
    hnet_peer_on_disconnect(peer);
    peer.outgoingPeerId = HNET_PROTOCOL_MAX_PEER_ID;
    peer.connectId = 0;
    peer.state = HNetPeerState::Disconnected;
    peer.incomingBandwidth = 0;
    peer.outgoingBandwidth = 0;
    peer.incomingBandwidthThrottoleEpoch = 0;
    peer.outgoingBandwidthThrottoleEpoch = 0;
    peer.incomingDataTotal = 0;
    peer.outgoingDataTotal = 0;
    peer.lastSendTime = 0;
    peer.lastRecvTime = 0;
    peer.nextTimeout = 0;
    peer.earliestTimeout = 0;
    peer.packetLossEpoch = 0;
    peer.packetsSent = 0;
    peer.packetsLost = 0;
    peer.packetLoss = 0;
    peer.packetLossVariance = 0;
    peer.packetThrottle = HNET_PEER_DEFAULT_PACKET_THROTTLE;
    peer.packetThrottleLimit = HNET_PEER_PACKET_THROTTLE_SCALE;
    peer.packetThrottleCounter = 0;
    peer.packetThrottleEpoch = 0;
    peer.packetThrottleAcceleration = HNET_PEER_PACKET_THROTTLE_ACCELERATION;
    peer.packetThrottleDeceleration = HNET_PEER_PACKET_THROTTLE_DECELERATION;
    peer.packetThrottleInterval = HNET_PEER_PACKET_THROTTLE_INTERVAL;
    peer.pingInterval = HNET_PEER_PING_INTERVAL;
    peer.timeoutLimit = HNET_PEER_TIMEOUT_LIMIT;
    peer.timeoutMin = HNET_PEER_TIMEOUT_MIN;
    peer.timeoutMax = HNET_PEER_TIMEOUT_MAX;
    peer.lastRoundTripTime = HNET_PEER_DEFAULT_ROUND_TRIP_TIME;
    peer.lowestRoundTripTime = HNET_PEER_DEFAULT_ROUND_TRIP_TIME;
    peer.lastRoundTripTimeVariance = 0;
    peer.highestRoundTripTimeVariance = 0;
    peer.roundTripTime = HNET_PEER_DEFAULT_ROUND_TRIP_TIME;
    peer.roundTripTimeVariance = 0;
    peer.mtu = peer.host->mtu;
    peer.reliableDataInTransit = 0;
    peer.outgoingReliableSeqNumber = 0;
    peer.windowSize = HNET_PROTOCOL_MAX_WINDOW_SIZE;
    peer.incomingUnseqGroup = 0;
    peer.outgoingUnseqGroup = 0;
    peer.eventData = 0;
    peer.totalWaitingData = 0;
    memset(peer.unseqWindow, 0, sizeof(peer.unseqWindow));
    hnet_peer_reset_queues(peer);
}

void hnet_peer_reset_queues(HNetPeer& peer)
{
    if (peer.needsDispatch) {
        HNetList::remove(&peer.dispatchList);
        peer.needsDispatch = false;
    }

    while (!peer.acks.empty()) {
        HNetListNode* pNode = peer.acks.front();
        HNetList::remove(pNode);
        hnet_free(pNode);
    }

    hnet_peer_reset_outgoing_commands(peer.sentReliableCommands);
    hnet_peer_reset_outgoing_commands(peer.sentUnreliableCommands);
    hnet_peer_reset_outgoing_commands(peer.outgoingReliableCommands);
    hnet_peer_reset_outgoing_commands(peer.outgoingUnreliableCommands);
    hnet_peer_reset_incoming_commands(peer.dispatchedCommands);

    for (size_t i = 0; i < peer.channelCount; i++) {
        HNetChannel& channel = peer.channels[i];
        hnet_peer_reset_incoming_commands(channel.incomingReliableCommands);
        hnet_peer_reset_incoming_commands(channel.incomingUnreliableCommands);
    }

    if (peer.channels != nullptr) {
        hnet_free(peer.channels);
    }
}