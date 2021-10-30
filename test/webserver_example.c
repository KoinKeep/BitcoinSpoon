#include "Webserver.h"
#include "Data.h"
#include "BTCUtil.h"
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#define PORT_NUMBER 1024

static int shouldQuit = 0;

static void sigint(int ignoreme)
{
    (void)ignoreme;

    shouldQuit = 1;
}

int main()
{
    signal(SIGINT, sigint);

    DataTrackPush();

    BTCUtilStartup();

    Webserver *server = WebserverStart(PORT_NUMBER);

    if(!server)
        printf("Webserver failed to start.\n");

    while(server && !shouldQuit) {

        DataTrackPush();

        WebserverClient *client = WebserverWaitForClient(server, 1000);

        if(!client)
            continue;

        if(!strcmp(client->requestName, "PBKDF2") && client->parameterCount >= 1) {

            char *sentence = client->parameters[0];
            char *passphrase = client->parameterCount > 1 ? client->parameters[1] : NULL;

            Data result = PBKDF2(sentence, passphrase);

            WebserverClientSendResponse(client, toHex(result).bytes);
        }
        else {

            String str = StringF("Unrecognized function '%s'", client->requestName ?: "");

            WebserverClientSendResponse(client, str.bytes);
        }

        WebserverClientEnd(client);

        DataTrackPop();
    }

    WebserverEnd(server);

    BTCUtilShutdown();

    DataTrackPop();
}
