#include "multi.h"

#define NET_GLOBAL_ADDR 0x800001a0
#define NET_MAGIC       0x905AB56A

int apiContextLock(Game* game)
{
    uint32_t tmp;
    int attempts;

    /* Check the magic */
    tmp = protocolRead32(game, NET_GLOBAL_ADDR + 0x00);
    if (tmp != NET_MAGIC)
        return 0;
    attempts = 0;
    for(;;)
    {
        /* Magic is good, we need to lock the mutex */
        protocolWrite32(game, NET_GLOBAL_ADDR + 0x0c, 1);

        /* Check if the mutex is good */
        tmp = protocolRead32(game, NET_GLOBAL_ADDR + 0x08);
        if (tmp == 0)
        {
            /* Almost good, now we need to check for the magic and mutex lock again */
            tmp = protocolRead32(game, NET_GLOBAL_ADDR + 0x00);
            if (tmp != NET_MAGIC)
            {
                /* Magic is bad */
                protocolWrite32(game, NET_GLOBAL_ADDR + 0x0c, 0);
                return 0;
            }

            /* Magic is good, check the mutex */
            tmp = protocolRead32(game, NET_GLOBAL_ADDR + 0x0c);
            if (tmp != 1)
            {
                /* Mutex is bad */
                protocolWrite32(game, NET_GLOBAL_ADDR + 0x0c, 0);
                return 0;
            }

            /* We got a lock, fetch the net ctx addr */
            game->apiNetAddr = protocolRead32(game, NET_GLOBAL_ADDR + 0x04);
            return 1;
        }

        /* Something went wrong, retry */
        protocolWrite32(game, NET_GLOBAL_ADDR + 0x0c, 0);
        Sleep(0);
        attempts++;
        if (attempts > 20)
            return 0;
    }
}

void apiContextUnlock(Game* game)
{
    protocolWrite32(game, NET_GLOBAL_ADDR + 0x0c, 0);
    game->apiNetAddr = 0;
}
