#include "NetUtils.h"

#include <string.h>

#define PORT_HELPER 5000
#define BUFFER_SIZE_HELPER 1024

int main(int argc, char** argv)
{
    const char* const host = (argc > 1) ? argv[1] : "127.0.0.1";
    const char* const message = (argc > 2) ? argv[2] : "hello, winsock";
    char logBuf[ADDR_TEXT_MAX_HELPER];
    char recvBuf[BUFFER_SIZE_HELPER];
    SOCKET sock;
    int receivedTotal = 0;
    int receivedBytes;
    int messageLength;
    int chunkSize;
    int roomLeft;

    if (NetStartupHelper() != 0)
    {
        return 1;
    }

    sock = ConnectTcpHelper(host, PORT_HELPER);
    if (sock == INVALID_SOCKET)
    {
        NetCleanupHelper();
        return 1;
    }

    if (FormatPeerHelper(sock, logBuf, sizeof(logBuf)) != 0)
    {
        closesocket(sock);
        NetCleanupHelper();
        return 1;
    }

    LOG_MSG_HELPER("connected to %s", logBuf);

    messageLength = (int)strlen(message);
    if (SendAllHelper(sock, message, messageLength) != 0)
    {
        closesocket(sock);
        NetCleanupHelper();
        return 1;
    }

    while (receivedTotal < messageLength && receivedTotal < (int)sizeof(recvBuf) - 1)
    {
        roomLeft = (int)sizeof(recvBuf) - 1 - receivedTotal;
        chunkSize = (messageLength - receivedTotal < roomLeft) ? (messageLength - receivedTotal) : roomLeft;
        receivedBytes = recv(sock, recvBuf + receivedTotal, chunkSize, 0);
        if (receivedBytes > 0)
        {
            receivedTotal += receivedBytes;
        }
        else if (receivedBytes == 0)
        {
            break;
        }
        else
        {
            NET_PERROR_HELPER("recv");
            break;
        }
    }
    recvBuf[receivedTotal] = '\0';
    LOG_RESULT_HELPER("received '%s' (%d bytes) from %s", recvBuf, receivedTotal, logBuf);

    closesocket(sock);
    NetCleanupHelper();
    return 0;
}