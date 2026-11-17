#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define ADDR_TEXT_MAX_HELPER 32

void LogWriteHelper(const char* const func, const char* const fmt, ...);
void LogWinsockErrorHelper(const char* const func, const char* const context);
void LogWriteResultHelper(const char* const fmt, ...);

#define LOG_MSG_HELPER(...) LogWriteHelper(__FUNCTION__, __VA_ARGS__)
#define NET_PERROR_HELPER(context) LogWinsockErrorHelper(__FUNCTION__, (context))
#define LOG_RESULT_HELPER(...) LogWriteResultHelper(__VA_ARGS__)

static __inline int NetStartupHelper(void)
{
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData);
}

static __inline void NetCleanupHelper(void)
{
    WSACleanup();
}

SOCKET BindSocketHelper(int sockType, unsigned short port);
SOCKET ConnectSocketHelper(int sockType, const char* const host, unsigned short port);

SOCKET ListenTcpHelper(unsigned short port, int backlog);

static __inline SOCKET ConnectTcpHelper(const char* const host, unsigned short port)
{
    return ConnectSocketHelper(SOCK_STREAM, host, port);
}

static __inline SOCKET BindUdpHelper(unsigned short port)
{
    return BindSocketHelper(SOCK_DGRAM, port);
}

static __inline SOCKET ConnectUdpHelper(const char* const host, unsigned short port)
{
    return ConnectSocketHelper(SOCK_DGRAM, host, port);
}

int SendAllHelper(SOCKET sock, const char* const sendBuf, int length);

int FormatAddrHelper(const struct sockaddr_in* const addr, char* const out, size_t size);
int FormatPeerHelper(SOCKET sock, char* const out, size_t size);