/*
 * Interfaz de gestión de usuarios y mensajes sobre SQLite.
 */
#ifndef USERS_H
#define USERS_H

#include <stdint.h>
#include "common.h"

/* Inicializa la base de datos SQLite (crea las tablas si no existen).
 * Devuelve 0 si OK, -1 si error. */
int  db_init(void);

/* Cierra la conexión con la base de datos. */
void db_close(void);

/*
 * Gestión de usuarios.
 *
 *   user_add        : 0=ok, 1=ya existe, 2=error
 *   user_remove     : 0=ok, 1=no existe, 2=error
 *   user_connect    : 0=ok, 1=no existe, 2=ya conectado, 3=error
 *   user_disconnect : 0=ok, 1=no existe, 2=no conectado, 3=error
 *   user_get_conn_info: rellena ip/port si conectado.
 *                       0=conectado, 1=no existe, 2=no conectado
 *   users_get_connected: devuelve número de usuarios conectados (≥0) o -1 si error
 */
int user_add(const char *name);
int user_remove(const char *name);
int user_connect(const char *name, const char *ip, uint16_t port);
int user_disconnect(const char *name);
int user_get_conn_info(const char *name, char *ip, uint16_t *port);
int users_get_connected(char (*names)[MAX_NAME], int max);

/*
 * Gestión de mensajes.
 *
 *   msg_add      : devuelve el id asignado (>0) o 0 si error/destinatario no existe
 *   msg_get_next : 0=encontrado (rellena id/sender/text), 1=sin mensajes, -1=error
 *   msg_delete   : 0=ok, -1=error
 */
unsigned int msg_add(const char *receiver, const char *sender, const char *text, const char *filename);
int msg_get_next(const char *receiver, unsigned int *id, char *sender, char *text, char *filename);
int msg_delete(const char *receiver, unsigned int id);

#endif /* USERS_H */
