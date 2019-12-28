#include "hnet.h"

void run()
{
    HNetServer* pServer = HNetServer::create("127.0.0.1", 20201, 32);
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