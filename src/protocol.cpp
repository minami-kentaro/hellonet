#include <algorithm>
#include <cmath>
#include "allocator.h"
#include "event.h"
#include "hnet_time.h"
#include "host.h"
#include "packet.h"
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

static void hnet_protocol_notify_connect(HNetHost& host, HNetPeer& peer, HNetEvent& event)
{
    host.recalculateBandwidthLimits = true;
    hnet_protocol_change_state(peer, HNetPeerState::Connected);
    event.type = HNetEventType::Connect;
    event.peer = &peer;
    event.data = peer.eventData;
}

static void hnet_protocol_notify_disconnect(HNetHost& host, HNetPeer& peer, HNetEvent* pEvent)
{
    if (static_cast<uint8_t>(peer.state) >= static_cast<uint8_t>(HNetPeerState::ConnectionPending)) {
        host.recalculateBandwidthLimits = true;
    }
    if (peer.state == HNetPeerState::Disconnected || peer.state == HNetPeerState::AckConnect || peer.state == HNetPeerState::ConnectionPending) {
        hnet_peer_reset(peer);
    } else if (pEvent != nullptr) {
        pEvent->type = HNetEventType::Disconnect;
        pEvent->peer = &peer;
        pEvent->data = 0;
        hnet_peer_reset(peer);
    } else {
        peer.eventData = 0;
        hnet_protocol_dispatch_state(host, peer, HNetPeerState::Zombie);
    }
}

static bool hnet_protocol_check_timeouts(HNetHost& host, HNetPeer& peer, HNetEvent* pEvent)
{
    for (HNetListNode* pNode = peer.sentReliableCommands.begin(); pNode != peer.sentReliableCommands.end();) {
        HNetOutgoingCommand& cmd = *reinterpret_cast<HNetOutgoingCommand*>(pNode);
        if (HNET_TIME_DIFF(host.serviceTime, cmd.sentTime) < cmd.roundTripTimeout) {
            continue;
        }

        if (peer.earliestTimeout == 0 || HNET_TIME_LT(cmd.sentTime, peer.earliestTimeout)) {
            peer.earliestTimeout = cmd.sentTime;
        }

        if (peer.earliestTimeout != 0) {
            if (HNET_TIME_DIFF(host.serviceTime, peer.earliestTimeout) >= peer.timeoutMax) {
                hnet_protocol_notify_disconnect(host, peer, pEvent);
                return true;
            } else if (cmd.roundTripTimeout >= cmd.roundTripTimeoutLimit &&
                       HNET_TIME_DIFF(host.serviceTime, peer.earliestTimeout) >= peer.timeoutMin) {
                hnet_protocol_notify_disconnect(host, peer, pEvent);
                return true;
            }
        }

        if (cmd.packet != nullptr) {
            peer.reliableDataInTransit -= cmd.fragmentLength;
        }
        ++peer.packetsLost;
        cmd.roundTripTimeout *= 2;

        HNetListNode* pNext = pNode->next;
        HNetList::remove(pNode);
        peer.outgoingReliableCommands.push_front(pNode);

        if (!peer.sentReliableCommands.empty() && (pNext == peer.sentReliableCommands.begin())) {
            HNetOutgoingCommand& nextCmd = *reinterpret_cast<HNetOutgoingCommand*>(pNext);
            peer.nextTimeout = nextCmd.sentTime + nextCmd.roundTripTimeout;
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

static bool hnet_protocol_send_reliable_outgoing_commands(HNetHost& host, HNetPeer& peer)
{
    bool canPing = true;

    for (HNetListNode* pNode = peer.outgoingReliableCommands.begin(); pNode != peer.outgoingReliableCommands.end();) {
    }

    return canPing;
}

static HNetProtocolCommand hnet_protocol_remove_sent_reliable_command(HNetPeer& peer, uint16_t reliableSeqNumber, uint8_t channelId)
{
    HNetOutgoingCommand* pOutgoingCmd = nullptr;
    bool wasSent = true;

    for (HNetListNode* pNode = peer.sentReliableCommands.begin(); pNode != peer.sentReliableCommands.end(); pNode = pNode->next) {
        HNetOutgoingCommand* pCmd = reinterpret_cast<HNetOutgoingCommand*>(pNode);
        if (pCmd->reliableSeqNumber == reliableSeqNumber && pCmd->command.header.channelId == channelId) {
            pOutgoingCmd = pCmd;
            break;
        }
    }

    if (pOutgoingCmd == nullptr) {
        for (HNetListNode* pNode = peer.outgoingReliableCommands.begin(); pNode != peer.outgoingReliableCommands.end(); pNode = pNode->next) {
            HNetOutgoingCommand* pCmd = reinterpret_cast<HNetOutgoingCommand*>(pNode);
            if (pCmd->sendAttempts == 0) {
                return HNET_PROTOCOL_COMMAND_NONE;
            }

            if (pCmd->reliableSeqNumber == reliableSeqNumber && pCmd->command.header.channelId == channelId) {
                pOutgoingCmd = pCmd;
                break;
            }
        }

        if (pOutgoingCmd == nullptr) {
            return HNET_PROTOCOL_COMMAND_NONE;
        }

        wasSent = false;
    }

    if (channelId < peer.channelCount) {
        HNetChannel& channel = peer.channels[channelId];
        uint16_t reliableWindow = reliableSeqNumber / HNET_PEER_RELIABLE_WINDOW_SIZE;
        uint16_t& window = channel.reliableWindows[reliableWindow];
        if (window > 0) {
            if (--window == 0) {
                channel.usedReliableWindows &= ~(1 << reliableWindow);
            }
        }
    }

    HNetProtocolCommand cmdNumber = static_cast<HNetProtocolCommand>(pOutgoingCmd->command.header.command & HNET_PROTOCOL_COMMAND_MASK);
    HNetList::remove(&pOutgoingCmd->outgoingCommandList);

    if (pOutgoingCmd->packet != nullptr) {
        if (wasSent) {
            peer.reliableDataInTransit -= pOutgoingCmd->fragmentLength;
        }
        size_t refCount = --pOutgoingCmd->packet->refCount;
        if (refCount == 0) {
            pOutgoingCmd->packet->flags |= HNET_PACKET_FLAG_SENT;
            hnet_packet_destroy(pOutgoingCmd->packet);
        }
    }

    hnet_free(pOutgoingCmd);

    if (!peer.sentReliableCommands.empty()) {
        pOutgoingCmd = reinterpret_cast<HNetOutgoingCommand*>(peer.sentReliableCommands.front());
        peer.nextTimeout = pOutgoingCmd->sentTime + pOutgoingCmd->roundTripTimeout;
    }

    return cmdNumber;
}

static bool hnet_protocol_handle_ack(HNetHost& host, HNetEvent& event, HNetPeer& peer, const HNetProtocol& cmd)
{
    if (peer.state == HNetPeerState::Disconnected || peer.state == HNetPeerState::Zombie) {
        return true;
    }

    uint32_t recvSentTime = HNET_NET_TO_HOST_16(cmd.ack.recvSentTime);
    recvSentTime |= (host.serviceTime & 0xFFFF0000);
    if ((recvSentTime & 0x8000) > (host.serviceTime & 0x8000)) {
        recvSentTime -= 0x10000;
    }

    if (HNET_TIME_LT(host.serviceTime, recvSentTime)) {
        return true;
    }

    peer.lastRecvTime = host.serviceTime;
    peer.earliestTimeout = 0;

    uint32_t roundTripTime = HNET_TIME_DIFF(host.serviceTime, recvSentTime);
    hnet_peer_throttle(peer, roundTripTime);
    peer.roundTripTimeVariance -= peer.roundTripTimeVariance / 4;

    if (roundTripTime >= peer.roundTripTime) {
        peer.roundTripTime += (roundTripTime - peer.roundTripTime) / 8;
        peer.roundTripTimeVariance += (roundTripTime - peer.roundTripTime) / 4;
    } else {
        peer.roundTripTime -= (peer.roundTripTime - roundTripTime) / 8;
        peer.roundTripTimeVariance += (peer.roundTripTime - roundTripTime) / 4;
    }
    if (peer.roundTripTime < peer.lowestRoundTripTime) {
        peer.lowestRoundTripTime = peer.roundTripTime;
    }
    if (peer.roundTripTimeVariance > peer.highestRoundTripTimeVariance) {
        peer.highestRoundTripTimeVariance = peer.roundTripTimeVariance;
    }

    if (peer.packetThrottleEpoch == 0 || HNET_TIME_DIFF(host.serviceTime, peer.packetThrottleEpoch) >= peer.packetThrottleInterval) {
        peer.lastRoundTripTime = peer.lowestRoundTripTime;
        peer.lastRoundTripTimeVariance = peer.highestRoundTripTimeVariance;
        peer.lowestRoundTripTime = peer.roundTripTime;
        peer.highestRoundTripTimeVariance = peer.roundTripTimeVariance;
        peer.packetThrottleEpoch = host.serviceTime;
    }

    uint16_t recvReliableSeqNumber = HNET_NET_TO_HOST_16(cmd.ack.recvReliableSeqNumber);
    HNetProtocolCommand cmdNumber = hnet_protocol_remove_sent_reliable_command(peer, recvReliableSeqNumber, cmd.header.channelId);

    switch (peer.state) {
    case HNetPeerState::AckConnect:
        if (cmdNumber != HNET_PROTOCOL_COMMAND_VERIFY_CONNECT) {
            return false;
        }
        hnet_protocol_notify_connect(host, peer, event);
        break;

    case HNetPeerState::Disconnecting:
        if (cmdNumber != HNET_PROTOCOL_COMMAND_DISCONNECT) {
            return false;
        }
        hnet_protocol_notify_disconnect(host, peer, &event);
        break;

    case HNetPeerState::DisconnectLater:
        if (peer.outgoingReliableCommands.empty() && peer.outgoingUnreliableCommands.empty() && peer.sentReliableCommands.empty()) {
            hnet_peer_disconnect(peer, peer.eventData);
        }
        break;

    default:
        break;
    }

    return true;
}

static bool hnet_protocol_handle_connect(HNetHost& host, HNetPeer*& pPeer, const HNetProtocol& cmd)
{
    pPeer = nullptr;
    size_t channelCount = HNET_NET_TO_HOST_32(cmd.connect.channelCount);
    if (channelCount < HNET_PROTOCOL_MIN_CHANNEL_COUNT || HNET_PROTOCOL_MAX_CHANNEL_COUNT < channelCount) {
        return false;
    }

    size_t duplicatePeers = 0;
    for (size_t i = 0; i < host.peerCount; i++) {
        HNetPeer& peer = host.peers[i];
        if (peer.state == HNetPeerState::Disconnected) {
            if (pPeer == nullptr) {
                pPeer = &peer;
            }
        } else if (peer.state != HNetPeerState::Connecting && peer.addr.host == host.recvAddr.host) {
            if (peer.addr.port == host.recvAddr.port && peer.connectId == cmd.connect.connectId) {
                return false;
            }
            ++duplicatePeers;
        }
    }

    if (pPeer == nullptr || duplicatePeers >= host.duplicatePeers) {
        return false;
    }

    if (channelCount > host.channelLimit) {
        channelCount = host.channelLimit;
    }

    HNetChannel* pChannels = static_cast<HNetChannel*>(hnet_malloc(channelCount * sizeof(HNetChannel)));
    if (pChannels == nullptr) {
        return false;
    }
    HNetPeer& peer = *pPeer;
    peer.channels = pChannels;
    peer.channelCount = channelCount;
    peer.state = HNetPeerState::AckConnect;
    peer.connectId = cmd.connect.connectId;
    peer.addr = host.recvAddr;
    peer.outgoingPeerId = HNET_NET_TO_HOST_16(cmd.connect.outgoingPeerId);
    peer.incomingBandwidth = HNET_NET_TO_HOST_32(cmd.connect.incomingBandwidth);
    peer.outgoingBandwidth = HNET_NET_TO_HOST_32(cmd.connect.outgoingBandwidth);
    peer.packetThrottleInterval = HNET_NET_TO_HOST_32(cmd.connect.packetThrottleInterval);
    peer.packetThrottleAcceleration = HNET_NET_TO_HOST_32(cmd.connect.packetThrottleAcceleration);
    peer.packetThrottleDeceleration = HNET_NET_TO_HOST_32(cmd.connect.packetThrottleDeceleration);
    peer.eventData = HNET_NET_TO_HOST_32(cmd.connect.data);

    uint8_t inSessionId = cmd.connect.incomingSessionId == 0xFF ? peer.outgoingSessionId : cmd.connect.incomingSessionId;
    inSessionId = (inSessionId + 1) & (HNET_PROTOCOL_HEADER_SESSION_MASK >> HNET_PROTOCOL_HEADER_SESSION_SHIFT);
    if (inSessionId == peer.outgoingSessionId) {
        inSessionId = (inSessionId + 1) & (HNET_PROTOCOL_HEADER_SESSION_MASK >> HNET_PROTOCOL_HEADER_SESSION_SHIFT);
    }
    peer.outgoingSessionId = inSessionId;

    uint8_t outSessionId = cmd.connect.outgoingSessionId == 0xFF ? peer.incomingSessionId : cmd.connect.outgoingSessionId;
    outSessionId = (outSessionId + 1) & (HNET_PROTOCOL_HEADER_SESSION_MASK >> HNET_PROTOCOL_HEADER_SESSION_SHIFT);
    if (outSessionId == peer.incomingSessionId) {
        outSessionId = (outSessionId + 1) & (HNET_PROTOCOL_HEADER_SESSION_MASK >> HNET_PROTOCOL_HEADER_SESSION_SHIFT);
    }
    peer.incomingSessionId = outSessionId;

    for (size_t i = 0; i < channelCount; i++) {
        HNetChannel& channel = peer.channels[i];
        channel.outgoingReliableSeqNumber = 0;
        channel.outgoingUnreliableSeqNumber = 0;
        channel.incomingReliableSeqNumber = 0;
        channel.incomingUnreliableSeqNumber = 0;
        channel.incomingReliableCommands.clear();
        channel.incomingUnreliableCommands.clear();
        channel.usedReliableWindows = 0;
        memset(channel.reliableWindows, 0, sizeof(channel.reliableWindows));
    }

    peer.mtu = std::clamp<uint32_t>(HNET_NET_TO_HOST_32(cmd.connect.mtu), HNET_PROTOCOL_MIN_MTU, HNET_PROTOCOL_MAX_MTU);

    if (host.outgoingBandwidth == 0 && peer.incomingBandwidth == 0) {
        peer.windowSize = HNET_PROTOCOL_MAX_WINDOW_SIZE;
    } else if (host.outgoingBandwidth == 0 || peer.incomingBandwidth == 0) {
        peer.windowSize = std::max(host.outgoingBandwidth, peer.incomingBandwidth) / HNET_PEER_WINDOW_SIZE_SCALE * HNET_PROTOCOL_MIN_WINDOW_SIZE;
    } else {
        peer.windowSize = std::min(host.outgoingBandwidth, peer.incomingBandwidth) / HNET_PEER_WINDOW_SIZE_SCALE * HNET_PROTOCOL_MIN_WINDOW_SIZE;
    }
    peer.windowSize = std::clamp<uint32_t>(peer.windowSize, HNET_PROTOCOL_MIN_WINDOW_SIZE, HNET_PROTOCOL_MAX_WINDOW_SIZE);

    uint32_t windowSize = HNET_PROTOCOL_MAX_WINDOW_SIZE;
    if (host.incomingBandwidth > 0) {
        windowSize = host.incomingBandwidth / HNET_PEER_WINDOW_SIZE_SCALE * HNET_PROTOCOL_MIN_WINDOW_SIZE;
    }
    if (windowSize > HNET_NET_TO_HOST_32(cmd.connect.windowSize)) {
        windowSize = HNET_NET_TO_HOST_32(cmd.connect.windowSize);
    }
    windowSize = std::clamp<uint32_t>(windowSize, HNET_PROTOCOL_MIN_WINDOW_SIZE, HNET_PROTOCOL_MAX_WINDOW_SIZE);

    HNetProtocol verifyCmd;
    verifyCmd.header.command = HNET_PROTOCOL_COMMAND_VERIFY_CONNECT | HNET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
    verifyCmd.header.channelId = 0xFF;
    verifyCmd.verifyConenct.outgoingPeerId = HNET_HOST_TO_NET_16(peer.incomingPeerId);
    verifyCmd.verifyConenct.incomingSessionId = inSessionId;
    verifyCmd.verifyConenct.outgoingSessionId = outSessionId;
    verifyCmd.verifyConenct.mtu = HNET_HOST_TO_NET_32(peer.mtu);
    verifyCmd.verifyConenct.windowSize = HNET_HOST_TO_NET_32(windowSize);
    verifyCmd.verifyConenct.channelCount = HNET_HOST_TO_NET_32(channelCount);
    verifyCmd.verifyConenct.incomingBandwidth = HNET_HOST_TO_NET_32(host.incomingBandwidth);
    verifyCmd.verifyConenct.outgoingBandwidth = HNET_HOST_TO_NET_32(host.outgoingBandwidth);
    verifyCmd.verifyConenct.packetThrottleInterval = HNET_HOST_TO_NET_32(peer.packetThrottleInterval);
    verifyCmd.verifyConenct.packetThrottleAcceleration = HNET_HOST_TO_NET_32(peer.packetThrottleAcceleration);
    verifyCmd.verifyConenct.packetThrottleDeceleration = HNET_HOST_TO_NET_32(peer.packetThrottleDeceleration);
    verifyCmd.verifyConenct.connectId = peer.connectId;

    hnet_peer_queue_outgoing_command(peer, verifyCmd, nullptr,0, 0);
    return true;
}

static bool hnet_protocol_handle_verify_connect(HNetHost& host, HNetEvent& event, HNetPeer& peer, const HNetProtocol& cmd)
{
    if (peer.state != HNetPeerState::Connecting) {
        return true;
    }

    size_t channelCount = HNET_NET_TO_HOST_32(cmd.verifyConenct.channelCount);
    if (channelCount < HNET_PROTOCOL_MIN_CHANNEL_COUNT || HNET_PROTOCOL_MAX_CHANNEL_COUNT < channelCount ||
        HNET_NET_TO_HOST_32(cmd.verifyConenct.packetThrottleInterval) != peer.packetThrottleInterval ||
        HNET_NET_TO_HOST_32(cmd.verifyConenct.packetThrottleAcceleration) != peer.packetThrottleAcceleration ||
        HNET_NET_TO_HOST_32(cmd.verifyConenct.packetThrottleDeceleration) != peer.packetThrottleDeceleration ||
        cmd.verifyConenct.connectId != peer.connectId) {
        peer.eventData = 0;
        hnet_protocol_dispatch_state(host, peer, HNetPeerState::Zombie);
        return false;
    }

    hnet_protocol_remove_sent_reliable_command(peer, 1, 0xFF);

    if (channelCount < peer.channelCount) {
        peer.channelCount = channelCount;
    }

    peer.outgoingPeerId = HNET_NET_TO_HOST_16(cmd.verifyConenct.outgoingPeerId);
    peer.incomingSessionId = cmd.verifyConenct.incomingSessionId;
    peer.outgoingSessionId = cmd.verifyConenct.outgoingSessionId;

    uint32_t mtu = std::clamp<uint32_t>(HNET_NET_TO_HOST_32(cmd.verifyConenct.mtu), HNET_PROTOCOL_MIN_MTU, HNET_PROTOCOL_MAX_MTU);
    if (mtu < peer.mtu) {
        peer.mtu = mtu;
    }

    uint32_t windowSize = std::clamp<uint32_t>(HNET_NET_TO_HOST_32(cmd.verifyConenct.windowSize), HNET_PROTOCOL_MIN_WINDOW_SIZE, HNET_PROTOCOL_MAX_WINDOW_SIZE);
    if (windowSize < peer.windowSize) {
        peer.windowSize = windowSize;
    }

    peer.incomingBandwidth = HNET_NET_TO_HOST_32(cmd.verifyConenct.incomingBandwidth);
    peer.outgoingBandwidth = HNET_NET_TO_HOST_32(cmd.verifyConenct.outgoingBandwidth);
    hnet_protocol_notify_connect(host, peer, event);
    return true;
}

static bool hnet_protocol_handle_disconnect(HNetHost& host, HNetPeer& peer, const HNetProtocol& cmd)
{
    if (peer.state == HNetPeerState::Disconnected || peer.state == HNetPeerState::Zombie || peer.state == HNetPeerState::AckDisconnet) {
        return true;
    }

    hnet_peer_reset_queues(peer);

    if (peer.state == HNetPeerState::ConnectionSucceeded || peer.state == HNetPeerState::Disconnecting || peer.state == HNetPeerState::Connecting) {
        hnet_protocol_dispatch_state(host, peer, HNetPeerState::Zombie);
    } else if (peer.state != HNetPeerState::Connected && peer.state != HNetPeerState::DisconnectLater) {
        if (peer.state == HNetPeerState::ConnectionPending) {
            host.recalculateBandwidthLimits = true;
        }
        hnet_peer_reset(peer);
    } else if (cmd.header.command & HNET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE) {
        hnet_protocol_change_state(peer, HNetPeerState::AckDisconnet);
    } else {
        hnet_protocol_dispatch_state(host, peer, HNetPeerState::Zombie);
    }

    peer.eventData = HNET_NET_TO_HOST_32(cmd.disconnect.data);
    return true;
}

static bool hnet_protocol_handle_ping(HNetPeer& peer)
{
    return peer.state == HNetPeerState::Connected || peer.state == HNetPeerState::DisconnectLater;
}

static bool hnet_protocol_handle_send_reliable(HNetHost& host, HNetPeer& peer, const HNetProtocol& cmd, uint8_t*& pData)
{
    if (pData == nullptr || cmd.header.channelId >= peer.channelCount || (peer.state != HNetPeerState::Connected && peer.state != HNetPeerState::DisconnectLater)) {
        return false;
    }

    uint16_t dataLength = HNET_NET_TO_HOST_16(cmd.sendReliable.dataLength);
    pData += dataLength;
    if (dataLength > host.maxPacketSize || pData < host.recvData || &host.recvData[host.recvDataLength] < pData) {
        return false;
    }

    const uint8_t* pPacketData = reinterpret_cast<const uint8_t*>(pData) + sizeof(HNetProtocolSendReliable);
    if (!hnet_peer_queue_incoming_command(peer, cmd, pPacketData, dataLength, HNET_PACKET_FLAG_RELIABLE, 0)) {
        return false;
    }

    return true;
}

static bool hnet_protocol_handle_send_unreliable(HNetHost& host, HNetPeer& peer, const HNetProtocol& cmd, uint8_t*& pData)
{
    if (pData == nullptr || cmd.header.channelId >= peer.channelCount || (peer.state != HNetPeerState::Connected && peer.state != HNetPeerState::DisconnectLater)) {
        return false;
    }

    uint16_t dataLength = HNET_NET_TO_HOST_16(cmd.sendUnreliable.dataLength);
    pData += dataLength;
    if (dataLength > host.maxPacketSize || pData < host.recvData || &host.recvData[host.recvDataLength] < pData) {
        return false;
    }

    const uint8_t* pPacketData = reinterpret_cast<const uint8_t*>(pData) + sizeof(HNetProtocolSendUnreliable);
    if (!hnet_peer_queue_incoming_command(peer, cmd, pPacketData, dataLength, 0, 0)) {
        return false;
    }

    return true;
}

bool hnet_protocol_handle_send_unsequenced(HNetHost& host, HNetPeer& peer, const HNetProtocol& cmd, uint8_t*& pData)
{
    return false;
}

bool hnet_protocol_handle_send_fragment(HNetHost& host, HNetPeer& peer, const HNetProtocol& cmd, uint8_t*& pData)
{
    return false;
}

bool hnet_protocol_handle_bandwidth_limit(HNetHost& host, HNetPeer& peer, const HNetProtocol& cmd)
{
    return false;
}

bool hnet_protocol_handle_throttle_configure(HNetHost& host, HNetPeer& peer, const HNetProtocol& cmd)
{
    return false;
}

bool hnet_protocol_handle_send_unreliable_fragment(HNetHost& host, HNetPeer& peer, const HNetProtocol& cmd)
{
    return false;
}

static bool hnet_protocol_handle_command(HNetHost& host, HNetEvent& event, HNetPeer*& pPeer, const HNetProtocol& cmd, uint8_t*& pData)
{
    uint8_t cmdNumber = cmd.header.command & HNET_PROTOCOL_COMMAND_MASK;
    switch (cmdNumber) {
    case HNET_PROTOCOL_COMMAND_ACKNOWLEDGE:
        return hnet_protocol_handle_ack(host, event, *pPeer, cmd);

    case HNET_PROTOCOL_COMMAND_CONNECT:
        return hnet_protocol_handle_connect(host, pPeer, cmd);

    case HNET_PROTOCOL_COMMAND_VERIFY_CONNECT:
        return hnet_protocol_handle_verify_connect(host, event, *pPeer, cmd);

    case HNET_PROTOCOL_COMMAND_DISCONNECT:
        return hnet_protocol_handle_disconnect(host, *pPeer, cmd);

    case HNET_PROTOCOL_COMMAND_PING:
        return hnet_protocol_handle_ping(*pPeer);

    case HNET_PROTOCOL_COMMAND_SEND_RELIABLE:
        return hnet_protocol_handle_send_reliable(host, *pPeer, cmd, pData);

    case HNET_PROTOCOL_COMMAND_SEND_UNRELIABLE:
        return hnet_protocol_handle_send_unreliable(host, *pPeer, cmd, pData);

    case HNET_PROTOCOL_COMMAND_SEND_UNSEQUENCED:
        return hnet_protocol_handle_send_unsequenced(host, *pPeer, cmd, pData);

    case HNET_PROTOCOL_COMMAND_SEND_FRAGMENT:
        return hnet_protocol_handle_send_fragment(host, *pPeer, cmd, pData);

    case HNET_PROTOCOL_COMMAND_BANDWIDTH_LIMIT:
        return hnet_protocol_handle_bandwidth_limit(host, *pPeer, cmd);

    case HNET_PROTOCOL_COMMAND_THROTTLE_CONFIGURE:
        return hnet_protocol_handle_throttle_configure(host, *pPeer, cmd);

    case HNET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT:
        return hnet_protocol_handle_send_unreliable_fragment(host, *pPeer, cmd);

    default:
        return false;
    }
}

static int hnet_protocol_handle_incoming_commands(HNetHost& host, HNetEvent& event)
{
    // @TODO: compress
    // @TODO: checksum
    if (host.recvDataLength < offsetof(HNetProtocolHeader, sentTime)) {
        return 0;
    }

    HNetProtocolHeader* pHeader = reinterpret_cast<HNetProtocolHeader*>(host.recvData);
    uint16_t peerId = HNET_NET_TO_HOST_16(pHeader->peerId);
    uint8_t sessionId = (peerId & HNET_PROTOCOL_HEADER_SESSION_MASK) >> HNET_PROTOCOL_HEADER_SESSION_SHIFT;
    uint16_t flags = peerId & HNET_PROTOCOL_HEADER_FLAG_MASK;
    peerId &= ~(HNET_PROTOCOL_HEADER_FLAG_MASK | HNET_PROTOCOL_HEADER_SESSION_MASK);

    size_t headerSize = (flags & HNET_PROTOCOL_HEADER_FLAG_SENT_TIME) ? sizeof(HNetProtocolHeader) : offsetof(HNetProtocolHeader, sentTime);

    HNetPeer* pPeer = nullptr;
    if (peerId >= host.peerCount) {
        return 0;
    } else if (peerId < HNET_PROTOCOL_MAX_PEER_ID) {
        HNetPeer& peer = host.peers[peerId];
        if (peer.state == HNetPeerState::Disconnected ||
            peer.state == HNetPeerState::Zombie ||
            ((host.recvAddr != peer.addr) && (peer.addr.host != HNET_HOST_BROADCAST)) ||
            ((peer.outgoingPeerId < HNET_PROTOCOL_MAX_PEER_ID) && (sessionId != peer.incomingSessionId))) {
            return 0;
        }
        pPeer = &peer;
    }

    if (pPeer != nullptr) {
        pPeer->addr.host = host.recvAddr.host;
        pPeer->addr.port = host.recvAddr.port;
        pPeer->incomingDataTotal += host.recvDataLength;
    }

    uint8_t* pData = host.recvData + headerSize;
    uint8_t* pDataEnd = &host.recvData[host.recvDataLength];
    while (pData < pDataEnd) {
        HNetProtocol& cmd = *reinterpret_cast<HNetProtocol*>(pData);
        if (pData + sizeof(HNetProtocolCommandHeader) > pDataEnd) {
            break;
        }

        uint8_t cmdNumber = cmd.header.command & HNET_PROTOCOL_COMMAND_MASK;
        if (cmdNumber >= HNET_PROTOCOL_COMMAND_COUNT) {
            break;
        }

        size_t cmdSize = commandSizes[cmdNumber];
        if (cmdSize == 0 || pData + cmdSize > pDataEnd) {
            break;
        }

        pData += cmdSize;

        if (pPeer == nullptr && cmdNumber != HNET_PROTOCOL_COMMAND_CONNECT) {
            break;
        }

        cmd.header.reliableSeqNumber = HNET_NET_TO_HOST_16(cmd.header.reliableSeqNumber);

        if (!hnet_protocol_handle_command(host, event, pPeer, cmd, pData)) {
            goto exit;
        }
        if (pPeer == nullptr) {
            goto exit;
        }
        if (cmd.header.command & HNET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE) {
            if (!(flags & HNET_PROTOCOL_HEADER_FLAG_SENT_TIME)) {
                break;
            }

            uint16_t sentTime = HNET_NET_TO_HOST_16(pHeader->sentTime);
            switch (pPeer->state) {
            case HNetPeerState::Disconnecting:
            case HNetPeerState::AckConnect:
            case HNetPeerState::Disconnected:
            case HNetPeerState::Zombie:
                break;
            case HNetPeerState::AckDisconnet:
                if ((cmd.header.command & HNET_PROTOCOL_COMMAND_MASK) == HNET_PROTOCOL_COMMAND_DISCONNECT) {
                    hnet_peer_queue_ack(*pPeer, cmd, sentTime);
                }
                break;
            default:
                hnet_peer_queue_ack(*pPeer, cmd, sentTime);
                break;
            }
        }
    }

exit:
    return (event.type != HNetEventType::None) ? 1 : 0;
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

int32_t hnet_protocol_dispatch_incoming_commands(HNetHost& host, HNetEvent& event)
{
    while (!host.dispatchQueue.empty()) {
        HNetListNode* pNode = HNetList::remove(host.dispatchQueue.begin());
        HNetPeer& peer = *reinterpret_cast<HNetPeer*>(pNode);
        peer.needsDispatch = false;

        switch (peer.state) {
        case HNetPeerState::ConnectionPending:
        case HNetPeerState::ConnectionSucceeded:
            hnet_protocol_change_state(peer, HNetPeerState::Connected);
            event.type = HNetEventType::Connect;
            event.peer = &peer;
            event.data = peer.eventData;
            return 1;

        case HNetPeerState::Zombie:
            host.recalculateBandwidthLimits = true;
            event.type = HNetEventType::Disconnect;
            event.peer = &peer;
            event.data = peer.eventData;
            hnet_peer_reset(peer);
            return 1;

        case HNetPeerState::Connected:
            if (peer.dispatchedCommands.empty()) {
                continue;
            }
            event.packet = hnet_peer_recv(peer, event.channelId);
            if (event.packet == nullptr) {
                continue;
            }
            event.type = HNetEventType::Receive;
            event.peer = &peer;
            if (!peer.dispatchedCommands.empty()) {
                peer.needsDispatch = true;
                host.dispatchQueue.push_back(&peer.dispatchList);
            }
            return 1;

        default:
            break;
        }
    }

    return 0;
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

int32_t hnet_protocol_recv_incoming_commands(HNetHost& host, HNetEvent& event)
{
    for (uint32_t i = 0; i < 256; i++) {
        HNetBuffer buffer{};
        buffer.data = host.packetData[0];
        buffer.dataLength = sizeof(host.packetData[0]);

        int32_t recvLength = hnet_socket_recv(host.socket, host.recvAddr, &buffer, 1);
        if (recvLength <= 0) {
            return recvLength;
        }

        host.recvData = host.packetData[0];
        host.recvDataLength = recvLength;
        host.totalRecvData += recvLength;
        host.totalRecvPackets++;

        int32_t ret = hnet_protocol_handle_incoming_commands(host, event);
        if (ret != 0) {
            return ret;
        }
    }

    return -1;
}
