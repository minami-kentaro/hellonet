#pragma once

#include "types.h"

struct HNetEvent;
struct HNetHost;
struct HNetPeer;

#define HNET_PROTOCOL_MIN_MTU             576
#define HNET_PROTOCOL_MAX_MTU             4096
#define HNET_PROTOCOL_MAX_PACKET_COMMANDS 32
#define HNET_PROTOCOL_MIN_WINDOW_SIZE     4096
#define HNET_PROTOCOL_MAX_WINDOW_SIZE     65536
#define HNET_PROTOCOL_MIN_CHANNEL_COUNT   1
#define HNET_PROTOCOL_MAX_CHANNEL_COUNT   255
#define HNET_PROTOCOL_MAX_PEER_ID         0xFFF
#define HNET_PROTOCOL_MAX_FRAGMENT_COUNT  1024 * 1024

#define HNET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE (1 << 7)
#define HNET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED (1 << 6)

#define HNET_PROTOCOL_HEADER_FLAG_COMPRESSED (1 << 14)
#define HNET_PROTOCOL_HEADER_FLAG_SENT_TIME  (1 << 15)
#define HNET_PROTOCOL_HEADER_FLAG_MASK       (HNET_PROTOCOL_HEADER_FLAG_COMPRESSED | HNET_PROTOCOL_HEADER_FLAG_SENT_TIME)

#define HNET_PROTOCOL_HEADER_SESSION_MASK  (3 << 12)
#define HNET_PROTOCOL_HEADER_SESSION_SHIFT 12

#define HNET_PACKED __attribute__ ((packed))

enum HNetProtocolCommand : uint8_t
{
    HNET_PROTOCOL_COMMAND_NONE = 0,
    HNET_PROTOCOL_COMMAND_ACKNOWLEDGE,
    HNET_PROTOCOL_COMMAND_CONNECT,
    HNET_PROTOCOL_COMMAND_VERIFY_CONNECT,
    HNET_PROTOCOL_COMMAND_DISCONNECT,
    HNET_PROTOCOL_COMMAND_PING,
    HNET_PROTOCOL_COMMAND_SEND_RELIABLE,
    HNET_PROTOCOL_COMMAND_SEND_UNRELIABLE,
    HNET_PROTOCOL_COMMAND_SEND_FRAGMENT,
    HNET_PROTOCOL_COMMAND_SEND_UNSEQUENCED,
    HNET_PROTOCOL_COMMAND_BANDWIDTH_LIMIT,
    HNET_PROTOCOL_COMMAND_THROTTLE_CONFIGURE,
    HNET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT,
    HNET_PROTOCOL_COMMAND_COUNT,
    HNET_PROTOCOL_COMMAND_MASK = 0x0F,
};

struct HNetProtocolHeader
{
    uint16_t peerId;
    uint16_t sentTime;
} HNET_PACKED;

struct HNetProtocolCommandHeader
{
    uint8_t command;
    uint8_t channelId;
    uint16_t reliableSeqNumber;
} HNET_PACKED;

struct HNetProtocolAck
{
    HNetProtocolCommandHeader header;
    uint16_t recvReliableSeqNumber;
    uint16_t recvSentTime;
} HNET_PACKED;

struct HNetProtocolConnect
{
    HNetProtocolCommandHeader header;
    uint16_t outgoingPeerId;
    uint8_t incomingSessionId;
    uint8_t outgoingSessionId;
    uint32_t mtu;
    uint32_t windowSize;
    uint32_t channelCount;
    uint32_t incomingBandwidth;
    uint32_t outgoingBandwidth;
    uint32_t packetThrottleInterval;
    uint32_t packetThrottleAcceleration;
    uint32_t packetThrottleDeceleration;
    uint32_t connectId;
    uint32_t data;
} HNET_PACKED;

struct HNetProtocolVerifyConnect
{
    HNetProtocolCommandHeader header;
    uint16_t outgoinngPeerId;
    uint8_t incomingSessionId;
    uint8_t outgoingSessionId;
    uint32_t mtu;
    uint32_t windowSize;
    uint32_t channelCount;
    uint32_t incomingBandwidth;
    uint32_t outgoingBandwidth;
    uint32_t packetThrottleInterval;
    uint32_t packetThrottleAcceleration;
    uint32_t packetThrottleDeceleration;
    uint32_t connectId;
} HNET_PACKED;

struct HNetProtocolBandwidthLimit
{
    HNetProtocolCommandHeader header;
    uint32_t incomingBandwidth;
    uint32_t outgoingBandwidth;
} HNET_PACKED;

struct HNetProtocolThrottleConfigure
{
    HNetProtocolCommandHeader header;
    uint32_t packetThrottleInterval;
    uint32_t packetThrottleAcceleration;
    uint32_t packetThrottleDeceleration;
} HNET_PACKED;

struct HNetProtocolDisconnect
{
    HNetProtocolCommandHeader header;
    uint32_t data;
} HNET_PACKED;

struct HNetProtocolPing
{
    HNetProtocolCommandHeader header;
} HNET_PACKED;

struct HNetProtocolSendReliable
{
    HNetProtocolCommandHeader header;
    uint16_t dataLength;
} HNET_PACKED;

struct HNetProtocolSendUnreliable
{
    HNetProtocolCommandHeader header;
    uint16_t unreliableSeqNumber;
    uint16_t dataLength;
} HNET_PACKED;

struct HNetProtocolSendUnsequenced
{
    HNetProtocolCommandHeader header;
    uint16_t unseqGroup;
    uint16_t dataLength;
} HNET_PACKED;

struct HNetProtocolSendFragment
{
    HNetProtocolCommandHeader header;
    uint16_t startSeqNumber;
    uint16_t dataLength;
    uint32_t fragmentCount;
    uint32_t fragmentNumber;
    uint32_t totalLength;
    uint32_t fragmentOffset;
} HNET_PACKED;

union HNetProtocol
{
    HNetProtocolCommandHeader header;
    HNetProtocolAck ack;
    HNetProtocolConnect connect;
    HNetProtocolVerifyConnect verifyConenct;
    HNetProtocolDisconnect disconnect;
    HNetProtocolPing ping;
    HNetProtocolSendReliable sendReliable;
    HNetProtocolSendUnreliable sendUnreliable;
    HNetProtocolSendUnsequenced sendUnsequenced;
    HNetProtocolSendFragment sendFragment;
    HNetProtocolBandwidthLimit bandwidthLimit;
    HNetProtocolThrottleConfigure throttleConfigure;
} HNET_PACKED;

size_t hnet_protocol_command_size(uint8_t command);
void hnet_protocol_init_connect_command(const HNetHost& host, const HNetPeer& peer, uint32_t data, HNetProtocol& cmd);
int32_t hnet_protocol_send_outgoing_commands(HNetHost& host, HNetEvent* pEvent, bool checkFormTimeouts);