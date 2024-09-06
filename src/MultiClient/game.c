#include "multi.h"

static void gameServerClose(Game* game)
{
    if (game->socketServer != INVALID_SOCKET)
    {
        closesocket(game->socketServer);
        game->socketServer = INVALID_SOCKET;
    }

    game->timeout = 0;
    game->rxBufferSize = 0;

    netBufClear(&game->tx);
}

static void gameClose(Game* game)
{
    game->valid = 0;
    if (game->socketServer)
    {
        closesocket(game->socketServer);
        game->socketServer = INVALID_SOCKET;
    }

    if (game->socketApi)
    {
        closesocket(game->socketApi);
        game->socketApi = INVALID_SOCKET;
    }

    netBufFree(&game->tx);
    free(game->rxBuffer);
    free(game->entries);
    sendqClose(&game->sendq);
}

static void gameServerReconnect(Game* game)
{
    printf("Disconnected from server, reconnecting...\n");
    gameServerClose(game);
    game->state = STATE_CONNECT;
}

void gameInit(Game* game, SOCKET sock, int apiProtocol)
{
    /* Set initial values */
    game->valid = 1;
    game->state = STATE_INIT;
    game->delay = 0;
    game->nopAcc = 0;
    game->timeout = 0;
    game->apiError = 0;
    game->apiProtocol = apiProtocol;
    game->socketApi = sock;
    game->socketServer = INVALID_SOCKET;
    protocolInit(game);

    netBufInit(&game->tx);

    game->rxBuffer = malloc(256);
    game->rxBufferSize = 0;

    game->entriesCount = 0;
    game->entriesCapacity = 256;
    game->entries = malloc(game->entriesCapacity * sizeof(LedgerFullEntry));

    sendqInit(&game->sendq);

    memset(&game->msg, 0, sizeof(game->msg));

    /* Log */
    printf("Game created\n");
}

static void gameLoadLedger(Game* game)
{
    (void)game;
}

static void gameLoadApiData(Game* game)
{
    uint32_t uuidAddr;

    uuidAddr = protocolRead32(game, game->apiNetAddr + 0x00);
    protocolReadBuffer(game, uuidAddr, 16, game->uuid);

    if (!game->apiError)
    {
        /* Load the send queue */
        sendqOpen(&game->sendq, game->uuid);

        /* Load the ledger */
        gameLoadLedger(game);
        game->state = STATE_CONNECT;
    }
}

static uint64_t crc64(const void* data, size_t size)
{
    uint64_t crc;
    crc = 0;
    for (size_t i = 0; i < size; ++i)
    {
        crc ^= ((uint64_t)((uint8_t*)data)[i] << 56);
        for (int j = 0; j < 8; ++j)
        {
            if (crc & 0x8000000000000000)
                crc = (crc << 1) ^ 0x42f0e1eba9ea3693;
            else
                crc <<= 1;
        }
    }

    return crc;
}

static void memcpy_rev(uint8_t* dest, uint8_t *src, size_t n) {
    for (int i = 0; i < n; i++) dest[n - 1 - i] = src[i];
}

static uint64_t itemKey(uint32_t checkKey, uint8_t gameId, uint8_t playerFrom, uint32_t entriesCount)
{
    char buffer[0x10];

    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer + 0x00, &checkKey, 4);
    memcpy(buffer + 0x04, &gameId, 1);
    memcpy(buffer + 0x05, &playerFrom, 1);
    memcpy(buffer + 0x06, &entriesCount, 4);

    return crc64(buffer, sizeof(buffer));
}

static int writeItemLedger(Game* game, uint8_t playerFrom, uint8_t playerTo, uint8_t gameId, uint32_t k, uint16_t gi, uint16_t flags)
{
    LedgerFullEntry e;

    memset(&e, 0, sizeof(e));

    /* Build the key */
    e.key = itemKey(k, gameId, playerFrom, (flags & (1 << 2)) ? game->entriesCount : 0xffffffff);

    /* Build the payload */
    e.size = 0x10;
    memcpy(e.data + 0x00, &playerFrom, 1);
    memcpy(e.data + 0x01, &playerTo, 1);
    memcpy(e.data + 0x02, &gameId, 1);
    memcpy(e.data + 0x04, &k, 4);
    memcpy(e.data + 0x08, &gi, 2);
    memcpy(e.data + 0x0a, &flags, 2);

    /* Write the ledger */
    if (sendqAppend(&game->sendq, &e))
        return 0;
    return 1;
}

static void gameApiItemOut(Game* game)
{
    uint32_t itemBase;
    uint8_t playerFrom;
    uint8_t playerTo;
    uint8_t gameId;
    uint32_t key;
    uint16_t gi;
    uint16_t flags;
    uint8_t buffer[16];

    itemBase = game->apiNetAddr + 0x0c;
    protocolReadBuffer(game, itemBase, 16, buffer);
    playerFrom = buffer[0];
    playerTo = buffer[1];
    gameId = buffer[2];
    key = (buffer[4] << 24) | (buffer[5] << 16) | (buffer[6] << 8) | buffer[7];
    gi = (buffer[8] << 8) | buffer[9];
    flags = (buffer[10] << 8) | buffer[11];

    if (game->apiError)
        return;

    printf("ITEM OUT - FROM: %d, TO: %d, GAME: %d, KEY: %04X, GI: %04X, FLAGS: %04X\n", playerFrom, playerTo, gameId, key, gi, flags);

    /* Write and flag as sent */
    if (writeItemLedger(game, playerFrom, playerTo, gameId, key, gi, flags))
    {
        protocolWrite8(game, game->apiNetAddr + 0x08, 0x00);
    }
}

static void gameApiApplyLedger(Game* game)
{
    uint32_t entryId;
    uint32_t cmdBase;
    uint8_t tmp[8];
    LedgerFullEntry* fe;

    entryId = protocolRead32(game, game->apiNetAddr + 0x04);
    if (entryId == 0xffffffff)
        return;
    if (entryId >= game->entriesCount)
        return;
    if (game->apiError)
        return;

    /* Apply the ledger entry */
    printf("LEDGER APPLY #%d\n", entryId);
    fe = game->entries + entryId;
    protocolWrite8(game, game->apiNetAddr + 0x18, 0x01);
    cmdBase = game->apiNetAddr + 0x1c;

    // uint8_t playerFrom, playerTo, gameId
    protocolWriteBuffer(game, cmdBase + 0x00, 3, fe->data);

    memcpy_rev(tmp + 0x00, fe->data + 0x04, 4); // uint32_t key
    memcpy_rev(tmp + 0x04, fe->data + 0x08, 2); // uint16_t gi
    memcpy_rev(tmp + 0x06, fe->data + 0x0a, 2); // uint16_t flags
    protocolWriteBuffer(game, cmdBase + 0x04, 8, tmp);
}

static int insertMessage(Game* game, NetMsg* msg)
{
    uint8_t size;
    int index;
    uint8_t sizes[32];

    protocolReadBuffer(game, game->apiNetAddr + 0x28, 32, sizes);

    index = -1;
    for (int i = 0; i < 32; ++i)
    {
        if (sizes[i] == 0)
        {
            index = i;
            break;
        }
    }

    if (index < 0)
        return -1;

    
    protocolWrite8(game, game->apiNetAddr + 0x28 + index, msg->size);
    protocolWrite16(game, game->apiNetAddr + 0x68 + index * 2, msg->clientId);
    if (msg->size > 0) protocolWriteBuffer(game, game->apiNetAddr + 0xa8 + index * 32, msg->size, msg->data);
    return 0;
}

static void gameApiProcessMessagesIn(Game* game)
{
    for (int i = 0; i < 128; ++i)
    {
        if (game->msg[i].size)
        {
            if (insertMessage(game, game->msg + i))
                return;
            game->msg[i].size = 0;
        }
    }
}

static void gameApiProcessMessagesOut(Game* game)
{
    uint8_t data[34];
    uint8_t size;
    uint8_t sizes[32];

    protocolReadBuffer(game, game->apiNetAddr + 0x48, 32, sizes);

    for (int i = 0; i < 32; ++i)
    {
        size = sizes[i];
        if (size)
        {
            data[0] = OP_MSG;
            data[1] = size;
            protocolReadBuffer(game, game->apiNetAddr + 0xa8 + i * 32, size, &data[2]);

            /* Send */
            netBufAppend(&game->tx, data, size + 2);

            /* Clear */
            protocolWrite8(game, game->apiNetAddr + 0x48 + i, 0x00);
        }
    }
}

static void gameApiTick(Game* game)
{
    uint8_t gameOpOut;
    uint8_t gameOpIn;

    if (game->state == STATE_INIT)
        gameLoadApiData(game);
    if (game->state == STATE_READY)
    {
        /* Handle transfers */
        gameOpOut = protocolRead8(game, game->apiNetAddr + 0x08);
        gameOpIn = protocolRead8(game, game->apiNetAddr + 0x18);
        if (game->apiError)
            return;

        if (gameOpOut == 0x02)
        {
            gameApiItemOut(game);
        }

        if (gameOpIn == 0x00)
        {
            gameApiApplyLedger(game);
        }

        gameApiProcessMessagesOut(game);
        gameApiProcessMessagesIn(game);
    }
}

static int gameProcessInputRx(Game* game, uint32_t size)
{
    int ret;

    while (game->rxBufferSize < size)
    {
        ret = recv(game->socketServer, game->rxBuffer + game->rxBufferSize, size - game->rxBufferSize, 0);
        if (ret == 0)
        {
            gameServerReconnect(game);
            return 0;
        }
        if (ret < 0)
            return 0;
        game->rxBufferSize += ret;
        game->timeout = 0;
    }

    //printf("DATA: ");
    //for (uint32_t i = 0; i < game->rxBufferSize; ++i)
    //    printf("%02x ", (uint8_t)game->rxBuffer[i]);
    //printf("\n");

    return 1;
}

static int gameProcessRxLedgerEntry(Game* game)
{
    LedgerFullEntry fe;
    uint8_t extraSize;

    /* Fetch the full header */
    if (!gameProcessInputRx(game, 10))
        return 0;

    extraSize = (uint8_t)game->rxBuffer[9];
    if (!gameProcessInputRx(game, 10 + extraSize))
        return 0;

    /* We have a full ledger entry */
    memset(&fe, 0, sizeof(fe));
    memcpy(&fe.key, game->rxBuffer + 1, 8);
    fe.size = extraSize;
    memcpy(fe.data, game->rxBuffer + 10, extraSize);

    printf("LEDGER ENTRY: %d bytes\n", extraSize);
    game->rxBufferSize = 0;

    /* Save the ledger entry */
    if (game->entriesCount == game->entriesCapacity)
    {
        game->entriesCapacity *= 2;
        game->entries = realloc(game->entries, game->entriesCapacity * sizeof(LedgerFullEntry));
    }
    memcpy(game->entries + game->entriesCount, &fe, sizeof(LedgerFullEntry));
    game->entriesCount++;

    /* Ack */
    sendqAck(&game->sendq, fe.key);

    return 1;
}

static int gameProcessRxMessage(Game* game)
{
    uint8_t size;
    uint16_t clientId;
    char data[32];

    /* Fetch the header */
    if (!gameProcessInputRx(game, 4))
        return 0;

    size = (uint8_t)game->rxBuffer[1];
    memcpy(&clientId, game->rxBuffer + 2, 2);
    if (!gameProcessInputRx(game, 4 + size))
        return 0;

    /* We have a full message entry */
    memcpy(data, game->rxBuffer + 4, size);
    game->rxBufferSize = 0;

    /* Store the message */
    for (int i = 0; i < 128; ++i)
    {
        if (game->msg[i].size == 0)
        {
            game->msg[i].size = size;
            game->msg[i].clientId = clientId;
            memcpy(game->msg[i].data, data, size);
            break;
        }
    }

    return 1;
}

static void gameProcessInput(Game* game)
{
    uint8_t op;

    for (;;)
    {
        if (game->rxBufferSize == 0)
        {
            if (!gameProcessInputRx(game, 1))
                return;
        }

        op = (uint8_t)game->rxBuffer[0];

        switch (op)
        {
        case OP_NONE:
            game->rxBufferSize = 0;
            break;
        case OP_TRANSFER:
            if (!gameProcessRxLedgerEntry(game))
                return;
            break;
        case OP_MSG:
            if (!gameProcessRxMessage(game))
                return;
            break;
        default:
            fprintf(stderr, "Unknown opcode: %02x\n", op);
            game->rxBufferSize = 0;
            break;
        }
    }
}

static void gameServerJoin(Game* game)
{
    char buf[20];
    uint32_t ledgerBase;

    ledgerBase = game->entriesCount;
    memcpy(buf, game->uuid, 16);
    memcpy(buf + 16, &ledgerBase, 4);
    if (send(game->socketServer, buf, 20, 0) != 20)
    {
        printf("Unable to send join message\n");
        gameServerReconnect(game);
        return;
    }
    game->state = STATE_READY;
}

static void gameServerConnect(App* app, Game* game)
{
    struct addrinfo hints;
    struct addrinfo* result;
    struct addrinfo* ptr;
    u_long mode;
    int ret;
    char buf[16];
    uint32_t tmp32;

    /* Resolve the server address and port */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family     = AF_UNSPEC;
    hints.ai_socktype   = SOCK_STREAM;
    hints.ai_protocol   = IPPROTO_TCP;
    snprintf(buf, sizeof(buf), "%d", app->serverPort);

    ret = getaddrinfo(app->serverHost, buf, &hints, &result);
    if (ret != 0)
    {
        printf("getaddrinfo failed: %d\n", ret);
        return;
    }

    /* Attempt to connect to an address until one succeeds */
    game->socketServer = INVALID_SOCKET;
    for (ptr = result; ptr != NULL; ptr = ptr->ai_next)
    {
        /* Create a SOCKET for connecting to server */
        game->socketServer = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (game->socketServer == INVALID_SOCKET)
            continue;

        /* Connect to server */
        ret = connect(game->socketServer, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (ret == SOCKET_ERROR)
        {
            closesocket(game->socketServer);
            game->socketServer = INVALID_SOCKET;
            continue;
        }

        /* Make the socket blocking */
        sockasync(game->socketServer, 0);

        /* Send the initial message */
        memcpy(buf, "OOMM2", 5);
        tmp32 = VERSION;
        memcpy(buf + 5, &tmp32, 4);
        if (send(game->socketServer, buf, 9, 0) != 9)
        {
            closesocket(game->socketServer);
            game->socketServer = INVALID_SOCKET;
            continue;
        }

        /* Get the initial reply */
        if (recv(game->socketServer, buf, 11, 0) != 11)
        {
            closesocket(game->socketServer);
            game->socketServer = INVALID_SOCKET;
            continue;
        }
        if (memcmp(buf, "OOMM2", 5))
        {
            closesocket(game->socketServer);
            game->socketServer = INVALID_SOCKET;
            continue;
        }
        memcpy(&game->clientId, buf + 9, 2);

        /* Make the socket non blocking */
        sockasync(game->socketServer, 1);

        break;
    }
    freeaddrinfo(result);

    if (game->socketServer == INVALID_SOCKET)
    {
        printf("Unable to connect to server at %s:%d\n", app->serverHost, app->serverPort);
        game->delay = 100;
        return;
    }

    /* Log */
    printf("Connected to server at %s:%d\n", app->serverHost, app->serverPort);
    game->state = STATE_JOIN;
    gameServerJoin(game);
}

static void gameServerTick(App* app, Game* game)
{
    if (game->apiError)
        return;

    if (game->delay)
    {
        --game->delay;
        return;
    }

    game->timeout++;
    if (game->timeout >= 1500)
    {
        gameServerReconnect(game);
        return;
    }

    switch (game->state)
    {
    case STATE_INIT:
        break;
    case STATE_CONNECT:
        gameServerConnect(app, game);
        break;
    case STATE_JOIN:
        break;
    case STATE_READY:
        /* NOP reqs */
        if (netBufIsEmpty(&game->tx) && game->nopAcc >= 100)
        {
            game->nopAcc = 0;
            netBufAppend(&game->tx, "\x00", 1);
        }
        else
            ++game->nopAcc;
        sendqTick(&game->sendq, &game->tx);
        netBufTransfer(game->socketServer, &game->tx);
        gameProcessInput(game);
        break;
    }
}

void gameTick(App* app, Game* game)
{
    LOGF("Game tick\n");
    if (apiContextLock(game))
    {
        LOGF("Game API tick\n");
        gameApiTick(game);
        apiContextUnlock(game);
    }
    gameServerTick(app, game);
    if (game->apiError)
    {
        printf("Game disconnected\n");
        gameClose(game);
    }
    LOGF("Game tick end\n");
}
