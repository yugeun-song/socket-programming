#include "NetUtils.h"

#define PORT_HELPER 5000
#define BACKLOG_HELPER 8
#define BUFFER_SIZE_HELPER 1024

int main(void)
{
    char logBuf[ADDR_TEXT_MAX_HELPER];
    char recvBuf[BUFFER_SIZE_HELPER];
    struct sockaddr_in clientAddr;
    SOCKET listenSock;
    SOCKET clientSock;
    int addrLength;
    int receivedBytes;

    if (NetStartupHelper() != 0)
    {
        return 1;
    }

    listenSock = ListenTcpHelper(PORT_HELPER, BACKLOG_HELPER);
    if (listenSock == INVALID_SOCKET)
    {
        NetCleanupHelper();
        return 1;
    }

    LOG_MSG_HELPER("listening on port %d", PORT_HELPER);

    addrLength = (int)sizeof(clientAddr);
    clientSock = accept(listenSock, (struct sockaddr*)&clientAddr, &addrLength);
    if (clientSock == INVALID_SOCKET)
    {
        NET_PERROR_HELPER("accept");
        closesocket(listenSock);
        NetCleanupHelper();
        return 1;
    }

    if (FormatAddrHelper(&clientAddr, logBuf, sizeof(logBuf)) != 0)
    {
        closesocket(clientSock);
        closesocket(listenSock);
        NetCleanupHelper();
        return 1;
    }

    LOG_MSG_HELPER("accepted %s", logBuf);

    for (;;)
    {
        receivedBytes = recv(clientSock, recvBuf, (int)sizeof(recvBuf), 0);
        if (receivedBytes > 0)
        {
            LOG_RESULT_HELPER("received %d bytes from %s", receivedBytes, logBuf);
            if (SendAllHelper(clientSock, recvBuf, receivedBytes) != 0)
            {
                break;
            }
        }
        else if (receivedBytes == 0)
        {
            LOG_MSG_HELPER("%s closed the connection", logBuf);
            break;
        }
        else
        {
            NET_PERROR_HELPER("recv");
            break;
        }
    }

    closesocket(clientSock);
    closesocket(listenSock);
    NetCleanupHelper();
    return 0;
}