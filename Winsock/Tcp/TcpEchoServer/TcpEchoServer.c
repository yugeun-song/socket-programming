#include "NetUtils.h"

#define PORT 5000
#define BACKLOG 8
#define BUFFER_SIZE 1024

int main(void)
{
    if (NetStartup() != 0)
    {
        return 1;
    }

    SOCKET listenSock = ListenTcp(PORT, BACKLOG);
    if (listenSock == INVALID_SOCKET)
    {
        NetCleanup();
        return 1;
    }

    LOG_MSG("listening on port %d", PORT);

    SOCKET clientSock = accept(listenSock, NULL, NULL);
    if (clientSock == INVALID_SOCKET)
    {
        NET_PERROR("accept");
        closesocket(listenSock);
        NetCleanup();
        return 1;
    }

    char buffer[BUFFER_SIZE];
    for (;;)
    {
        int received = recv(clientSock, buffer, (int)sizeof(buffer), 0);
        if (received > 0)
        {
            if (SendAll(clientSock, buffer, received) != 0)
            {
                break;
            }
        }
        else if (received == 0)
        {
            break;
        }
        else
        {
            NET_PERROR("recv");
            break;
        }
    }

    closesocket(clientSock);
    closesocket(listenSock);
    NetCleanup();
    return 0;
}