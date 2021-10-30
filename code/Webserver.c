#include "Webserver.h"

#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <time.h>
#include <ctype.h>

#define SINGLE_READ_BUFFER (10 * 1024)
#define MAX_ALLOWED_BUFFER_BACKLOG (10 * 1024 * 1024)

Webserver *WebserverStart(int port)
{
    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    char portStr[1024];

    sprintf(portStr, "%d", port);

    int result = getaddrinfo(NULL, portStr, &hints, &res);

    if(result != 0) {

        printf("Webserver getaddrinfo fails %s\n", gai_strerror(result));
        
        return NULL;
    }

    Webserver *webserver = malloc(sizeof(Webserver));

    webserver->pendingClientCount = 0;
    webserver->pendingClients = 0;

    webserver->serverSocket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    int optval = 1;

    setsockopt(webserver->serverSocket, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

    bind(webserver->serverSocket, res->ai_addr, res->ai_addrlen);

    listen(webserver->serverSocket, 10);

    freeaddrinfo(res);

    return webserver;
}

// Returns 0 on success and errno otherwise
static int ProcessAccept(Webserver *webserver)
{
    int ptrSize = sizeof(WebserverClient*);

    int result = accept(webserver->serverSocket, NULL, 0);

    if(result < 0)
        return errno;

    WebserverClient *client = malloc(sizeof(WebserverClient));

    memset(client, 0, sizeof(WebserverClient));

    client->clientSocket = result;

    int newCount = ++webserver->pendingClientCount;

    webserver->pendingClients = reallocf(webserver->pendingClients, ptrSize * newCount);

    webserver->pendingClients[newCount - 1] = client;

    return 0;
}

// Return 0 on success
static int ProcessClientRead(WebserverClient *client)
{
    uint8_t buffer[SINGLE_READ_BUFFER];

    ssize_t result = recv(client->clientSocket, buffer, sizeof(buffer), 0);

    if(result > 0) {

        if(client->inputBufferLen + result > MAX_ALLOWED_BUFFER_BACKLOG) {

            return 105; // ENOBUFS
        }
        else {

            int oldLen = client->inputBufferLen;

            client->inputBufferLen += result;

            client->inputBuffer = reallocf(client->inputBuffer, client->inputBufferLen);

            memcpy(client->inputBuffer + oldLen, buffer, result);
        }
    }

    if(result == 0) {

        return -1;
    }

    if(result == -1) {

        return errno;
    }

    return 0;
}

static void RemoveClient(Webserver *webserver, WebserverClient *client)
{
    int count = webserver->pendingClientCount;
    int ptrSize = sizeof(WebserverClient*);

    for(int i = 0; i < count; i++) {

        if(webserver->pendingClients[i] != client)
            continue;

        int rightBytes = (count - (i + 1)) * ptrSize;

        memmove(&webserver->pendingClients[i], &webserver->pendingClients[i + 1], rightBytes);

        count--;

        webserver->pendingClients = reallocf(webserver->pendingClients, count * ptrSize);

        webserver->pendingClientCount = count;
    }
}

// Request URI ends with the first space encountered
static char *RequestURI(char *in, int len)
{
    char *firstSpace = strnstr(in, " ", len);

    if(firstSpace + 1 > in + len)
        return NULL;

    return firstSpace + 1;
}

static int hexToI(char c)
{
    if(c >= '0' && c <= '9')
        return c - '0';

    c = tolower(c);

    if(c >= 'a' && c <= 'f')
        return 10 + c - 'a';

    return -1;
}

static void cleanURISpaces(char *str)
{
    char *end = str + strlen(str);

    for(char *p = str; p < end; p++) {

        if(*p == '+') {

            *p = ' ';
        }
        else if(*p == '%' && p + 2 < end) {

            *p = hexToI(p[2]) | (hexToI(p[1]) << 4);

            memmove(p + 1, p + 3, (end - p) - 2);
        }
    }
}

static void PrepareWebserverClient(WebserverClient *client)
{
    char *in = client->inputBuffer;
    int len = client->inputBufferLen;

    char *uri = RequestURI(in, len);

    if(!uri)
        return;

    char *start = NULL;

    for(char *p = uri; p && p < in + len; p++) {

        if(*p == '/' || *p == ' ') {

            if(start) {

                char *dest = NULL;
                int len = p - start + 1;

                if(!client->requestName) {
                    
                    dest = client->requestName = malloc(len);
                }
                else {

                    client->parameterCount++;

                    client->parameters = reallocf(client->parameters, client->parameterCount * sizeof(char*));

                    dest = client->parameters[client->parameterCount - 1] = malloc(len);
                }

                strncpy(dest, start, p - start);

                dest[len - 1] = 0;

                cleanURISpaces(dest);

                start = NULL;
            }
        }
        else if(!start) {

            start = p;
        }

        if(*p == ' ')
            break;
    }
}

static WebserverClient *FirstReadyClient(Webserver *webserver)
{
    for(int i = 0; i < webserver->pendingClientCount; i++) {

        WebserverClient *client = webserver->pendingClients[i];

        if(client->inputBuffer && strnstr(client->inputBuffer, "\r\n\r\n", client->inputBufferLen)) {

            client->inputBufferLen++;

            client->inputBuffer = reallocf(client->inputBuffer, client->inputBufferLen);

            client->inputBuffer[client->inputBufferLen - 1] = 0;

            RemoveClient(webserver, client);

            PrepareWebserverClient(client);

            return client;
        }
    }

    return NULL;
}

WebserverClient *WebserverWaitForClient(Webserver *webserver, int waitMilliseconds)
{
    WebserverClient *candidate = FirstReadyClient(webserver);

    if(candidate)
        return candidate;

    fd_set read, write, except;

    FD_ZERO(&read);
    FD_ZERO(&write);
    FD_ZERO(&except);

    int maxfd = webserver->serverSocket;

    FD_SET(webserver->serverSocket, &read);
    FD_SET(webserver->serverSocket, &except);

    for(int i = 0; i < webserver->pendingClientCount; i++) {

        int fd = webserver->pendingClients[i]->clientSocket;

        FD_SET(fd, &read);
        FD_SET(fd, &except);

        if(fd > maxfd)
            maxfd = fd;
    }

    struct timeval waitTime = { 0 };

    waitTime.tv_sec = waitMilliseconds / 1000;
    waitTime.tv_usec = (waitMilliseconds % 1000) * 1000;

    int activity = select(maxfd + 1, &read, &write, &except, &waitTime);

    switch(activity) {
        case 0:
            if(waitMilliseconds)
                break;

            perror("Webserver::select() shouldn't return 0");
            webserver->errorCode = errno;
            break;

        case -1:
            perror("Webserver::select()");
            webserver->errorCode = errno;
            break;

        default:

            if(FD_ISSET(webserver->serverSocket, &read)) {

                ProcessAccept(webserver);
            }

            if(FD_ISSET(webserver->serverSocket, &except)) {

                 // Arbitrary error number is chosen incase errno is empty
                 // 77 is EBADFD, file descriptor in bad state
                webserver->errorCode = errno ?: 77;
            }

            for(int i = 0; i < webserver->pendingClientCount; i++) {

                WebserverClient *client = webserver->pendingClients[i];

                int fd = webserver->pendingClients[i]->clientSocket;

                if(FD_ISSET(webserver->serverSocket, &read)) {

                    if(0 != ProcessClientRead(client)) {

                        printf("Removing client %d due to read error.\n", i);

                        WebserverClientEnd(client);

                        RemoveClient(webserver, client);
                        i--;
                    }
                }

                if(FD_ISSET(webserver->serverSocket, &except)) {

                    printf("Removing client %d due to exception.\n", i);

                    WebserverClientEnd(client);

                    RemoveClient(webserver, client);
                    i--;
                }
            }
    }

    return FirstReadyClient(webserver);
}

void WebserverClientSendResponse(WebserverClient *client, char *response)
{
    return WebserverClientSendResponseExplicit(client, response, strlen(response));
}

void WebserverClientSendResponseExplicit(WebserverClient *client, char *response, uint32_t responseLength)
{
    const char *headerFmt =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=UTF-8\r\n"
    "Content-Length: %d\r\n"
    "\r\n";

    char header[strlen(headerFmt) + 1024];

    snprintf(header, sizeof(header), headerFmt, (int)responseLength);

    int len = strlen(header) + responseLength;

    char buffer[len];

    strcpy(buffer, header);

    memcpy(buffer + strlen(header), response, responseLength);

    char *end = buffer + len;

    for(char *p = buffer; p < end; ) {

        ssize_t result = send(client->clientSocket, p, end - p, 0);

        printf("Sent %d bytes out of %d\n", (int)result, len);

        if(result < 0) {

            printf("We had an error while sending %s\n", strerror(errno));
            break;
        }

        p += result;
    }
}

void WebserverClientEnd(WebserverClient *client)
{
    free(client->requestName);

    for(int i = 0; i < client->parameterCount; i++)
        free(client->parameters[i]);

    free(client->parameters);

    free(client->inputBuffer);

    close(client->clientSocket);

    memset(client, 0xaa, sizeof(WebserverClient));
}

void WebserverEnd(Webserver *webserver)
{
    for(int i = 0; i < webserver->pendingClientCount; i++)
        free(webserver->pendingClients[i]);

    free(webserver->pendingClients);

    close(webserver->serverSocket);

    free(webserver);
}
