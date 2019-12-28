#include <iostream>
#include "hnet.h"

int main()
{
    if (!hnet_initialize()) {
        return -1;
    }

    HNetAddress addr{};
    addr.host = HNET_HOST_ANY;
    addr.port = 20201;

    hnet_finalize();
    return 0;
}