#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include<pthread.h>

#define MAX_USERS 50
#define MAX_NAME 256
#define MAX_MSG 256

typedef struct msg {
    unsigned int id;
    char from[MAX_NAME];
    char text[MAX_MSG];
    struct msg *next;
} Msg;

typedef struct {
    char name[MAX_NAME];
    int connected;
    char ip[16];
    uint16_t port;
    Msg *pending;
    unsigned int msg_counter;
} User;

extern User users[MAX_USERS];
extern int nusers;
extern pthread_mutex_t users_mutex;

#endif //COMMON_H