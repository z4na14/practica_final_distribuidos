#include <stdio.h>
#include <unistd.h>

#include "users.h"
#include "common.h"

int main(void) {
    unlink(DB_PATH);

    if (db_init() != 0) {
        fprintf(stderr, "Error al inicializar la BD\n");
        return 1;
    }

    // user_add
    printf("add alice: %d (esperado 0)\n", user_add("alice"));
    printf("add alice: %d (esperado 1)\n", user_add("alice"));
    printf("add bob: %d (esperado 0)\n", user_add("bob"));
    printf("add charlie: %d (esperado 0)\n", user_add("charlie"));

    // user_connect
    printf("connect alice: %d (esperado 0)\n", user_connect("alice", "127.0.0.1", 9000));
    printf("connect alice: %d (esperado 2)\n", user_connect("alice", "127.0.0.1", 9000));
    printf("connect nadie: %d (esperado 1)\n", user_connect("nadie", "127.0.0.1", 9001));
    printf("connect charlie: %d (esperado 0)\n", user_connect("charlie", "127.0.0.1", 9002));

    // users_get_connected
    char names[MAX_USERS][MAX_NAME];
    int count = users_get_connected(names, MAX_USERS);
    printf("usuarios conectados: %d (esperado 2)\n", count);
    for (int i = 0; i < count; i++) {
        printf("  %s\n", names[i]);
    }

    // user_get_conn_info
    char ip[16];
    uint16_t port;
    printf("info alice: %d (esperado 0)\n", user_get_conn_info("alice", ip, &port));
    printf("  ip=%s port=%u\n", ip, port);
    printf("info bob: %d (esperado 2)\n", user_get_conn_info("bob", ip, &port));
    printf("info nadie: %d (esperado 1)\n", user_get_conn_info("nadie", ip, &port));

    // msg_add
    unsigned int id1 = msg_add("bob", "alice", "hola bob", NULL);
    unsigned int id2 = msg_add("bob", "alice", "segundo mensaje", NULL);
    unsigned int id3 = msg_add("nadie", "alice", "a nadie", NULL);
    printf("msg id1: %u (esperado 1)\n", id1);
    printf("msg id2: %u (esperado 2)\n", id2);
    printf("msg id3: %u (esperado 0)\n", id3);

    // msg_get_next + msg_delete
    unsigned int mid;
    char sender[MAX_NAME], text[MAX_MSG];

    printf("msg_get_next bob: %d (esperado 0)\n", msg_get_next("bob", &mid, sender, text, NULL));
    printf("  id=%u from='%s' text='%s'\n", mid, sender, text);
    printf("msg_delete: %d (esperado 0)\n", msg_delete("bob", mid));

    printf("msg_get_next bob: %d (esperado 0)\n", msg_get_next("bob", &mid, sender, text, NULL));
    printf("  id=%u from='%s' text='%s'\n", mid, sender, text);
    printf("msg_delete: %d (esperado 0)\n", msg_delete("bob", mid));

    printf("msg_get_next bob: %d (esperado 1)\n", msg_get_next("bob", &mid, sender, text, NULL));

    // user_disconnect
    printf("disconnect alice: %d (esperado 0)\n", user_disconnect("alice"));
    printf("disconnect alice: %d (esperado 2)\n", user_disconnect("alice"));
    printf("disconnect nadie: %d (esperado 1)\n", user_disconnect("nadie"));

    // user_remove
    printf("remove bob: %d (esperado 0)\n", user_remove("bob"));
    printf("remove bob: %d (esperado 1)\n", user_remove("bob"));
    printf("remove alice: %d (esperado 0)\n", user_remove("alice"));
    printf("remove charlie: %d (esperado 0)\n", user_remove("charlie"));

    db_close();
    unlink(DB_PATH);
    return 0;
}
