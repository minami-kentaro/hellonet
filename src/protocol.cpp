#include <cmath>
#include "allocator.h"
#include "event.h"
#include "hnet_time.h"
#include "host.h"
#include "peer.h"
#include "protocol.h"
#include "socket.h"

static size_t commandSizes[HNET_PROTOCOL_COMMAND_COUNT] = {
    0,
    sizeof(HNetProtocolAck),
    sizeof(HNetProtocolConnect),
    sizeof(HNetProtocolVerifyConnect),
    sizeof(HNetProtocolDisconnect),
    sizeof(HNetProtocolPing),
    sizeof(HNetProtocolSendReliable),
    sizeof(HNetProtocolSendUnreliable),
    sizeof(HNetProtocolSendFragment),
    sizeof(HNetProtocolSendUnsequenced),
    sizeof(HNetProtocolBandwidthLimit),
    sizeof(HNetProtocolThrottleConfigure),
    sizeof(HNetProtocolSendFragment),
};

static void hnet_protocol_change_state(HNetPeer& peer, HNetPeerState state)
{
    if (state == HNetPeerState::Connected || state == HNetPeerState::DisconnectLater) {
        hnet_peer_on_connect(peer);
    } else {
        hnet_peer_on_disconnect(peer);
    }
    peer.state = state;
}

static void hnet_protocol_dispatch_state(HNetHost& host, HNetPeer& peer, HNetPeerState state)
{
    hnet_protocol_change_state(peer, state);
    if (!peer.needsDispatch) {
        host.dispatchQueue.push_back(reinterpret_cast<HNetListNode*>(&peer));
        peer.needsDispatch = true;
    }
}

static void hnet_protocol_notify_disconnect(HNetHost& host, HNetPeer& peer, HNetEvent* pEvent)
{
    if (static_cast<uint8_t>(peer.state) >= static_cast<uint8_t>(HNetPeerState::ConnectionPending)) {
        host.recalculateBandwidthLimits = true;
    }
    if (peer.state == HNetPeerState::Disconnected || peer.state == HNetPeerState::AckConnect || peer.state == HNetPeerState::ConnectionPending) {
        hnet_peer_reset(peer);
    } else {
        if (pEvent != nullptr) {
            pEvent->type = HNetEventType::Disconnect;
            pEvent->peer = &peer;
            pEvent->data = 0;
            hnet_peer_reset(peer);
        } else {
            peer.eventData = 0;
            hnet_protocol_dispatch_state(host, peer, HNetPeerState::Zombie);
        }
    }
}

static bool hnet_protocol_check_timeouts(HNetHost& host, HNetPeer& peer, HNetEvent* pEvent)
{
    for (HNetListNode* pNode = peer.sentReliableCommands.begin(); pNode != peer.sentReliableCommands.end();) {
        HNetOutgoingCommand* pCmd = reinterpret_cast<HNetOutgoingCommand*>(pNode);
        if (HNET_TIME_DIFF(host.serviceTime, pCmd->sentTime) < pCmd->roundTripTimeout) {
            continue;
        }

        if (peer.earliestTimeout == 0 || HNET_TIME_LT(pCmd->sentTime, peer.earliestTimeout)) {
            peer.earliestTimeout = pCmd->sentTime;
        }

        if (peer.earliestTimeout != 0) {
            if (HNET_TIME_DIFF(host.serviceTime, peer.earliestTimeout) >= peer.timeoutMax) {
                hnet_protocol_notify_disconnect(host, peer, pEvent);
                return true;
            } else if (pCmd->roundTripTimeout >= pCmd->roundTripTimeoutLimit &&
                       HNET_TIME_DIFF(host.serviceTime, peer.earliestTimeout) >= peer.timeoutMin) {
                hnet_protocol_notify_disconnect(host, peer, pEvent);
                return true;
            }
        }

        if (pCmd->packet != nullptr) {
            peer.reliableDataInTransit -= pCmd->fragmentLength;
        }
        ++peer.packetsLost;
        pCmd->roundTripTimeout *= 2;

        HNetListNode* pNext = pNode->next;
        HNetList::remove(pNode);
        peer.outgoingReliableCommands.push_front(pNode);

        if (!peer.sentReliableCommands.empty() && (pNext == peer.sentReliableCommands.begin())) {
            pCmd = reinterpret_cast<HNetOutgoingCommand*>(pNext);
            peer.nextTimeout = pCmd->sentTime + pCmd->roundTripTimeout;
        }

        pNode = pNext;
    }

    return false;
}

static void hnet_protocol_send_acks(HNetHost& host, HNetPeer& peer)
{
    for (HNetListNode* pNode = peer.acks.begin(); pNode != peer.acks.end();) {
        if (host.commandCount >= HNET_PROTOCOL_MAX_PACKET_COMMANDS ||
            host.bufferCount >= HNET_BUFFER_MAX ||
            (peer.mtu - host.packetSize) < sizeof(HNetProtocolAck)) {
            host.continueSending = true;
            return;
        }

        HNetProtocol& cmd = host.commands[host.commandCount++];
        HNetBuffer& buffer = host.buffers[host.bufferCount++];
        buffer.data = &cmd;
        buffer.dataLength = sizeof(HNetProtocolAck);
        host.packetSize += buffer.dataLength;

        HNetAck* pAck = reinterpret_cast<HNetAck*>(pNode);
        cmd.header.command = HNET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
        cmd.header.channelId = pAck->command.header.channelId;
        cmd.header.reliableSeqNumber = HNET_HOST_TO_NET_16(pAck->command.header.reliableSeqNumber);
        cmd.ack.recvReliableSeqNumber = HNET_HOST_TO_NET_16(pAck->command.header.reliableSeqNumber);
        cmd.ack.recvSentTime = HNET_HOST_TO_NET_16(pAck->sentTime);

        if ((pAck->command.header.command & HNET_PROTOCOL_COMMAND_MASK) == HNET_PROTOCOL_COMMAND_DISCONNECT) {
            hnet_protocol_dispatch_state(host, peer, HNetPeerState::Zombie);
        }

        pNode = pNode->next;
        HNetList::remove(reinterpret_cast<HNetListNode*>(pAck));
        hnet_free(pAck);
    }
}

static void hnet_protocol_ping(HNetPeer& peer)
{
    if (peer.state != HNetPeerState::Connected) {
        return;
    }

    HNetProtocol cmd;
    cmd.header.command = HNET_PROTOCOL_COMMAND_PING | HNET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
    cmd.header.channelId = 0xFF;
    hnet_peer_queue_outgoing_command(peer, cmd, nullptr, 0, 0);
}

size_t hnet_protocol_command_size(uint8_t command)
{
    return commandSizes[command & HNET_PROTOCOL_COMMAND_MASK];
}

void hnet_protocol_init_connect_command(const HNetHost& host, const HNetPeer& peer, uint32_t data, HNetProtocol& cmd)
{
    cmd.header.command = HNET_PROTOCOL_COMMAND_CONNECT | HNET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
    cmd.header.channelId = 0xFF;
    cmd.connect.outgoingPeerId = HNET_HOST_TO_NET_16(peer.incomingPeerId);
    cmd.connect.incomingSessionId = peer.incomingSessionId;
    cmd.connect.outgoingSessionId = peer.outgoingSessionId;
    cmd.connect.mtu = HNET_HOST_TO_NET_32(peer.mtu);
    cmd.connect.windowSize = HNET_HOST_TO_NET_32(peer.windowSize);
    cmd.connect.channelCount = HNET_HOST_TO_NET_32(peer.channelCount);
    cmd.connect.incomingBandwidth = HNET_HOST_TO_NET_32(host.incomingBandwidth);
    cmd.connect.outgoingSessionId = HNET_HOST_TO_NET_32(host.outgoingBandwidth);
    cmd.connect.packetThrottleInterval = HNET_HOST_TO_NET_32(peer.packetThrottleInterval);
    cmd.connect.packetThrottleAcceleration = HNET_HOST_TO_NET_32(peer.packetThrottleAcceleration);
    cmd.connect.packetThrottleDeceleration = HNET_HOST_TO_NET_32(peer.packetThrottleDeceleration);
    cmd.connect.connectId = peer.connectId;
    cmd.connect.data = HNET_HOST_TO_NET_32(data);
}

int32_t hnet_protocol_send_outgoing_commands(HNetHost& host, HNetEvent* pEvent, bool checkFormTimeouts)
{
    // @TODO: checksum
    // @TODO: compress
    host.continueSending = true;

    while (host.continueSending) {
        host.continueSending = false;
        for (size_t i = 0; i < host.peerCount; i++) {
            HNetPeer& peer = host.peers[i];
            if (peer.state == HNetPeerState::Disconnected || peer.state == HNetPeerState::Zombie)  {
                continue;
            }

            host.headerFlags = 0;
            host.commandCount = 0;
            host.bufferCount = 1;
            host.packetSize = sizeof(HNetProtocolHeader);

            hnet_protocol_send_acks(host, peer);

            if (checkFormTimeouts) {
                if (HNET_TIME_GE(host.serviceTime, peer.nextTimeout) && hnet_protocol_check_timeouts(host, peer, pEvent)) {
                    if (pEvent != nullptr && pEvent->type != HNetEventType::None) {
                        return 1;
                    }
                    continue;
                }
            }
        }
    }

    return 0;
}