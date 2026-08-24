#include "NetUtils.h"

#include <stdarg.h>
#include <stdio.h>

#define LOG_LINE_MAX_HELPER 512

void LogWriteHelper(const char* func, const char* fmt, ...)
{
    va_list args;

    fprintf(stderr, "%s(): ", func);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    fflush(stderr);
}

void LogWriteResultHelper(const char* fmt, ...)
{
    char body[LOG_LINE_MAX_HELPER];
    va_list args;

    va_start(args, fmt);
    vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);

    fprintf(stdout, "%s\n", body);
    fflush(stdout);
}

void LogWinsockErrorHelper(const char* func, const char* context)
{
    char* message = NULL;
    DWORD messageLength;
    int errorCode;

    errorCode = WSAGetLastError();
    messageLength = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
        (DWORD)errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&message, 0, NULL);

    if (messageLength != 0 && message != NULL)
    {
        while (messageLength > 0 && (message[messageLength - 1] == '\r' || message[messageLength - 1] == '\n' ||
                                     message[messageLength - 1] == '.' || message[messageLength - 1] == ' '))
        {
            --messageLength;
            message[messageLength] = '\0';
        }
        LogWriteHelper(func, "%s: %s (%d)", context, message, errorCode);
        LocalFree(message);
    }
    else
    {
        LogWriteHelper(func, "%s: winsock error %d", context, errorCode);
    }
}

SOCKET BindSocketHelper(int sockType, unsigned short port)
{
    SOCKET sock;
    BOOL reuse = TRUE;
    struct sockaddr_in addr;

    sock = socket(AF_INET, sockType, 0);
    if (sock == INVALID_SOCKET)
    {
        NET_PERROR_HELPER("socket");
        return INVALID_SOCKET;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) == SOCKET_ERROR)
    {
        NET_PERROR_HELPER("setsockopt(SO_REUSEADDR)");
        closesocket(sock);
        return INVALID_SOCKET;
    }

    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        NET_PERROR_HELPER("bind");
        closesocket(sock);
        return INVALID_SOCKET;
    }

    return sock;
}

SOCKET ConnectSocketHelper(int sockType, const char* host, unsigned short port)
{
    SOCKET sock;
    struct sockaddr_in addr;

    sock = socket(AF_INET, sockType, 0);
    if (sock == INVALID_SOCKET)
    {
        NET_PERROR_HELPER("socket");
        return INVALID_SOCKET;
    }

    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1)
    {
        LOG_MSG_HELPER("invalid address '%s'", host);
        closesocket(sock);
        return INVALID_SOCKET;
    }

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        NET_PERROR_HELPER("connect");
        closesocket(sock);
        return INVALID_SOCKET;
    }

    return sock;
}

SOCKET ListenTcpHelper(unsigned short port, int backlog)
{
    SOCKET sock;

    sock = BindSocketHelper(SOCK_STREAM, port);
    if (sock == INVALID_SOCKET)
    {
        return INVALID_SOCKET;
    }

    if (listen(sock, backlog) == SOCKET_ERROR)
    {
        NET_PERROR_HELPER("listen");
        closesocket(sock);
        return INVALID_SOCKET;
    }

    return sock;
}

int SendAllHelper(SOCKET sock, const char* sendBuf, int length)
{
    int sentTotal = 0;
    int sentBytes;

    while (sentTotal < length)
    {
        sentBytes = send(sock, sendBuf + sentTotal, length - sentTotal, 0);
        if (sentBytes == SOCKET_ERROR)
        {
            NET_PERROR_HELPER("send");
            return -1;
        }
        sentTotal += sentBytes;
    }
    return 0;
}

int FormatAddrHelper(const struct sockaddr_in* addr, char* out, size_t size)
{
    char host[INET_ADDRSTRLEN];

    if (inet_ntop(AF_INET, &addr->sin_addr, host, sizeof(host)) == NULL)
    {
        return -1;
    }
    if (snprintf(out, size, "%s:%u", host, ntohs(addr->sin_port)) < 0)
    {
        return -1;
    }
    return 0;
}

int FormatPeerHelper(SOCKET sock, char* out, size_t size)
{
    struct sockaddr_in addr;
    int addrLength = (int)sizeof(addr);

    if (getpeername(sock, (struct sockaddr*)&addr, &addrLength) == SOCKET_ERROR)
    {
        NET_PERROR_HELPER("getpeername");
        return -1;
    }
    return FormatAddrHelper(&addr, out, size);
}