#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

void LogWrite(const char* func, const char* fmt, ...);
void LogWinsockError(const char* func, const char* context);

#define LOG_MSG(...) LogWrite(__FUNCTION__, __VA_ARGS__)
#define NET_PERROR(context) LogWinsockError(__FUNCTION__, (context))

static __inline int NetStartup(void)
{
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData);
}

static __inline void NetCleanup(void)
{
    WSACleanup();
}

SOCKET BindSocket(int type, unsigned short port);
SOCKET ConnectSocket(int type, const char* host, unsigned short port);

SOCKET ListenTcp(unsigned short port, int backlog);

static __inline SOCKET ConnectTcp(const char* host, unsigned short port)
{
    return ConnectSocket(SOCK_STREAM, host, port);
}

static __inline SOCKET BindUdp(unsigned short port)
{
    return BindSocket(SOCK_DGRAM, port);
}

static __inline SOCKET ConnectUdp(const char* host, unsigned short port)
{
    return ConnectSocket(SOCK_DGRAM, host, port);
}

int SendAll(SOCKET sock, const char* buffer, int length);

int SetNonBlocking(SOCKET sock, int enable);