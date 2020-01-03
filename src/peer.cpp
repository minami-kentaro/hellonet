#include "allocator.h"
#include "hnet_utility.h"
#include "host.h"
#include "packet.h"
#include "peer.h"
#include "protocol.h"
#include "socket.h"

static void hnet_peer_reset_outgoing_commands(HNetList& queue)
{
    while (!queue.empty()) {
        HNetListNode* pNode = queue.remove(queue.front());
        HNetOutgoingCommand& cmd = *reinterpret_cast<HNetOutgoingCommand*>(pNode);
        if (cmd.packet != nullptr) {
            size_t refCount = --cmd.packet->refCount;
            if (refCount == 0) {
                hnet_packet_destroy(cmd.packet);
            }
        }
        hnet_free(&cmd);
    }
}

static void hnet_peer_remove_incoming_commands(HNetList& queue, HNetListNode* pStart, HNetListNode* pEnd)
{
    if (pStart == nullptr || pEnd == nullptr) {
        return;
    }

    for (HNetListNode* pNode = pStart; pNode != pEnd; ) {
        HNetIncomingCommand& cmd = *reinterpret_cast<HNetIncomingCommand*>(pNode);
        pNode = pNode->next;
        HNetList::remove(&cmd.incomingCommandList);
        if (cmd.packet != nullptr) {
            size_t refCount = --cmd.packet->refCount;
            if (refCount == 0) {
                hnet_packet_destroy(cmd.packet);
            }
        }
        if (cmd.fragments != nullptr) {
            hnet_free(cmd.fragments);
        }
        hnet_free(&cmd);
    }
}

static void hnet_peer_reset_incoming_commands(HNetList& queue)
{
    hnet_peer_remove_incoming_commands(queue, queue.begin(), queue.end());
}

static void hnet_peer_setup_outgoing_command(HNetPeer& peer, HNetOutgoingCommand& cmd)
{
    peer.outgoingDataTotal += hnet_protocol_command_size(cmd.command.header.command);

    if (cmd.command.header.channelId == 0xFF) {
        ++peer.outgoingReliableSeqNumber;
        cmd.reliableSeqNumber = peer.outgoingReliableSeqNumber;
        cmd.unreliableSeqNumber = 0;
    } else if (cmd.command.header.command & HNET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE) {
        HNetChannel& channel = peer.channels[cmd.command.header.channelId];
        ++channel.outgoingReliableSeqNumber;
        channel.outgoingUnreliableSeqNumber = 0;
        cmd.reliableSeqNumber = channel.outgoingReliableSeqNumber;
        cmd.unreliableSeqNumber = 0;
    } else if (cmd.command.header.command & HNET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED) {
        ++peer.outgoingUnseqGroup;
        cmd.reliableSeqNumber = 0;
        cmd.unreliableSeqNumber = 0;
    } else {
        HNetChannel& channel = peer.channels[cmd.command.header.channelId];
        if (cmd.fragmentOffset == 0) {
            ++channel.outgoingUnreliableSeqNumber;
        }
        cmd.reliableSeqNumber = channel.outgoingReliableSeqNumber;
        cmd.unreliableSeqNumber = channel.outgoingUnreliableSeqNumber;
    }

    cmd.sendAttempts = 0;
    cmd.sentTime = 0;
    cmd.roundTripTimeout = 0;
    cmd.roundTripTimeoutLimit = 0;
    cmd.command.header.reliableSeqNumber = HNET_HOST_TO_NET_16(cmd.reliableSeqNumber);

    switch (cmd.command.header.command & HNET_PROTOCOL_COMMAND_MASK)
    {
    case HNET_PROTOCOL_COMMAND_SEND_UNRELIABLE:
        cmd.command.sendUnreliable.unreliableSeqNumber = HNET_HOST_TO_NET_16(cmd.unreliableSeqNumber);
        break;
    case HNET_PROTOCOL_COMMAND_SEND_UNSEQUENCED:
        cmd.command.sendUnsequenced.unseqGroup = HNET_HOST_TO_NET_16(peer.outgoingUnseqGroup);
        break;
    default:
        break;
    }

    if (cmd.command.header.command & HNET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE) {
        peer.outgoingReliableCommands.push_back(reinterpret_cast<HNetListNode*>(&cmd));
    } else {
        peer.outgoingUnreliableCommands.push_back(reinterpret_cast<HNetListNode*>(&cmd));
    }
}

static HNetListNode* hnet_peer_find_incoming_current_command(HNetPeer& peer, const HNetProtocol& cmd)
{
    HNetListNode* pCurrentCmd = nullptr;
    HNetChannel& channel = peer.channels[cmd.header.channelId];
    HNetProtocolCommand type = static_cast<HNetProtocolCommand>(cmd.header.command & HNET_PROTOCOL_COMMAND_MASK);

    switch (type) {
    case HNET_PROTOCOL_COMMAND_SEND_RELIABLE:
    case HNET_PROTOCOL_COMMAND_SEND_FRAGMENT:
        if (cmd.header.reliableSeqNumber == channel.incomingReliableSeqNumber) {
            break;
        }
        for (HNetListNode* pNode = channel.incomingReliableCommands.back(); pNode != channel.incomingReliableCommands.end(); pNode = pNode->prev) {
            HNetIncomingCommand* pCmd = reinterpret_cast<HNetIncomingCommand*>(pNode);
            if (cmd.header.reliableSeqNumber >= channel.incomingReliableSeqNumber) {
                if (pCmd->reliableSeqNumber < channel.incomingReliableSeqNumber) {
                    continue;
                }
            } else if (pCmd->reliableSeqNumber >= channel.incomingReliableSeqNumber) {
                pCurrentCmd = pNode;
                break;
            }

            if (pCmd->reliableSeqNumber < cmd.header.reliableSeqNumber) {
                pCurrentCmd = pNode;
                break;
            } else if (pCmd->reliableSeqNumber == cmd.header.reliableSeqNumber) {
                break;
            }
        }
        break;

    case HNET_PROTOCOL_COMMAND_SEND_UNRELIABLE:
    case HNET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT:
        {
            uint32_t seqNumber = HNET_NET_TO_HOST_16(cmd.sendUnreliable.unreliableSeqNumber);
            if (cmd.header.reliableSeqNumber == channel.incomingReliableSeqNumber && seqNumber <= channel.incomingUnreliableSeqNumber) {
                break;
            }
            for (HNetListNode* pNode = channel.incomingUnreliableCommands.back(); pNode != channel.incomingUnreliableCommands.end(); pNode = pNode->prev) {
                HNetIncomingCommand* pCmd = reinterpret_cast<HNetIncomingCommand*>(pNode);
                if ((cmd.header.command & HNET_PROTOCOL_COMMAND_MASK) == HNET_PROTOCOL_COMMAND_SEND_UNSEQUENCED) {
                    continue;
                }
                if (cmd.header.reliableSeqNumber >= channel.incomingReliableSeqNumber) {
                    if (pCmd->reliableSeqNumber < channel.incomingReliableSeqNumber) {
                        continue;
                    }
                } else if (pCmd->reliableSeqNumber >= channel.incomingReliableSeqNumber) {
                    pCurrentCmd = pNode;
                    break;
                }

                if (pCmd->reliableSeqNumber < cmd.header.reliableSeqNumber) {
                    pCurrentCmd = pNode;
                    break;
                }

                if (pCmd->reliableSeqNumber > cmd.header.reliableSeqNumber) {
                    continue;
                }

                if (pCmd->unreliableSeqNumber < seqNumber) {
                    pCurrentCmd = pNode;
                    break;
                } else if (pCmd->unreliableSeqNumber == seqNumber) {
                    break;
                }
            }
        }
        break;

    case HNET_PROTOCOL_COMMAND_SEND_UNSEQUENCED:
        pCurrentCmd = channel.incomingUnreliableCommands.end();
        break;

    default:
        break;
    }

    return pCurrentCmd;
}

static void hnet_peer_dispatch_incoming_unreliable_commands(HNetPeer& peer, HNetChannel& channel)
{
    HNetListNode *pDroppedNode, *pStartNode, *pCurrentNode;
    pDroppedNode = pStartNode = pCurrentNode = channel.incomingUnreliableCommands.begin();

    for (; pCurrentNode != channel.incomingUnreliableCommands.end(); pCurrentNode = pCurrentNode->next) {
        HNetIncomingCommand* pCmd = reinterpret_cast<HNetIncomingCommand*>(pCurrentNode);

        if ((pCmd->command.header.command & HNET_PROTOCOL_COMMAND_MASK) == HNET_PROTOCOL_COMMAND_SEND_UNSEQUENCED) {
            continue;
        }

        if (pCmd->reliableSeqNumber == channel.incomingReliableSeqNumber) {
            if (pCmd->fragmentsRemaining <= 0) {
                channel.incomingUnreliableSeqNumber = pCmd->unreliableSeqNumber;
                continue;
            }

            if (pStartNode != pCurrentNode) {
                peer.dispatchedCommands.push_back(pStartNode, pCurrentNode->prev);
                if (!peer.needsDispatch) {
                    peer.host->dispatchQueue.push_back(&peer.dispatchList);
                    peer.needsDispatch = true;
                }
                pDroppedNode = pCurrentNode;
            } else if (pDroppedNode != pCurrentNode) {
                pDroppedNode = pCurrentNode->prev;
            }
        } else {
            uint16_t reliableWindow = hnet_calc_reliable_window(pCmd->reliableSeqNumber);
            uint16_t currentWindow = hnet_calc_reliable_window(channel.incomingReliableSeqNumber);

            if (pCmd->reliableSeqNumber < channel.incomingReliableSeqNumber) {
                reliableWindow += HNET_PEER_RELIABLE_WINDOWS;
            }
            if (currentWindow <= reliableWindow && reliableWindow < currentWindow + HNET_PEER_FREE_RELIABLE_WINDOWS - 1) {
                break;
            }

            pDroppedNode = pCurrentNode->next;

            if (pStartNode != pCurrentNode) {
                peer.dispatchedCommands.push_back(pStartNode, pCurrentNode->prev);
                if (!peer.needsDispatch) {
                    peer.host->dispatchQueue.push_back(&peer.dispatchList);
                    peer.needsDispatch = true;
                }
            }
        }
        pStartNode = pCurrentNode->next;
    }

    if (pStartNode != pCurrentNode) {
        peer.dispatchedCommands.push_back(pStartNode, pCurrentNode->prev);
        if (!peer.needsDispatch) {
            peer.host->dispatchQueue.push_back(&peer.dispatchList);
            peer.needsDispatch = true;
        }
        pDroppedNode = pCurrentNode;
    }

    hnet_peer_remove_incoming_commands(channel.incomingUnreliableCommands, channel.incomingUnreliableCommands.begin(), pDroppedNode);
}

static void hnet_peer_dispatch_incoming_reliable_commands(HNetPeer& peer, HNetChannel& channel)
{
    HNetListNode* pNode = nullptr;

    for (pNode = channel.incomingReliableCommands.begin(); pNode != channel.incomingReliableCommands.end(); pNode = pNode->next) {
        HNetIncomingCommand* pCmd = reinterpret_cast<HNetIncomingCommand*>(pNode);
        if (pCmd->fragmentCount > 0 || pCmd->reliableSeqNumber != (channel.incomingReliableSeqNumber + 1)) {
            break;
        }

        channel.incomingReliableSeqNumber = pCmd->reliableSeqNumber;

        if (pCmd->fragmentCount > 0) {
            channel.incomingReliableSeqNumber += pCmd->fragmentCount - 1;
        }
    }

    if (pNode == channel.incomingReliableCommands.begin()) {
        return;
    }

    channel.incomingUnreliableSeqNumber = 0;

    peer.dispatchedCommands.push_back(channel.incomingReliableCommands.begin(), pNode->prev);

    if (!peer.needsDispatch) {
        peer.host->dispatchQueue.push_back(&peer.dispatchList);
        peer.needsDispatch = true;
    }

    if (!channel.incomingUnreliableCommands.empty()) {
        hnet_peer_dispatch_incoming_unreliable_commands(peer, channel);
    }
}

void hnet_peer_on_connect(HNetPeer& peer)
{
    if (peer.state != HNetPeerState::Connected && peer.state != HNetPeerState::DisconnectLater) {
        if (peer.incomingBandwidth != 0) {
            ++peer.host->bandwidthLimitedPeers;
        }
        ++peer.host->connectedPeers;
    }
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

void hnet_peer_disconnect(HNetPeer& peer, uint32_t data)
{
    if (peer.state == HNetPeerState::Disconnecting ||
        peer.state == HNetPeerState::Disconnected ||
        peer.state == HNetPeerState::AckDisconnet ||
        peer.state == HNetPeerState::Zombie) {
        return;
    }

    hnet_peer_reset_queues(peer);

    HNetProtocol cmd;
    cmd.header.command = HNET_PROTOCOL_COMMAND_DISCONNECT;
    cmd.header.channelId = 0xFF;
    cmd.disconnect.data = HNET_HOST_TO_NET_32(data);

    if (peer.state == HNetPeerState::Connected || peer.state == HNetPeerState::DisconnectLater) {
        cmd.header.command |= HNET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
    } else {
        cmd.header.command |= HNET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED;
    }

    hnet_peer_queue_outgoing_command(peer, cmd, nullptr, 0, 0);

    if (peer.state == HNetPeerState::Connected || peer.state == HNetPeerState::DisconnectLater) {
        hnet_peer_on_disconnect(peer);
        peer.state = HNetPeerState::Disconnecting;
    } else {
        hnet_host_flush(*peer.host);
        hnet_peer_reset(peer);
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

bool hnet_peer_queue_outgoing_command(HNetPeer& peer, const HNetProtocol& cmd, HNetPacket* pPacket, uint32_t offset, uint16_t length)
{
    HNetOutgoingCommand* pCmd = static_cast<HNetOutgoingCommand*>(hnet_malloc(sizeof(HNetOutgoingCommand)));
    if (pCmd == nullptr) {
        return false;
    }

    pCmd->command = cmd;
    pCmd->fragmentOffset = offset;
    pCmd->fragmentLength = length;
    pCmd->packet = pPacket;
    if (pPacket != nullptr) {
        ++pPacket->refCount;
    }

    hnet_peer_setup_outgoing_command(peer, *pCmd);
    return true;
}

bool hnet_peer_queue_incoming_command(HNetPeer& peer, const HNetProtocol& cmd, uint8_t* pData, size_t dataLength, uint32_t flags, uint32_t fragmentCount)
{
    // fragment is not supported.
    if (peer.state == HNetPeerState::DisconnectLater) {
        return false;
    }

    HNetChannel& channel = peer.channels[cmd.header.channelId];
    if ((cmd.header.command & HNET_PROTOCOL_COMMAND_MASK) != HNET_PROTOCOL_COMMAND_SEND_UNSEQUENCED) {
        uint16_t seqNumber = cmd.header.reliableSeqNumber;
        uint16_t reliableWindow = hnet_calc_reliable_window(seqNumber);
        uint16_t currentWindow = hnet_calc_reliable_window(channel.incomingReliableSeqNumber);

        if (seqNumber < channel.incomingReliableSeqNumber) {
            reliableWindow += HNET_PEER_RELIABLE_WINDOWS;
        }
        if (reliableWindow < currentWindow || reliableWindow >= (currentWindow + HNET_PEER_FREE_RELIABLE_WINDOWS - 1)) {
            return false;
        }
    }

    HNetListNode* pCurrent = hnet_peer_find_incoming_current_command(peer, cmd);
    if (pCurrent == nullptr) {
        return false;
    }

    if (peer.totalWaitingData >= peer.host->maxWaitingData) {
        return false;
    }

    HNetPacket* pPacket = hnet_packet_create(pData, dataLength, flags);
    if (pPacket == nullptr) {
        return false;
    }

    HNetIncomingCommand* pCmd = static_cast<HNetIncomingCommand*>(hnet_malloc(sizeof(HNetIncomingCommand)));
    if (pCmd == nullptr) {
        hnet_packet_destroy(pPacket);
        return false;
    }

    pCmd->reliableSeqNumber = cmd.header.reliableSeqNumber;
    pCmd->unreliableSeqNumber = HNET_NET_TO_HOST_16(cmd.sendUnreliable.unreliableSeqNumber) & 0xFFFF;
    pCmd->command = cmd;
    pCmd->fragmentCount = fragmentCount;
    pCmd->fragmentsRemaining = fragmentCount;
    pCmd->packet = pPacket;
    pCmd->fragments = nullptr;

    ++pPacket->refCount;
    peer.totalWaitingData += pPacket->dataLength;

    HNetList::insert(pCurrent, &pCmd->incomingCommandList);

    switch (cmd.header.command & HNET_PROTOCOL_COMMAND_MASK) {
    case HNET_PROTOCOL_COMMAND_SEND_RELIABLE:
    case HNET_PROTOCOL_COMMAND_SEND_FRAGMENT:
        hnet_peer_dispatch_incoming_reliable_commands(peer, channel);
        break;

    default:
        hnet_peer_dispatch_incoming_unreliable_commands(peer, channel);
        break;
    }
    return true;
}

bool hnet_peer_queue_ack(HNetPeer& peer, const HNetProtocol& cmd, uint16_t sentTime)
{
    if (cmd.header.channelId < peer.channelCount) {
        HNetChannel& channel = peer.channels[cmd.header.channelId];
        uint16_t reliableWindow = hnet_calc_reliable_window(cmd.header.reliableSeqNumber);
        uint16_t currentWindow = hnet_calc_reliable_window(channel.incomingReliableSeqNumber);

        if (cmd.header.reliableSeqNumber < channel.incomingReliableSeqNumber) {
            reliableWindow += HNET_PEER_RELIABLE_WINDOWS;
        }

        if (currentWindow + HNET_PEER_FREE_RELIABLE_WINDOWS - 1 <= reliableWindow && reliableWindow <= currentWindow + HNET_PEER_FREE_RELIABLE_WINDOWS) {
            return false;
        }
    }

    HNetAck* pAck = static_cast<HNetAck*>(hnet_malloc(sizeof(HNetAck)));
    if (pAck == nullptr) {
        return false;
    }

    peer.outgoingDataTotal += sizeof(HNetProtocolAck);
    pAck->sentTime = sentTime;
    pAck->command = cmd;
    peer.acks.push_back(&pAck->ackList);
    return true;
}

void hnet_peer_throttle(HNetPeer& peer, uint32_t rtt)
{
    if (peer.lastRoundTripTime <= peer.lastRoundTripTimeVariance) {
        peer.packetThrottle = peer.packetThrottleLimit;
    } else if (rtt < peer.lastRoundTripTime) {
        peer.packetThrottle += peer.packetThrottleAcceleration;
        if (peer.packetThrottle > peer.packetThrottleLimit) {
            peer.packetThrottle = peer.packetThrottleLimit;
        }
    } else if (rtt > peer.lastRoundTripTime + 2 * peer.lastRoundTripTimeVariance) {
        if (peer.packetThrottle > peer.packetThrottleDeceleration) {
            peer.packetThrottle -= peer.packetThrottleDeceleration;
        } else {
            peer.packetThrottle = 0;
        }
    }
}

bool hnet_peer_send(HNetPeer& peer, uint8_t channelId, HNetPacket& packet)
{
    // fragment is not supported.
    if (peer.state != HNetPeerState::Connected || channelId >= peer.channelCount || packet.dataLength > peer.host->maxPacketSize) {
        return false;
    }

    HNetChannel& channel = peer.channels[channelId];
    HNetProtocol cmd;
    cmd.header.channelId = channelId;

    if ((packet.flags & (HNET_PACKET_FLAG_RELIABLE | HNET_PACKET_FLAG_UNSEQUENCED)) == HNET_PACKET_FLAG_UNSEQUENCED) {
        cmd.header.command = HNET_PROTOCOL_COMMAND_SEND_UNSEQUENCED | HNET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED;
        cmd.sendUnsequenced.dataLength = HNET_HOST_TO_NET_16(packet.dataLength);
    } else if (packet.flags & HNET_PACKET_FLAG_RELIABLE || channel.outgoingUnreliableSeqNumber >= 0xFFFF) {
        cmd.header.command = HNET_PROTOCOL_COMMAND_SEND_RELIABLE | HNET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
        cmd.sendReliable.dataLength = HNET_HOST_TO_NET_16(packet.dataLength);
    } else {
        cmd.header.command = HNET_PROTOCOL_COMMAND_SEND_UNSEQUENCED;
        cmd.sendUnreliable.dataLength = HNET_HOST_TO_NET_16(packet.dataLength);
    }

    if (!hnet_peer_queue_outgoing_command(peer, cmd, &packet, 0, packet.dataLength)) {
        return false;
    }

    return true;
}

HNetPacket* hnet_peer_recv(HNetPeer& peer, uint8_t& channelId)
{
    if (peer.dispatchedCommands.empty()) {
        return nullptr;
    }

    HNetIncomingCommand& cmd = *reinterpret_cast<HNetIncomingCommand*>(HNetList::remove(peer.dispatchedCommands.begin()));
    channelId = cmd.command.header.channelId;

    HNetPacket* pPacket = cmd.packet;
    --pPacket->refCount;

    if (cmd.fragments != nullptr) {
        hnet_free(cmd.fragments);
    }
    hnet_free(&cmd);
    peer.totalWaitingData -= pPacket->dataLength;
    return pPacket;
}
