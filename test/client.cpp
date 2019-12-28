#include "hnet.h"

void run()
{
    HNetClient* pClient = HNetClient::create();
    if (pClient == nullptr) {
        return;
    }
    HNetClient::destroy(pClient);
}

int main()
{
    if (hnet_initialize()) {
        run();
        hnet_finalize();
    }
    return 0;
}
