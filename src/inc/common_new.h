#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <pthread.h>

#define MAX_USERS 256 // 2^8
#define MAX_MESSAGES 512 // Lenght of queue before dropping packages
#define MAX_NAME 256
#define MAX_MSG 256

typedef struct {
    uint32_t mid; // MSG ID
    uint16_t uid_sender; // USER ID
    uint16_t uid_receiver; // USER ID
    char contents[MAX_MSG];
} Msg;

typedef struct {
    char name[MAX_NAME];
    uint8_t status;
    char ip[16];
    uint16_t port;
} User;

/* User status codes:
 * - 0: Disconnected
 * - 1: Connected
 * - 2: Deleted
 *
 * Deleted users are bound to be cleaned after X time (Not implemented),
 * and unable to be logged into.
 */

extern Msg message_queue[MAX_MESSAGES];
extern uint16_t message_queue_index;
extern pthread_mutex_t messages_mutex;

extern User connected_users[MAX_USERS];
extern uint8_t connected_users_count;
extern pthread_mutex_t users_mutex;

#endif //COMMON_H
