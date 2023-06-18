#include "multi.h"

static void sockSend(SOCKET s, const void* data, uint32_t size)
{
    uint32_t sent = 0;
    while (sent < size)
    {
        int ret = send(s, (const char*)data + sent, size - sent, 0);
        if (ret == SOCKET_ERROR)
        {
            printf("send failed: %d\n", WSAGetLastError());
            return;
        }
        sent += ret;
    }
}

static void sockRecv(SOCKET s, void* data, uint32_t size)
{
    uint32_t acc = 0;
    while (acc < size)
    {
        int ret = recv(s, (char*)data + acc, size - acc, 0);
        if (ret == SOCKET_ERROR)
        {
            printf("recv failed: %d\n", WSAGetLastError());
            return;
        }
        acc += ret;
    }
}

static void protocolRead(SOCKET s, void* buf, uint8_t op, uint32_t size, uint32_t addr)
{
    char packet[5];

    packet[0] = op;
    memcpy(packet + 1, &addr, 4);
    sockSend(s, packet, 5);
    sockRecv(s, buf, size);
}

static void protocolWrite(SOCKET s, const void* buf, uint8_t op, uint32_t size, uint32_t addr)
{
    char packet[9];

    packet[0] = op;
    memcpy(packet + 1, &addr, 4);
    memcpy(packet + 5, buf, size);
    sockSend(s, packet, 5 + size);
}

uint8_t protocolRead8(Game* game, uint32_t addr)
{
    uint8_t value;
    protocolRead(game->socketApi, &value, 2, 1, addr);
    return value;
}

uint16_t protocolRead16(Game* game, uint32_t addr)
{
    uint16_t value;
    protocolRead(game->socketApi, &value, 3, 2, addr);
    return value;
}

uint32_t protocolRead32(Game* game, uint32_t addr)
{
    uint32_t value;
    protocolRead(game->socketApi, &value, 4, 4, addr);
    return value;
}

void protocolWrite8(Game* game, uint32_t addr, uint8_t value)
{
    protocolWrite(game->socketApi, &value, 6, 1, addr);
}

void protocolWrite16(Game* game, uint32_t addr, uint16_t value)
{
    protocolWrite(game->socketApi, &value, 7, 2, addr);
}

void protocolWrite32(Game* game, uint32_t addr, uint32_t value)
{
    protocolWrite(game->socketApi, &value, 8, 4, addr);
}
