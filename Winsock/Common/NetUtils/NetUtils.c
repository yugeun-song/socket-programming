#include "NetUtils.h"

#include <stdarg.h>
#include <stdio.h>

void LogWrite(const char* func, const char* fmt, ...)
{
    va_list args;

    fprintf(stderr, "%s(): ", func);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    fflush(stderr);
}

void LogWinsockError(const char* func, const char* context)
{
    int error = WSAGetLastError();
    char* message = NULL;

    DWORD length = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                      FORMAT_MESSAGE_IGNORE_INSERTS,
                                  NULL, (DWORD)error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                  (LPSTR)&message, 0, NULL);

    if (length != 0 && message != NULL)
    {
        while (length > 0 && (message[length - 1] == '\r' || message[length - 1] == '\n' ||
                              message[length - 1] == '.' || message[length - 1] == ' '))
        {
            message[--length] = '\0';
        }
        LogWrite(func, "%s: %s (%d)", context, message, error);
        LocalFree(message);
    }
    else
    {
        LogWrite(func, "%s: winsock error %d", context, error);
    }
}

SOCKET BindSocket(int type, unsigned short port)
{
    SOCKET sock;
    BOOL reuse = TRUE;
    struct sockaddr_in addr;

    sock = socket(AF_INET, type, 0);
    if (sock == INVALID_SOCKET)
    {
        NET_PERROR("socket");
        return INVALID_SOCKET;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) == SOCKET_ERROR)
    {
        NET_PERROR("setsockopt(SO_REUSEADDR)");
        closesocket(sock);
        return INVALID_SOCKET;
    }

    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        NET_PERROR("bind");
        closesocket(sock);
        return INVALID_SOCKET;
    }

    return sock;
}

SOCKET ConnectSocket(int type, const char* host, unsigned short port)
{
    SOCKET sock;
    struct sockaddr_in addr;

    sock = socket(AF_INET, type, 0);
    if (sock == INVALID_SOCKET)
    {
        NET_PERROR("socket");
        return INVALID_SOCKET;
    }

    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1)
    {
        LOG_MSG("invalid address '%s'", host);
        closesocket(sock);
        return INVALID_SOCKET;
    }

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        NET_PERROR("connect");
        closesocket(sock);
        return INVALID_SOCKET;
    }

    return sock;
}

SOCKET ListenTcp(unsigned short port, int backlog)
{
    SOCKET sock = BindSocket(SOCK_STREAM, port);
    if (sock == INVALID_SOCKET)
    {
        return INVALID_SOCKET;
    }

    if (listen(sock, backlog) == SOCKET_ERROR)
    {
        NET_PERROR("listen");
        closesocket(sock);
        return INVALID_SOCKET;
    }

    return sock;
}

int SendAll(SOCKET sock, const char* buffer, int length)
{
    int total = 0;
    while (total < length)
    {
        int sent = send(sock, buffer + total, length - total, 0);
        if (sent == SOCKET_ERROR)
        {
            NET_PERROR("send");
            return -1;
        }
        total += sent;
    }
    return 0;
}

int SetNonBlocking(SOCKET sock, int enable)
{
    u_long mode = enable ? 1 : 0;
    if (ioctlsocket(sock, FIONBIO, &mode) == SOCKET_ERROR)
    {
        NET_PERROR("ioctlsocket(FIONBIO)");
        return -1;
    }
    return 0;
}