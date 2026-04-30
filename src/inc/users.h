#ifndef USERS_H
#define USERS_H

#include <stdint.h>
#include "common.h"

int user_find(const char *name);
int user_add(const char *name);
int user_remove(const char *name);
int user_connect(const char *name, const char *ip, uint16_t port);
int user_disconnect(const char *name);
unsigned int msg_add(int idx, const char *from, const char *text);
Msg *msg_pop(int idx);

#endif