#include "multi.h"

static int usage(const char* prog)
{
    printf("Usage: %s [host [port]]\n", prog);
    return 2;
}

int main(int argc, char** argv)
{
    App app;
    int ret;
    const char* host;
    uint16_t port;

    host = "multi.ootmm.com";
    port = 13248;

    if (argc > 3)
        return usage(argv[0]);
    if (argc > 1)
        host = argv[1];
    if (argc > 2)
        port = atoi(argv[2]);

    if (appInit(&app))
        return 1;
    if (appListen(&app, "localhost", 13249))
    {
        appQuit(&app);
        return 1;
    }
    ret = appRun(&app, host, port);
    if (appQuit(&app))
        return 1;
    return ret;
}
