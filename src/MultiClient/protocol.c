#include "multi.h"

static int sockSend(SOCKET s, const void* data, uint32_t size)
{
    uint32_t sent = 0;
    while (sent < size)
    {
        int ret = send(s, (const char*)data + sent, size - sent, 0);
        if (ret == SOCKET_ERROR)
        {
            printf("send failed: %d\n", WSAGetLastError());
            return 0;
        }
        sent += ret;
    }
    return 1;
}

static int sockRecv(SOCKET s, void* data, uint32_t size)
{
    uint32_t acc = 0;
    while (acc < size)
    {
        int ret = recv(s, (char*)data + acc, size - acc, 0);
        if (ret == SOCKET_ERROR)
        {
            printf("recv failed: %d\n", WSAGetLastError());
            return 0;
        }
        acc += ret;
    }
    return 1;
}

static void pj64Read(Game* game, void* buf, uint8_t op, uint32_t size, uint32_t addr)
{
    char packet[5];

    packet[0] = op;
    memcpy(packet + 1, &addr, 4);
    if (!sockSend(game->socketApi, packet, 5))
    {
        game->apiError = 1;
        closesocket(game->socketApi);
        game->socketApi = INVALID_SOCKET;
        return;
    }
    if (!sockRecv(game->socketApi, buf, size))
    {
        game->apiError = 1;
        closesocket(game->socketApi);
        game->socketApi = INVALID_SOCKET;
        return;
    }
}

static void pj64Write(Game* game, const void* buf, uint8_t op, uint32_t size, uint32_t addr)
{
    char packet[9];

    packet[0] = op;
    memcpy(packet + 1, &addr, 4);
    memcpy(packet + 5, buf, size);
    if (!sockSend(game->socketApi, packet, 5 + size))
    {
        game->apiError = 1;
        closesocket(game->socketApi);
        game->socketApi = INVALID_SOCKET;
        return;
    }
}

static void aresStart(Game* game)
{
    char packet[1];

    packet[0] = '+';
    if (!sockSend(game->socketApi, packet, 1))
    {
        game->apiError = 1;
        closesocket(game->socketApi);
        game->socketApi = INVALID_SOCKET;
        return;
    }
}

static uint8_t unhex(char ch) {
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    return 0;
}

static int aresCommandRead(Game* game, uint32_t addr, int count, uint8_t *value)
{
    int read;
    int error = 0;
    uint8_t checksum = 0;

    LOGF("aresRead(%08x, %d)\n", addr, count);
    printf("    => ");

    char buf[256];
    sprintf(buf, "m%08x,%x", addr, count);
    for (int i = 0; buf[i] != 0; ++i)
        checksum += buf[i];

    error |= !sockSend(game->socketApi, "$", 1);
    error |= !sockSend(game->socketApi, buf, strlen(buf));
    sprintf(buf, "#%02x", checksum);
    error |= !sockSend(game->socketApi, buf, strlen(buf));
    if (error)
        return 0;

    read = sockRecv(game->socketApi, buf, 1);
    if (read == 0)
        return 0;
    if (buf[0] != '+')
        return 0;
    
    read = sockRecv(game->socketApi, buf, 1+count*2+3);
    if (read == 0)
        return 0;
    if (buf[0] != '$')
        return 0;
    for (int i=0; i<count; i++) {
        *value++ = (unhex(buf[1+i*2]) << 4) | unhex(buf[1+i*2+1]);
        printf("%02x", value[-1]);
    }
    printf("\n");

    if (buf[1+count*2] != '#')
        return 0;

    if (!sockSend(game->socketApi, "+", 1))
        return 0;
    return 1;
}

static int aresCommandWrite(Game* game, uint32_t addr, int size, uint32_t value)
{
    int read;
    int error = 0;
    uint8_t checksum = 0;

    char buf[32];
    sprintf(buf, "M%08x,%x:%0*X", addr, size, size*2, value);
    for (int i = 0; buf[i] != 0; ++i)
        checksum += buf[i];

    error |= !sockSend(game->socketApi, "$", 1);
    error |= !sockSend(game->socketApi, buf, strlen(buf));
    sprintf(buf, "#%02x", checksum);
    error |= !sockSend(game->socketApi, buf, strlen(buf));
    if (error)
        return 0;

    read = sockRecv(game->socketApi, buf, 1);
    if (read == 0)
        return 0;
    if (buf[0] != '+')
        return 0;
    
    read = sockRecv(game->socketApi, buf, 1+2+3);
    if (read == 0)
        return 0;
    if (buf[0] != '$')
        return 0;
    if (strncmp(buf + 1, "OK#", 3) != 0)
        return 0;

    if (!sockSend(game->socketApi, "+", 1))
        return 0;
    return 1;
}

static uint32_t aresReadInt(Game* game, uint32_t addr, int size)
{
    uint8_t buf[4];
    if (!aresCommandRead(game, addr, size, buf))
    {
        game->apiError = 1;
        closesocket(game->socketApi);
        game->socketApi = INVALID_SOCKET;
        return 0;
    }
    uint32_t value;
    switch (size) {
    case 4: value = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]; break;
    case 2: value = (buf[0] << 8) | buf[1]; break;
    case 1: value = buf[0]; break;
    }
    return value;
}

static void aresWrite(Game* game, uint32_t addr, uint32_t value, int size)
{
    LOGF("aresWrite(%08x, %08x, %d)\n", addr, value, size);
    if (!aresCommandWrite(game, addr, size, value))
    {
        game->apiError = 1;
        closesocket(game->socketApi);
        game->socketApi = INVALID_SOCKET;
    }
}


uint8_t protocolRead8(Game* game, uint32_t addr)
{
    uint8_t value;
    switch (game->apiProtocol) {
    case PROTOCOL_PJ64:
        pj64Read(game, &value, 2, 1, addr);
        break;
    case PROTOCOL_ARES:
        value = aresReadInt(game, addr, 1);
        break;
    }
    return value;
}

uint16_t protocolRead16(Game* game, uint32_t addr)
{
    uint16_t value;
    switch (game->apiProtocol) {
    case PROTOCOL_PJ64:
        pj64Read(game, &value, 3, 2, addr);
        break;
    case PROTOCOL_ARES:
        value = aresReadInt(game, addr, 2);
        break;
    }
    return value;
}

uint32_t protocolRead32(Game* game, uint32_t addr)
{
    uint32_t value;
    switch (game->apiProtocol) {
    case PROTOCOL_PJ64:
        pj64Read(game, &value, 4, 4, addr);
        break;
    case PROTOCOL_ARES:
        value = aresReadInt(game, addr, 4);
        break;
    }
    return value;
}

void protocolReadBuffer(Game *game, uint32_t addr, int count, uint8_t *buffer)
{
    switch (game->apiProtocol) {
    case PROTOCOL_PJ64:
        for (int i=0; i<count; i++)
            *buffer++ = protocolRead8(game, addr+i);
        break;
    case PROTOCOL_ARES:
        aresCommandRead(game, addr, count, buffer);
        break;
    }
}

void protocolWrite8(Game* game, uint32_t addr, uint8_t value)
{
    switch (game->apiProtocol) {
    case PROTOCOL_PJ64:
        pj64Write(game, &value, 6, 1, addr);
        break;
    case PROTOCOL_ARES:
        aresWrite(game, addr, value, 1);
        break;
    }
}

void protocolWrite16(Game* game, uint32_t addr, uint16_t value)
{
    switch (game->apiProtocol) {
    case PROTOCOL_PJ64:
        pj64Write(game, &value, 7, 2, addr);
        break;
    case PROTOCOL_ARES:
        aresWrite(game, addr, value, 2);
        break;
    }
}

void protocolWrite32(Game* game, uint32_t addr, uint32_t value)
{
    switch (game->apiProtocol) {
    case PROTOCOL_PJ64:
        pj64Write(game, &value, 8, 4, addr);
        break;
    case PROTOCOL_ARES:
        aresWrite(game, addr, value, 4);
        break;
    }
}

void protocolInit(Game* game)
{
    switch (game->apiProtocol) {
    case PROTOCOL_PJ64:
        break;
    case PROTOCOL_ARES:
        aresStart(game);
        break;
    }
}