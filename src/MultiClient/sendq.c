#include "multi.h"
#include <direct.h>

void sendqInit(SendQueue* q)
{
    q->file = NULL;
    q->lastSend = 0;
    q->size = 0;
    q->capacity = 8;
    q->data = NULL;
}

int sendqOpen(SendQueue* q, const uint8_t* uuid)
{
    int count;
    char buffer[MAX_PATH];

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
    q->data = (LedgerFullEntry*)malloc(q->capacity * sizeof(LedgerFullEntry));

    /* Read the entries */
    if (count)
        fread(q->data, sizeof(LedgerFullEntry), count, q->file);

    return 0;
}

void sendqClose(SendQueue* q)
{
    fclose(q->file);
    q->file = NULL;
    q->size = 0;
    q->lastSend = 0;
    free(q->data);
}
