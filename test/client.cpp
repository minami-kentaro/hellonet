#include <time.h>
#include "hnet.h"

static void sleep()
{
    timespec time{};
    time.tv_nsec = 1000000;
    nanosleep(&time, nullptr);
}

static void run()
{
    HNetClient* pClient = HNetClient::create();
    if (pClient != nullptr) {
        pClient->connect("127.0.0.1", 20201);
        for (uint32_t counter = 0;; counter++) {
            pClient->update();
            sleep();
            if (counter == 100) {
                pClient->sendPacket();
                counter = 0;
            }
        }
        HNetClient::destroy(pClient);
    }
}

int main()
{
    if (hnet_initialize()) {
        run();
        hnet_finalize();
    }
    return 0;
}
