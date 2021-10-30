#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <stdint.h>

struct WebserverClient;

typedef struct Webserver {

    /* Public Websever data */

    // If errorCode is nonzero, kill the webserver
    int errorCode;

    /* Private Webserver data */
    int serverSocket;

    int pendingClientCount;
    struct WebserverClient **pendingClients;

} Webserver;

typedef struct WebserverClient {

    /* Public client data */
    char *requestName;

    char **parameters;
    int parameterCount;

    /* Private Client Data */
    int clientSocket;
    char *inputBuffer;
    int inputBufferLen;
} WebserverClient;

Webserver *WebserverStart(int port);

WebserverClient *WebserverWaitForClient(Webserver *webserver, int waitMilliseconds);

void WebserverClientSendResponse(WebserverClient *client, char *response);
void WebserverClientSendResponseExplicit(WebserverClient *client, char *response, uint32_t responseLength);

void WebserverClientEnd(WebserverClient *client);

void WebserverEnd(Webserver *webserver);

#endif
