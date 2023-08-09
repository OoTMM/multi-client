#ifndef MULTI_H
#define MULTI_H

#define _WIN32_LEAN_AND_MEAN 1
#define _CRT_SECURE_NO_WARNINGS 1
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAX_GAMES 8

#define STATE_INIT      0x00
#define STATE_CONNECT   0x01
#define STATE_JOIN      0x02
#define STATE_READY     0x03

typedef struct
{
    uint64_t key;
    uint8_t  size;
    char     data[128];
}
LedgerFullEntry;

typedef struct
{
    int         valid;
    int         state;
    int         delay;
    unsigned    nopAcc;
    unsigned    timeout;
    int         apiError;

    SOCKET      socketApi;
    SOCKET      socketServer;

    uint32_t    apiNetAddr;
    uint8_t     uuid[16];
    uint32_t    ledgerBaseLocal;
    uint32_t    ledgerBaseGame;

    char*       txBuffer;
    uint32_t    txBufferCapacity;
    uint32_t    txBufferSize;
    uint32_t    txBufferPos;

    char*       rxBuffer;
    uint32_t    rxBufferSize;

    LedgerFullEntry* entries;
    uint32_t         entriesCount;
    uint32_t         entriesCapacity;
}
Game;

typedef struct
{
    const char* serverHost;
    uint16_t    serverPort;
    SOCKET      socket;
    Game        games[MAX_GAMES];
}
App;

int appInit(App* app);
int appListen(App* app, const char* host, uint16_t port);
int appRun(App* app, const char* host, uint16_t port);
int appQuit(App* app);

void gameInit(Game* g, SOCKET s);
void gameTick(App* app, Game* game);
void gameTransfer(Game* game, const void* data, uint32_t size);

uint8_t     protocolRead8(Game* game, uint32_t addr);
uint16_t    protocolRead16(Game* game, uint32_t addr);
uint32_t    protocolRead32(Game* game, uint32_t addr);
void        protocolWrite8(Game* game, uint32_t addr, uint8_t value);
void        protocolWrite16(Game* game, uint32_t addr, uint16_t value);
void        protocolWrite32(Game* game, uint32_t addr, uint32_t value);

int         apiContextLock(Game* game);
void        apiContextUnlock(Game* game);

#endif
