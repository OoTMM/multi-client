#include "multi.h"
#include <signal.h>

int appInit(App* app)
{
    WSADATA wsaData;

    /* Init WS2 */
    if (WSAStartup(MAKEWORD(2, 2), &wsaData))
    {
        printf("WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }

    app->socket = INVALID_SOCKET;
    for (int i = 0; i < MAX_GAMES; ++i)
        app->games[i].valid = 0;

    return 0;
}

int appListen(App* app, const char* host, uint16_t port)
{
    SOCKET sock;
    struct addrinfo hints;
    struct addrinfo* result;
    struct addrinfo* ptr;
    int ret;
    char buf[16];

    /* Resolve the host and port */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family     = AF_UNSPEC;
    hints.ai_socktype   = SOCK_STREAM;
    hints.ai_protocol   = IPPROTO_TCP;

    sprintf(buf, "%d", port);
    ret = getaddrinfo(host, buf, &hints, &result);
    sock = INVALID_SOCKET;
    for (ptr = result; ptr != NULL; ptr = ptr->ai_next)
    {
        /* Create a SOCKET for connecting to server */
        sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (sock == INVALID_SOCKET)
            continue;

        /* Setup the TCP listening socket */
        ret = bind(sock, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (ret == SOCKET_ERROR)
        {
            closesocket(sock);
            sock = INVALID_SOCKET;
            continue;
        }

        /* Listen to the socket */
        ret = listen(sock, SOMAXCONN);
        if (ret == SOCKET_ERROR)
        {
            closesocket(sock);
            sock = INVALID_SOCKET;
            continue;
        }

        /* We have a good socket */
        break;
    }
    freeaddrinfo(result);
    if (sock == INVALID_SOCKET)
    {
        fprintf(stderr, "Unable to listen to %s:%d\n", host, port);
        return 1;
    }

    /* Set the socket to non-blocking */
#ifdef _WIN32
    u_long mode = 1;
    ret = ioctlsocket(sock, FIONBIO, &mode);
#else
    int mode = fcntl(sock, F_GETFL, 0);
    ret = fcntl(sock, F_SETFL, mode | O_NONBLOCK);
#endif
    if (ret == SOCKET_ERROR)
    {
        fprintf(stderr, "Unable to set socket to non-blocking\n");
        closesocket(sock);
        return 1;
    }

    app->socket = sock;
    printf("Listening to %s:%d\n", host, port);
    return 0;
}

static void appAccept(App* app)
{
    SOCKET sock;
    u_long mode;

    /* Accept the connection */
    sock = accept(app->socket, NULL, NULL);
    if (sock == INVALID_SOCKET)
        return;

    /* Make the socket blocking */
#ifdef _WIN32
    mode = 0;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    mode = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, mode & ~O_NONBLOCK);
#endif

    /* Create game */
    for (int i = 0; i < MAX_GAMES; ++i)
    {
        Game* g;

        g = &app->games[i];
        if (g->valid)
            continue;
        gameInit(g, sock);
        return;
    }

    /* No game available */
    closesocket(sock);
}

static void appTryAccept(App* app)
{
    fd_set readfds;
    int ret;
    TIMEVAL timeout;

    /* Setup the timeout */
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    for (;;)
    {
        /* Setup the selector */
        FD_ZERO(&readfds);
        FD_SET(app->socket, &readfds);

        /* Select */
        ret = select(0, &readfds, NULL, NULL, &timeout);
        if (ret < 1)
            break;
        appAccept(app);
    }
}

static sig_atomic_t sSignaled = 0;

static void sigHandler(int signum)
{
    (void)signum;
    sSignaled = 1;
}

int appRun(App* app, const char *host, uint16_t port)
{
    /* Set host and port */
    app->serverHost = host;
    app->serverPort = port;

    /* Set signal handlers */
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    for (;;)
    {
        if (sSignaled)
            break;
        appTryAccept(app);
        for (int i = 0; i < MAX_GAMES; ++i)
        {
            Game* g;

            g = &app->games[i];
            if (!g->valid)
                continue;
            gameTick(app, g);
        }
        Sleep(10);
    }
    return 0;
}

int appQuit(App* app)
{
    /* Close the socket */
    if (app->socket != INVALID_SOCKET)
    {
        closesocket(app->socket);
        app->socket = INVALID_SOCKET;
    }

    /* Cleanup WS2 */
    WSACleanup();

    return 0;
}
