#include "Service.h"

int main()
{
    wimi::rpc::FileServer fileServer(2);

    fileServer.Run(51000);
    return 0;
}