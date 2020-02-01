#ifndef WORKQUEUE_H
#define WORKQUEUE_H

#include "Data.h"
#include <pthread.h>

typedef struct WorkQueue {

    Datas queue;
    pthread_mutex_t queueLock, executionLock;
    pthread_cond_t waitCondition;

    int waitingToDestroy;

} WorkQueue;

// For each unique value of "name", a new thread and work queue is created.
// On subsequent calls with the same "name", the previously created work queue is returned
WorkQueue *WorkQueueThreadNamed(const char *name);
WorkQueue *WorkQueueThreadNamedStackSize(const char *name, int stackSize);

// Waits for the WorkQueue thread to finish up and destroys it.
void WorkQueueThreadWaitAndDestroy(const char *name);

// WorkQueues are created Untracked by default.
WorkQueue WorkQueueNew();

void WorkQueueFree(WorkQueue queue);

typedef void (*WorkQueueFunc)(Dict dict);

void WorkQueueAdd(WorkQueue *queue, WorkQueueFunc function, Dict dict);

void WorkQueueAddDelayed(WorkQueue *queue, WorkQueueFunc function, Dict dict, uint64_t delayMillisecods);

void WorkQueueRemove(WorkQueue *queue, int (*removalTest)(void *ptr, WorkQueueFunc func, Dict dict), void *ptr);

void WorkQueueRemoveByFunction(WorkQueue *queue, WorkQueueFunc func);

void WorkQueueExecuteAll(WorkQueue *queue);

void WorkQueueWaitUntilEmpty(WorkQueue *queue);

// Not empty means things with 0 executeTime or executeTime in the past
void WorkQueueWaitUntilNotEmpty(WorkQueue *queue);

#endif
