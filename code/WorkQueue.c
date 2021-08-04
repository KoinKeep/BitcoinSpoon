#include "WorkQueue.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif

#define LOCK(lock) if(pthread_mutex_lock(&self->lock) != 0) abort()
#define UNLOCK(lock) if(pthread_mutex_unlock(&self->lock) != 0) abort()

typedef struct WorkQueueItem {

    void (*function)(Dict dict);
    Dict dict;
    uint64_t executeTimeMilliseconds;

} WorkQueueItem;

static void *workQueueExecute(void *ptr)
{
    WorkQueue *self = ptr;

    while(!self->waitingToDestroy) {

        WorkQueueWaitUntilNotEmpty(self);
        WorkQueueExecuteAll(self);
    }

    LOCK(executionLock);

    self->waitingToDestroy = 0;

    if(pthread_cond_broadcast(&self->waitCondition) != 0)
        abort();

    UNLOCK(executionLock);

    return NULL;
}

static Dict threadQueues = { 0 };
static pthread_mutex_t threadQueuesLock = PTHREAD_MUTEX_INITIALIZER;

static void threadProcessingInit()
{
    threadQueues = DictUntrack(DictNew());
}

WorkQueue *WorkQueueThreadNamedStackSize(const char *name, int stackSize)
{
    static pthread_once_t onceToken = PTHREAD_ONCE_INIT;

    pthread_once(&onceToken, threadProcessingInit);

    if(pthread_mutex_lock(&threadQueuesLock) != 0)
        abort();

    Data result = DictGetS(threadQueues, name);

    if(!result.bytes) {

        WorkQueue queue = WorkQueueNew();

        result = DataUntrack(DataCopy(&queue, sizeof(queue)));

        pthread_t workQueueThread = { 0 };

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, stackSize);

        if(pthread_create(&workQueueThread, &attr, workQueueExecute, result.bytes))
            abort();

        threadQueues = DictionaryAddRef(threadQueues, StringNew(name), result);

        threadQueues = DictUntrack(threadQueues);
    }

    if(pthread_mutex_unlock(&threadQueuesLock) != 0)
        abort();

    return (WorkQueue*)result.bytes;
}

WorkQueue *WorkQueueThreadNamed(const char *name)
{
    return WorkQueueThreadNamedStackSize(name, 32768);
}

void WorkQueueThreadWaitAndDestroy(const char *name)
{
    WorkQueue *self = WorkQueueThreadNamed(name);

    LOCK(executionLock);

    self->waitingToDestroy = 1;

    if(pthread_cond_broadcast(&self->waitCondition) != 0)
        abort();

    while(self->waitingToDestroy)
        if(pthread_cond_wait(&self->waitCondition, &self->executionLock) != 0)
            abort();

    UNLOCK(executionLock);

    if(pthread_mutex_lock(&threadQueuesLock) != 0)
        abort();

    WorkQueueFree(*self);
    DictRemoveS(&threadQueues, name);

    // If thread count goes down to 0, this will free all the memory we allocated
    if(!DictCount(threadQueues)) {

        DictionaryFree(threadQueues);
        threadQueues = DictUntrack(DictNew()); // Empty Dicts don't allocate any memory.
    }

    if(pthread_mutex_unlock(&threadQueuesLock) != 0)
        abort();
}

WorkQueue WorkQueueNew()
{
    WorkQueue self = {0};

    self.queue = DatasUntrack(DatasNew());

    if(pthread_mutex_init(&self.queueLock, 0) != 0)
        abort();

    pthread_mutexattr_t attr;

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

    if(pthread_mutex_init(&self.executionLock, &attr) != 0)
        abort();

    if(pthread_cond_init(&self.waitCondition, 0) != 0)
        abort();

    self.waitingToDestroy = 0;

    return self;
}

void WorkQueueFree(WorkQueue queue)
{
    WorkQueue *self = &queue;

    LOCK(queueLock);

    self->queue = DatasFree(self->queue);

    UNLOCK(queueLock);

    pthread_mutex_destroy(&self->queueLock);
    pthread_mutex_destroy(&self->executionLock);
    pthread_cond_destroy(&self->waitCondition);
}

void WorkQueueAdd(WorkQueue *self, void (*function)(Dict dict), Dict dict)
{
    WorkQueueAddDelayed(self,function, dict, 0);
}

void WorkQueueAddDelayed(WorkQueue *self, void (*function)(Dict dict), Dict dict, uint64_t delayMillisecods)
{
    WorkQueueItem item = { function, DictUntrackCopy(dict), delayMillisecods };

    LOCK(queueLock);

    if(delayMillisecods) {

        struct timeval tv;

        gettimeofday(&tv, NULL);

        item.executeTimeMilliseconds += tv.tv_sec * 1000;
        item.executeTimeMilliseconds += tv.tv_usec / 1000;
    }

    self->queue = DatasUntrack(DatasAddCopy(self->queue, DataRaw(item)));

    UNLOCK(queueLock);

    if(pthread_cond_broadcast(&self->waitCondition) != 0)
        abort();
}

void WorkQueueRemove(WorkQueue *self, int (*removalTest)(void *ptr, WorkQueueFunc func, Dict dict), void *ptr)
{
    LOCK(queueLock);

    for(int i = 0; i < self->queue.count; i++) {

        WorkQueueItem *item = (WorkQueueItem*)DatasIndex(self->queue, i).bytes;

        if(removalTest(ptr, item->function, item->dict)) {

            DictionaryFree(item->dict);
            self->queue = DatasRemoveIndex(self->queue, i);

            i--;
        }
    }

    UNLOCK(queueLock);

    LOCK(executionLock);

    if(pthread_cond_broadcast(&self->waitCondition) != 0)
        abort();

    UNLOCK(executionLock);
}

static int functionRemovalTest(void *ptr, WorkQueueFunc func, Dict dict)
{
    return ptr == func ? 1 : 0;
}

void WorkQueueRemoveByFunction(WorkQueue *self, WorkQueueFunc func)
{
    WorkQueueRemove(self, functionRemovalTest, func);
}

static Data WorkQueueGetOne(WorkQueue *self)
{
    Data result = DataNull();

    LOCK(queueLock);

    struct timeval tv;
    gettimeofday(&tv, NULL);

    uint64_t milliseconds = tv.tv_sec * 1000 + tv.tv_usec / 1000;

    for(int i = 0; i < self->queue.count; i++) {

        WorkQueueItem *item = (WorkQueueItem*)DatasIndex(self->queue, i).bytes;

        if(item->executeTimeMilliseconds <= milliseconds) {

            result = DatasIndex(self->queue, i);
            self->queue = DatasRemoveIndexTake(self->queue, i);

            break;
        }
    }

    UNLOCK(queueLock);

    return DataTrack(result);
}

void WorkQueueExecuteAll(WorkQueue *self)
{
    LOCK(executionLock);

    DataTrackPush();

    Data data = DataNull();

    while((data = WorkQueueGetOne(self)).bytes) {

        WorkQueueItem *item = (void*)data.bytes;

        DataTrackPush();
        DictTrack(item->dict);

        item->function(item->dict);

        DataTrackPop();
    }

    DataTrackPop();

    if(pthread_cond_broadcast(&self->waitCondition) != 0)
        abort();

    UNLOCK(executionLock);
}

void WorkQueueWaitUntilEmpty(WorkQueue *self)
{
    LOCK(executionLock);

    while(1) {

        LOCK(queueLock);

        int queueSize = self->queue.count;

        UNLOCK(queueLock);

        if(!queueSize)
            break;

        if(pthread_cond_wait(&self->waitCondition, &self->executionLock) != 0)
            abort();
    }

    UNLOCK(executionLock);
}

void WorkQueueWaitUntilNotEmpty(WorkQueue *self)
{
    LOCK(executionLock);

    while(1) {

        LOCK(queueLock);

        int readyCount = self->queue.count;

        (void)readyCount;

        uint64_t lowestExecuteTime = UINT64_MAX;

        for(int i = 0; i < self->queue.count; i++) {

            WorkQueueItem *item = (WorkQueueItem*)DatasIndex(self->queue, i).bytes;

            lowestExecuteTime = MIN(lowestExecuteTime, item->executeTimeMilliseconds);
        }

        int waitingToDestroy = self->waitingToDestroy;

        struct timeval tv;
        gettimeofday(&tv, NULL);

        uint64_t milliseconds = tv.tv_sec * 1000 + tv.tv_usec / 1000;

        UNLOCK(queueLock);

        if(waitingToDestroy || lowestExecuteTime <= milliseconds)
            break;

        tv.tv_sec += lowestExecuteTime / 1000;
        tv.tv_usec += lowestExecuteTime % 1000;

        struct timespec ts;

        ts.tv_sec = tv.tv_sec;
        ts.tv_nsec = tv.tv_usec * 1000;

        int result = 0;

        if(lowestExecuteTime == UINT64_MAX)
            result = pthread_cond_wait(&self->waitCondition, &self->executionLock);
        else
            result = pthread_cond_timedwait(&self->waitCondition, &self->executionLock, &ts);

        if(result && result != ETIMEDOUT)
            abort();
    }

    UNLOCK(executionLock);
}
