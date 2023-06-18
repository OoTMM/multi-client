#include "multi.h"

int main(int argc, char** argv)
{
    App app;
    int ret;

    if (appInit(&app))
        return 1;
    if (appListen(&app, "localhost", 13249))
    {
        appQuit(&app);
        return 1;
    }
    ret = appRun(&app);
    if (appQuit(&app))
        return 1;
    return ret;
}
