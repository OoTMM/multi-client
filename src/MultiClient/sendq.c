#include "multi.h"
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#endif

#define SQ_TTL 2000

void sendqInit(SendQueue* q)
{
    q->file = NULL;
    q->size = 0;
    q->capacity = 8;
    q->data = NULL;
}

int sendqOpen(SendQueue* q, const uint8_t* uuid)
{
    int count;
    char buffer[MAX_PATH];

    /* Close previous send queue */
    sendqClose(q);

    _mkdir("data");
    snprintf(buffer, MAX_PATH, "data/%02x%02x%02x%02x%02x%02x%02x%02x"
        "%02x%02x%02x%02x%02x%02x%02x%02x/",
        uuid[0], uuid[1], uuid[2], uuid[3],
        uuid[4], uuid[5], uuid[6], uuid[7],
        uuid[8], uuid[9], uuid[10], uuid[11],
        uuid[12], uuid[13], uuid[14], uuid[15]);
    _mkdir(buffer);
    strcat(buffer, "sendq.bin");

    /* Ensure the file exists */
    q->file = fopen(buffer, "ab");
    fclose(q->file);

    /* Open the file */
    q->file = fopen(buffer, "r+b");
    if (!q->file)
        return -1;

    /* Read the number of entries */
    fseek(q->file, 0, SEEK_END);
    count = ftell(q->file) / sizeof(LedgerFullEntry);
    fseek(q->file, 0, SEEK_SET);

    /* Allocate the buffer */
    q->size = count;
    q->capacity = count + 8;
    q->data = malloc(q->capacity * sizeof(SendQueueEntry));

    /* Read the entries */
    for (int i = 0; i < count; ++i)
    {
        fread(&q->data[i].entry, sizeof(LedgerFullEntry), 1, q->file);
        q->data[i].ttl = 0;
    }

    return 0;
}

void sendqClose(SendQueue* q)
{
    if (q->file)
        fclose(q->file);
    q->file = NULL;
    q->size = 0;
    free(q->data);
}

static int sendqLocate(const SendQueue* sq, uint64_t key)
{
    for (uint32_t i = 0; i < sq->size; ++i)
    {
        if (sq->data[i].entry.key == key)
            return (int)i;
    }

    return -1;
}

static int sendqWrite(SendQueue* sq, const LedgerFullEntry* entry)
{
    int id;
    uint32_t newCapacity;
    void* newData;

    /* No need to duplicate an entry */
    id = sendqLocate(sq, entry->key);
    if (id >= 0)
        return id;

    /* Ensure there is enough space */
    if (sq->size == sq->capacity)
    {
        newCapacity = sq->capacity * 2;
        newData = realloc(sq->data, newCapacity * sizeof(SendQueueEntry));
        if (!newData)
            return -1;
        sq->data = newData;
        sq->capacity = newCapacity;
    }

    /* Write in-memory */
    id = (int)sq->size;
    memcpy(&sq->data[id].entry, entry, sizeof(LedgerFullEntry));
    sq->data[id].ttl = 0;
    ++sq->size;

    /* Write to disk */
    fseek(sq->file, id * sizeof(LedgerFullEntry), SEEK_SET);
    fwrite(entry, sizeof(LedgerFullEntry), 1, sq->file);
    fflush(sq->file);

    return id;
}

static int sendqTransfer(NetBuffer* nb, const LedgerFullEntry* entry)
{
    char* bufDst;

    /* Write to the net buffer */
    bufDst = netBufReserve(nb, 1 + 8 + 1 + entry->size);
    if (!bufDst)
        return -1;
    memcpy(bufDst + 0, "\x01", 1);
    memcpy(bufDst + 1, &entry->key, 8);
    memcpy(bufDst + 9, &entry->size, 1);
    memcpy(bufDst + 10, entry->data, entry->size);

    return 0;
}

int sendqAppend(SendQueue* sq, const LedgerFullEntry* entry)
{
    int sqId;

    /* Write to the send queue */
    sqId = sendqWrite(sq, entry);
    if (sqId < 0)
        return -1;

    return 0;
}

void sendqTick(SendQueue* sq, NetBuffer* nb)
{
    SendQueueEntry* sqe;

    for (uint32_t i = 0; i < sq->size; ++i)
    {
        sqe = &sq->data[i];
        if (sqe->ttl)
        {
            sqe->ttl -= 1;
            continue;
        }
        if (!sendqTransfer(nb, &sqe->entry))
            sqe->ttl = SQ_TTL;
    }
}

void sendqAck(SendQueue* q, uint64_t key)
{
    int id;

    id = sendqLocate(q, key);
    if (id < 0)
        return;

    printf("ACK %016llx\n", key);

    /* Check if it's not the last element */
    if (id != (int)q->size - 1)
    {
        memcpy(&q->data[id], &q->data[q->size - 1], sizeof(SendQueueEntry));
        fseek(q->file, id * sizeof(LedgerFullEntry), SEEK_SET);
        fwrite(&q->data[id].entry, sizeof(LedgerFullEntry), 1, q->file);
        fflush(q->file);
    }

    /* Truncate the file */
    q->size--;
    _chsize_s(_fileno(q->file), q->size * sizeof(LedgerFullEntry));
}
