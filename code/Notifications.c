#include "Notifications.h"
#include <pthread.h>

static struct {

    pthread_mutex_t mutex;
    pthread_once_t once;
    Dict funcs;
    WorkQueue workQueue;
    Datas/*String*/ queuedNotificationNames;

} note = { {0}, PTHREAD_ONCE_INIT, {0}, {0}, {0} };

static void init()
{
    pthread_mutexattr_t recursiveAttr;

    pthread_mutexattr_init(&recursiveAttr);
    pthread_mutexattr_settype(&recursiveAttr, PTHREAD_MUTEX_RECURSIVE);
    
    if(pthread_mutex_init(&note.mutex, &recursiveAttr) != 0)
        abort();
    
    if(pthread_mutexattr_destroy(&recursiveAttr) != 0)
        abort();
    
    note.funcs = DictUntrack(DictNew());
    note.workQueue = WorkQueueNew();
}

void NotificationsFire(const char *name, Dict dict)
{
    pthread_once(&note.once, init);

    pthread_mutex_lock(&note.mutex);

    note.queuedNotificationNames = DatasUntrack(DatasAddCopy(note.queuedNotificationNames, StringNew(name)));

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
    NotificationsProcessReturningEventNames();
}

Datas NotificationsProcessReturningEventNames()
{
    pthread_once(&note.once, init);

    pthread_mutex_lock(&note.mutex);

    WorkQueueExecuteAll(&note.workQueue);

    Datas result = DatasTrack(note.queuedNotificationNames);

    note.queuedNotificationNames = DatasUntrack(DatasNew());

    pthread_mutex_unlock(&note.mutex);

    return result;
}
