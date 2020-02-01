#include "Notifications.h"
#include <pthread.h>

static struct {

    pthread_mutex_t mutex;
    pthread_once_t once;
    Dict funcs;
    WorkQueue workQueue;

} note = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_ONCE_INIT, {0}, {0} };

static void init()
{
    note.funcs = DictUntrack(DictNew());
    note.workQueue = WorkQueueNew();
}

void NotificationsFire(const char *name, Dict dict)
{
    pthread_once(&note.once, init);

    pthread_mutex_lock(&note.mutex);

    Datas *datas = (Datas*)DictGetS(note.funcs, name).bytes;

    if(datas)
        FORDATAIN(data, *datas)
            WorkQueueAdd(&note.workQueue, DataGetPtr(*data), dict);

    pthread_mutex_unlock(&note.mutex);
}

void NotificationsAddListener(const char *name, WorkQueueFunc func)
{
    pthread_once(&note.once, init);

    pthread_mutex_lock(&note.mutex);

    Datas datas = DataGetDatas(DictGetS(note.funcs, name));

    datas = DatasAddRef(datas, DataPtr(func));

    DictSetS(&note.funcs, name, DataDatas(datas));

    DictUntrack(note.funcs);

    pthread_mutex_unlock(&note.mutex);
}

void NotificationsProcess()
{
    pthread_once(&note.once, init);

    WorkQueueExecuteAll(&note.workQueue);
}
