#ifndef USERS_H
#define USERS_H

#include <stdint.h>
#include "common.h"

int  db_init(void);
void db_close(void);

// 0=ok, 1=ya existe, 2=error
int user_add(const char *name);
// 0=ok, 1=no existe, 2=error
int user_remove(const char *name);
// 0=ok, 1=no existe, 2=ya conectado, 3=error
int user_connect(const char *name, const char *ip, uint16_t port);
// 0=ok, 1=no existe, 2=no conectado, 3=error
int user_disconnect(const char *name);
// rellena ip/port si conectado; 0=conectado, 1=no existe, 2=no conectado
int user_get_conn_info(const char *name, char *ip, uint16_t *port);
// devuelve el numero de conectados o -1 si error
int users_get_connected(char (*names)[MAX_NAME], int max);

// devuelve el id asignado o 0 si error
unsigned int msg_add(const char *receiver, const char *sender, const char *text, const char *filename);
// 0=hay mensaje (rellena los campos), 1=cola vacia, -1=error
int msg_get_next(const char *receiver, unsigned int *id, char *sender, char *text, char *filename);
int msg_delete(const char *receiver, unsigned int id);

#endif /* USERS_H */
