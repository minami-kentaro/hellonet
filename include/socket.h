#pragma once

#include "types.h"

using HNetSocket = int32_t;

enum class HNetSocketType
{
    Stream,
    DataGram,
};

enum class HNetSocketOption
{
    NONBLOCK,
    BROADCAST,
    RCVBUF,
    SNDBUF,
    REUSEADDR,
    RCVTIMEO,
    SNDTIMEO,
    ERROR,
    NODELAY,
};

#define HNET_HOST_TO_NET_16(value) (htons(value))
#define HNET_NET_TO_HOST_16(value) (ntohs(value))
#define HNET_SOCKET_NULL -1

bool hnet_address_set_host(HNetAddr& addr, const char* pHostName);
bool hnet_address_set_host_ip(HNetAddr& addr, const char* pHostName);
HNetSocket hnet_socket_create(HNetSocketType type);
bool hnet_socket_bind(HNetSocket socket, HNetAddr& addr);
void hnet_socket_destroy(HNetSocket socket);
bool hnet_socket_set_option(HNetSocket socket, HNetSocketOption option, int32_t val);
bool hnet_socket_get_addr(HNetSocket socket, HNetAddr& addr);
