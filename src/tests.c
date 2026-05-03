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

    printf("add alice:      %d (esperado 0)\n", user_add("alice"));
    printf("add alice:      %d (esperado 1)\n", user_add("alice")); /* ya existe */
    printf("add bob:        %d (esperado 0)\n", user_add("bob"));
    printf("add charlie:    %d (esperado 0)\n", user_add("charlie"));

    printf("connect alice:  %d (esperado 0)\n", user_connect("alice",   "127.0.0.1", 9000));
    printf("connect alice:  %d (esperado 2)\n", user_connect("alice",   "127.0.0.1", 9000)); /* ya conectado */
    printf("connect nadie:  %d (esperado 1)\n", user_connect("nadie",   "127.0.0.1", 9001)); /* no existe */
    printf("connect charlie:%d (esperado 0)\n", user_connect("charlie", "127.0.0.1", 9002));

    char names[MAX_USERS][MAX_NAME];
    int count = users_get_connected(names, MAX_USERS);
    printf("usuarios conectados: %d (esperado 2)\n", count);
    for (int i = 0; i < count; i++)
        printf("  %s\n", names[i]);

    char ip[16];
    uint16_t port;
    printf("info alice: %d (esperado 0)\n", user_get_conn_info("alice",   ip, &port));
    printf("  ip=%s port=%u\n", ip, port);
    printf("info bob:   %d (esperado 2)\n", user_get_conn_info("bob",     ip, &port)); /* no conectado */
    printf("info nadie: %d (esperado 1)\n", user_get_conn_info("nadie",   ip, &port)); /* no existe */

    unsigned int id1 = msg_add("bob", "alice", "hola bob");
    unsigned int id2 = msg_add("bob", "alice", "segundo mensaje");
    unsigned int id3 = msg_add("nadie", "alice", "a nadie"); /* destinatario no existe */
    printf("msg id1: %u (esperado 1)\n", id1);
    printf("msg id2: %u (esperado 2)\n", id2);
    printf("msg id3: %u (esperado 0)\n", id3); /* 0 = error */

    unsigned int mid;
    char sender[MAX_NAME], text[MAX_MSG];

    printf("msg_get_next bob: %d (esperado 0)\n", msg_get_next("bob", &mid, sender, text));
    printf("  id=%u from='%s' text='%s'\n", mid, sender, text);
    printf("msg_delete:       %d (esperado 0)\n", msg_delete("bob", mid));

    printf("msg_get_next bob: %d (esperado 0)\n", msg_get_next("bob", &mid, sender, text));
    printf("  id=%u from='%s' text='%s'\n", mid, sender, text);
    printf("msg_delete:       %d (esperado 0)\n", msg_delete("bob", mid));

    printf("msg_get_next bob: %d (esperado 1)\n", msg_get_next("bob", &mid, sender, text)); /* sin mensajes */

    printf("disconnect alice:  %d (esperado 0)\n", user_disconnect("alice"));
    printf("disconnect alice:  %d (esperado 2)\n", user_disconnect("alice")); /* no conectado */
    printf("disconnect nadie:  %d (esperado 1)\n", user_disconnect("nadie")); /* no existe */

    printf("remove bob:     %d (esperado 0)\n", user_remove("bob"));
    printf("remove bob:     %d (esperado 1)\n", user_remove("bob"));    /* no existe */
    printf("remove alice:   %d (esperado 0)\n", user_remove("alice"));
    printf("remove charlie: %d (esperado 0)\n", user_remove("charlie"));

    db_close();
    unlink(DB_PATH);
    return 0;
}
