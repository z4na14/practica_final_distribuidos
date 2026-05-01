#ifndef USERS_H
#define USERS_H

#include <stdint.h>
#include "common.h"

enum MSG_CODES {
    MESSAGE    = 0, // Used just for the answer
    REGISTER   = 1,
    UNREGISTER = 2,
    CONNECT    = 3,
    DISCONNECT = 4,
    SEND       = 5,
    SENDATTACH = 6,
    USERS      = 7
};

int user_find(const char *name);
int user_add(const char *name);
int user_remove(const char *name);
int user_connect(const char *name, const char *ip, uint16_t port);
int user_disconnect(const char *name);
unsigned int msg_add(int idx, const char *from, const char *text);
Msg *msg_pop(int idx);

#endif