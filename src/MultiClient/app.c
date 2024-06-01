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

    app->socketPj64 = INVALID_SOCKET;
    app->socketAres = INVALID_SOCKET;
    for (int i = 0; i < MAX_GAMES; ++i)
        app->games[i].valid = 0;

    return 0;
}

int appStartPj64(App* app, const char* host, uint16_t port)
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
    ret = sockasync(sock, 1);

    if (ret == SOCKET_ERROR)
    {
        fprintf(stderr, "Unable to set socket to non-blocking\n");
        closesocket(sock);
        return 1;
    }

    app->socketPj64 = sock;
    return 0;
}

int appStartAres(App* app, const char* host, uint16_t port)
{
    int ret;
    SOCKET sock;
    struct addrinfo hints;
    struct addrinfo* result;
    struct addrinfo* ptr;
    char buf[16];

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Unable to create socket\n");
        return 1;
    }

    /* Set the socket to non-blocking */
    ret = sockasync(sock, 1);

    if (ret == SOCKET_ERROR)
    {
        fprintf(stderr, "Unable to set socket to non-blocking\n");
        closesocket(sock);
        return 1;
    }

    /* Resolve the host and port */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family     = AF_UNSPEC;
    hints.ai_socktype   = SOCK_STREAM;
    hints.ai_protocol   = IPPROTO_TCP;

    sprintf(buf, "%d", port);
    ret = getaddrinfo(host, buf, &hints, &result);
    if (ret != 0)
    {
        fprintf(stderr, "Unable to resolve %s:%d: %s\n", host, port, strerror(WSAGetLastError()));
        closesocket(sock);
        return 1;
    }

    if (result == NULL) {
        fprintf(stderr, "Unable to resolve to %s:%d\n", host, port);
        closesocket(sock);
        return 1;
    }

    /* Connect to the server */
    for (ptr = result; ptr != NULL; ptr = ptr->ai_next)
    {
        ret = connect(sock, ptr->ai_addr, (int)ptr->ai_addrlen);
        printf("[ares] connect: %d %d %s %p\n", ret, WSAGetLastError(), strerror(WSAGetLastError()), ptr);
        if (ret == 0 || (ret == SOCKET_ERROR && (WSAGetLastError() == WSAEINPROGRESS || WSAGetLastError() == WSAEWOULDBLOCK))) {
            printf("[ares] connection in progress...\n");
            break;
        }
    }

    if (ptr == NULL) {
        fprintf(stderr, "Unable to connect to %s:%d\n", host, port);
        closesocket(sock);
        return 1;
    }

    app->socketAres = sock;
    return 0;
}

static void appGameInit(App* app, SOCKET sock, int apiProtocol)
{
    /* Create game */
    for (int i = 0; i < MAX_GAMES; ++i)
    {
        Game* g;

        g = &app->games[i];
        if (g->valid)
            continue;
        gameInit(g, sock, apiProtocol);
        return;
    }

    /* No game available */
    closesocket(sock);
}

static void appCheckPj64(App* app)
{
    SOCKET sock;
    u_long mode;
    fd_set readfds;
    int ret;
    TIMEVAL timeout;

    /* Setup the timeout */
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    /* Setup the selector */
    FD_ZERO(&readfds);
    FD_SET(app->socketPj64, &readfds);

    /* Select */
    ret = select(app->socketPj64+1, &readfds, NULL, NULL, &timeout);
    if (ret < 1)
        return;


    /* Accept the connection */
    sock = accept(app->socketPj64, NULL, NULL);
    if (sock == INVALID_SOCKET)
        return;

    /* Make the socket blocking */
    sockasync(sock, 0);

    appGameInit(app, sock, PROTOCOL_PJ64);
}

static void appCheckAres(App* app)
{
    fd_set writefds;
    int ret;
    int error; unsigned len;
    TIMEVAL timeout;
    SOCKET sock;
    u_long mode;

    if (app->socketAres == INVALID_SOCKET) {
        appStartAres(app, "localhost", 9123);
        if (app->socketAres == INVALID_SOCKET)
            return;
    }

    /* Setup the timeout */
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    /* Setup the selector */
    FD_ZERO(&writefds);
    FD_SET(app->socketAres, &writefds);

    /* Select */
    ret = select(app->socketAres+1, NULL, &writefds, NULL, &timeout);
    if (ret < 1)
        return;

    printf("[ares] connection completed\n");
    error = 0;
    len = sizeof(error);
    if (getsockopt(app->socketAres, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {        
        fprintf(stderr, "[ares] connection failed: %d %s\n", error, strerror(error));
        closesocket(app->socketAres);
        app->socketAres = INVALID_SOCKET;
        return;
    }

    /* Make the socket blocking */
    sock = app->socketAres;
    app->socketAres = INVALID_SOCKET;

#ifdef _WIN32
    mode = 0;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    mode = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, mode & ~O_NONBLOCK);
#endif

    appGameInit(app, sock, PROTOCOL_ARES);
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
        appCheckPj64(app);
        appCheckAres(app);
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
    if (app->socketPj64 != INVALID_SOCKET)
    {
        closesocket(app->socketPj64);
        app->socketPj64 = INVALID_SOCKET;
    }
    if (app->socketAres != INVALID_SOCKET)
    {
        closesocket(app->socketAres);
        app->socketAres = INVALID_SOCKET;
    }

    /* Cleanup WS2 */
    WSACleanup();

    return 0;
}
