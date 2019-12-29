#include "protocol.h"

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

size_t hnet_protocol_command_size(uint8_t command)
{
    return commandSizes[command & HNET_PROTOCOL_COMMAND_MASK];
}
