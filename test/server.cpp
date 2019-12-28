#include "hnet.h"

void run()
{
    HNetAddr addr{HNET_HOST_ANY, 20201};
    HNetServer* pServer = HNetServer::create(addr, 32);
    if (pServer == nullptr) {
        return;
    }
    HNetServer::destroy(pServer);
}

int main()
{
    if (hnet_initialize()) {
        run();
        hnet_finalize();
    }
    return 0;
}