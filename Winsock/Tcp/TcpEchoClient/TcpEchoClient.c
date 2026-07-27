#include "NetUtils.h"

#include <string.h>

#define PORT 5000
#define BUFFER_SIZE 1024

int main(int argc, char** argv)
{
    const char* host = (argc > 1) ? argv[1] : "127.0.0.1";
    const char* message = (argc > 2) ? argv[2] : "hello, winsock";

    if (NetStartup() != 0)
    {
        return 1;
    }

    SOCKET sock = ConnectTcp(host, PORT);
    if (sock == INVALID_SOCKET)
    {
        NetCleanup();
        return 1;
    }

    int want = (int)strlen(message);
    if (SendAll(sock, message, want) != 0)
    {
        closesocket(sock);
        NetCleanup();
        return 1;
    }

    char buffer[BUFFER_SIZE];
    int total = 0;
    while (total < want && total < (int)sizeof(buffer) - 1)
    {
        int room = (int)sizeof(buffer) - 1 - total;
        int chunk = (want - total < room) ? (want - total) : room;
        int received = recv(sock, buffer + total, chunk, 0);
        if (received > 0)
        {
            total += received;
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
    buffer[total] = '\0';
    LOG_MSG("received '%s'", buffer);

    closesocket(sock);
    NetCleanup();
    return 0;
}