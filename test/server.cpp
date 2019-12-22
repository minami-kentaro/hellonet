#include <iostream>
#include "hnet.h"

int main()
{
    std::cout << "hello hnet" << std::endl;
    if (hnet_initialize()) {
        hnet_finalize();
    }
    return 0;
}