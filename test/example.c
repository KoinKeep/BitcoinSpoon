#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "Database.h"
#include "KeyManager.h"
#include "BasicStorage.h"
#include "TransactionTracker.h"
#include "Database.h"
#include "NodeManager.h"
#include "Notifications.h"

int main()
{
    DataTrackPush();

    BTCUtilStartup();
    basicStorageSetup(StringF("/tmp/bs.basicStorage"));
    bsSave("testnet", DataInt(0));
    KMInit();
    KMSetTestnet(&km, 0);

    if(KMMasterPrivKey(&km).bytes == NULL)
        KMGenerateMasterPrivKey(&km);

    tracker = TTNew(0);
    database = DatabaseNew();

    Data d = bsLoad("walletCreationTime");
    time_t walletCreationTime = DataGetLong(d);

    if(!walletCreationTime) {

        walletCreationTime = time(0);
        bsSave("walletCreationTime", DataLong(walletCreationTime));
    }

    nodeManager = NodeManagerNew(walletCreationTime);

    nodeManager.testnet = 0;

    printf("BitcoinSpoon started! Creation time was %ld\n", walletCreationTime);

    NodeManagerConnectNodes(&nodeManager);

    printf("BitcoinSpoon connecting to network..\n");

    DataTrackPop();

    while(1) {

        DataTrackPush();

        NodeManagerProcessNodes(&nodeManager);
        NotificationsProcess();

        DataTrackPop();

        usleep(1000 * 1000);
    }

    BTCUtilShutdown();
}
