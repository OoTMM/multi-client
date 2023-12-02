#include "multi.h"

int netBufInit(NetBuffer* nb)
{
    nb->capacity = 4096;
    nb->size = 0;
    nb->pos = 0;
    nb->data = malloc(nb->capacity);

    return nb->data ? 0 : -1;
}

void netBufFree(NetBuffer* nb)
{
    free(nb->data);
    nb->data = NULL;
}

void netBufClear(NetBuffer* nb)
{
    nb->size = 0;
    nb->pos = 0;
}

int netBufTransfer(SOCKET sock, NetBuffer* nb)
{
    int ret;

    for (;;)
    {
        if (nb->pos == nb->size)
        {
            nb->pos = 0;
            nb->size = 0;
            return 0;
        }

        ret = send(sock, nb->data + nb->pos, nb->size - nb->pos, 0);
        if (ret == SOCKET_ERROR)
        {
            if (WSAGetLastError() == WSAEWOULDBLOCK)
                return 0;
            return -1;
        }
        nb->pos += ret;
    }
}

int netBufIsEmpty(const NetBuffer* nb)
{
    return nb->size == 0;
}

int netBufAppend(NetBuffer* nb, const void* data, uint32_t size)
{
    uint32_t newCapacity;
    uint32_t newSize;
    char* newData;

    /* Handle resize */
    newSize = nb->size + size;
    if (newSize > nb->capacity)
    {
        newCapacity = nb->capacity;
        while (newCapacity < newSize)
            newCapacity *= 2;

        newData = (char*)malloc(newCapacity);
        if (!newData)
            return -1;

        memcpy(newData, nb->data, nb->size);
        free(nb->data);
        nb->data = newData;
        nb->capacity = newCapacity;
    }

    /* Append the data */
    memcpy(nb->data + nb->size, data, size);
    nb->size = newSize;
    return 0;
}
