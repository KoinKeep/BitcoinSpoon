#ifndef NOTIFICATIONS_H
#define NOTIFICATIONS_H

#include "WorkQueue.h"

void NotificationsFire(const char *name, Dict dict);

void NotificationsAddListener(const char *name, WorkQueueFunc func);

void NotificationsProcess();

#endif
