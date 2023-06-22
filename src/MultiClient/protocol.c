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

static void protocolRead(Game* game, void* buf, uint8_t op, uint32_t size, uint32_t addr)
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

static void protocolWrite(Game* game, const void* buf, uint8_t op, uint32_t size, uint32_t addr)
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

uint8_t protocolRead8(Game* game, uint32_t addr)
{
    uint8_t value;
    protocolRead(game, &value, 2, 1, addr);
    return value;
}

uint16_t protocolRead16(Game* game, uint32_t addr)
{
    uint16_t value;
    protocolRead(game, &value, 3, 2, addr);
    return value;
}

uint32_t protocolRead32(Game* game, uint32_t addr)
{
    uint32_t value;
    protocolRead(game, &value, 4, 4, addr);
    return value;
}

void protocolWrite8(Game* game, uint32_t addr, uint8_t value)
{
    protocolWrite(game, &value, 6, 1, addr);
}

void protocolWrite16(Game* game, uint32_t addr, uint16_t value)
{
    protocolWrite(game, &value, 7, 2, addr);
}

void protocolWrite32(Game* game, uint32_t addr, uint32_t value)
{
    protocolWrite(game, &value, 8, 4, addr);
}
