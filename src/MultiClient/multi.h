#ifndef MULTI_H
#define MULTI_H

#ifdef _WIN32
    #define _WIN32_LEAN_AND_MEAN 1
    #define _CRT_SECURE_NO_WARNINGS 1
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>

    static inline int sockasync(SOCKET sock, int enable)
    {
        u_long mode = enable ? 1 : 0;
        return ioctlsocket(sock, FIONBIO, &mode);
    }
#else
    typedef int SOCKET;
    #include <unistd.h>
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <netdb.h>
    #include <fcntl.h>
    #include <errno.h>

    #define INVALID_SOCKET          (-1)
    #define SOCKET_ERROR            (-1)

    #define WSADATA                 int
    #define WSAStartup(x,y)         (0)
    #define WSACleanup()            (0)
    #define WSAGetLastError()       (errno)
    #define WSAEWOULDBLOCK          EWOULDBLOCK
    #define WSAEINPROGRESS          EINPROGRESS

    #define Sleep(x)                usleep((x)*1000)
    #define closesocket(fd)         close(fd)
    #define _mkdir(x)               mkdir(x, 0777)
    #define _fileno(x)              fileno(x)
    #define _chsize_s(fd, size)     ftruncate(fd, size)
    #define MAX_PATH                16384
    #define TIMEVAL                 struct timeval

    static inline int sockasync(SOCKET sock, int enable)
    {
        int mode = fcntl(sock, F_GETFL, 0);
        if (mode == -1)
            return -1;
        if (enable)
            mode |= O_NONBLOCK;
        else
            mode &= ~O_NONBLOCK;
        return fcntl(sock, F_SETFL, mode);
    }
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAX_GAMES   8
#define VERSION     0x00000200

#define STATE_INIT      0x00
#define STATE_CONNECT   0x01
#define STATE_JOIN      0x02
#define STATE_READY     0x03

#define OP_NONE         0x00
#define OP_TRANSFER     0x01
#define OP_MSG          0x02

typedef struct
{
    uint8_t     size;
    uint16_t    clientId;
    char        data[32];
}
NetMsg;

typedef struct
{
    uint64_t key;
    uint8_t  size;
    char     data[128];
}
LedgerFullEntry;

typedef struct
{
    LedgerFullEntry    entry;
    int                ttl;
}
SendQueueEntry;

typedef struct
{
    FILE*               file;
    uint32_t            size;
    uint32_t            capacity;
    SendQueueEntry*     data;
}
SendQueue;

typedef struct
{
    char*       data;
    uint32_t    capacity;
    uint32_t    size;
    uint32_t    pos;
}
NetBuffer;

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

    NetBuffer   tx;

    char*       rxBuffer;
    uint32_t    rxBufferSize;

    uint16_t         clientId;
    LedgerFullEntry* entries;
    uint32_t         entriesCount;
    uint32_t         entriesCapacity;

    SendQueue   sendq;
    NetMsg      msg[128];
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

uint8_t     protocolRead8(Game* game, uint32_t addr);
uint16_t    protocolRead16(Game* game, uint32_t addr);
uint32_t    protocolRead32(Game* game, uint32_t addr);
void        protocolWrite8(Game* game, uint32_t addr, uint8_t value);
void        protocolWrite16(Game* game, uint32_t addr, uint16_t value);
void        protocolWrite32(Game* game, uint32_t addr, uint32_t value);

int         apiContextLock(Game* game);
void        apiContextUnlock(Game* game);

void        sendqInit(SendQueue* q);
int         sendqOpen(SendQueue* q, const uint8_t* uuid);
void        sendqClose(SendQueue* q);
int         sendqAppend(SendQueue* sq, const LedgerFullEntry* entry);
void        sendqTick(SendQueue* q, NetBuffer* nb);
void        sendqAck(SendQueue* q, uint64_t key);

int         netBufInit(NetBuffer* nb);
void        netBufFree(NetBuffer* nb);
void        netBufClear(NetBuffer* nb);
int         netBufTransfer(SOCKET sock, NetBuffer* nb);
int         netBufIsEmpty(const NetBuffer* nb);
void*       netBufReserve(NetBuffer* nb, uint32_t size);
int         netBufAppend(NetBuffer* nb, const void* data, uint32_t size);

#endif
