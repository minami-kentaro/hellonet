#pragma once

#include <cstdint>

using HNetServer = void*;
using HNetConnection = void*;

bool hnet_initialize();
void hnet_finalize();
void hnet_update();

HNetServer hnet_server_create();
HNetConnection hnet_connect(const char* pAddr, uint16_t port);
bool hnet_write();
void hnet_close();

void hnet_server_set_new_connection_callback();
void hnet_set_connect_callback();
void hnet_set_disconnect_callback();
void hnet_set_error_callback();
void hnet_set_read_callback();