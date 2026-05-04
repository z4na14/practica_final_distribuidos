#include <stdio.h>
#include <unistd.h>

#include "users.h"
#include "common.h"

int main(void) {
    /* Borramos cualquier BD anterior para empezar desde cero */
    unlink(DB_PATH);

    if (db_init() != 0) {
        fprintf(stderr, "Error al inicializar la BD\n");
        return 1;
    }

    /* user_add
       Primera vez debe devolver 0, la segunda 1 porque ya existe */
    printf("add alice:      %d (esperado 0)\n", user_add("alice"));
    printf("add alice:      %d (esperado 1)\n", user_add("alice"));
    printf("add bob:        %d (esperado 0)\n", user_add("bob"));
    printf("add charlie:    %d (esperado 0)\n", user_add("charlie"));

    /* user_connect
       Conectar dos veces al mismo usuario debe devolver 2 la segunda vez.
       Conectar un usuario que no existe debe devolver 1. */
    printf("connect alice:  %d (esperado 0)\n", user_connect("alice",   "127.0.0.1", 9000));
    printf("connect alice:  %d (esperado 2)\n", user_connect("alice",   "127.0.0.1", 9000));
    printf("connect nadie:  %d (esperado 1)\n", user_connect("nadie",   "127.0.0.1", 9001));
    printf("connect charlie:%d (esperado 0)\n", user_connect("charlie", "127.0.0.1", 9002));

    /* users_get_connected
       Alice y charlie están conectados, bob no, así que debe devolver 2 */
    char names[MAX_USERS][MAX_NAME];
    int count = users_get_connected(names, MAX_USERS);
    printf("usuarios conectados: %d (esperado 2)\n", count);
    for (int i = 0; i < count; i++) {
        printf("  %s\n", names[i]);
    }

    /* user_get_conn_info
       Alice está conectada (0), bob existe pero no está conectado (2),
       nadie no existe en absoluto (1) */
    char ip[16];
    uint16_t port;
    printf("info alice: %d (esperado 0)\n", user_get_conn_info("alice", ip, &port));
    printf("  ip=%s port=%u\n", ip, port);
    printf("info bob:   %d (esperado 2)\n", user_get_conn_info("bob",   ip, &port));
    printf("info nadie: %d (esperado 1)\n", user_get_conn_info("nadie", ip, &port));

    /* msg_add
       Los dos primeros mensajes deben recibir IDs 1 y 2.
       El tercero falla porque el destinatario no existe y devuelve 0. */
    unsigned int id1 = msg_add("bob",   "alice", "hola bob");
    unsigned int id2 = msg_add("bob",   "alice", "segundo mensaje");
    unsigned int id3 = msg_add("nadie", "alice", "a nadie");
    printf("msg id1: %u (esperado 1)\n", id1);
    printf("msg id2: %u (esperado 2)\n", id2);
    printf("msg id3: %u (esperado 0)\n", id3);

    /* msg_get_next + msg_delete
       Leemos y borramos los dos mensajes de bob uno a uno.
       Después de borrar ambos, msg_get_next debe devolver 1 (sin mensajes). */
    unsigned int mid;
    char sender[MAX_NAME], text[MAX_MSG];

    printf("msg_get_next bob: %d (esperado 0)\n", msg_get_next("bob", &mid, sender, text));
    printf("  id=%u from='%s' text='%s'\n", mid, sender, text);
    printf("msg_delete:       %d (esperado 0)\n", msg_delete("bob", mid));

    printf("msg_get_next bob: %d (esperado 0)\n", msg_get_next("bob", &mid, sender, text));
    printf("  id=%u from='%s' text='%s'\n", mid, sender, text);
    printf("msg_delete:       %d (esperado 0)\n", msg_delete("bob", mid));

    printf("msg_get_next bob: %d (esperado 1)\n", msg_get_next("bob", &mid, sender, text));

    /* user_disconnect
       Desconectar dos veces debe devolver 2 la segunda vez.
       Desconectar un usuario que no existe debe devolver 1. */
    printf("disconnect alice:  %d (esperado 0)\n", user_disconnect("alice"));
    printf("disconnect alice:  %d (esperado 2)\n", user_disconnect("alice"));
    printf("disconnect nadie:  %d (esperado 1)\n", user_disconnect("nadie"));

    /* user_remove
       Borrar dos veces debe devolver 1 la segunda vez porque ya no existe. */
    printf("remove bob:     %d (esperado 0)\n", user_remove("bob"));
    printf("remove bob:     %d (esperado 1)\n", user_remove("bob"));
    printf("remove alice:   %d (esperado 0)\n", user_remove("alice"));
    printf("remove charlie: %d (esperado 0)\n", user_remove("charlie"));

    /* Cerramos la BD y borramos el archivo para no dejar rastro */
    db_close();
    unlink(DB_PATH);
    return 0;
}
