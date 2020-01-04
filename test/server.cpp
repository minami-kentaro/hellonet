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
    HNetServer* pServer = HNetServer::create("127.0.0.1", 20201, 32);
    if (pServer != nullptr) {
        for (uint32_t counter = 0;; counter++) {
            pServer->update();
            sleep();
            if (counter == 100) {
                pServer->sendPacket();
                counter = 0;
            }
        }
        HNetServer::destroy(pServer);
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