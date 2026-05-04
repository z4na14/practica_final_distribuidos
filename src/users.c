#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "sqlite/sqlite3.h"
#include "users.h"
#include "common.h"

/* Conexión global a la BD. Todo acceso pasa por db_mutex para evitar
   condiciones de carrera entre los hilos del servidor. */
static sqlite3          *db       = NULL;
static pthread_mutex_t   db_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Abre la base de datos y crea las tablas si no existen todavía.
   Devuelve 0 si OK, -1 si hubo algún error al abrir o crear las tablas. */
int db_init(void) {
    int rc = sqlite3_open_v2(DB_PATH, &db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "db_init: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    char *errmsg = NULL;

    /* Tabla de usuarios: status 0=desconectado, 1=conectado.
       msg_counter lleva la cuenta de IDs de mensajes por usuario. */
    rc = sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS users ("
        "  name        TEXT    PRIMARY KEY,"
        "  status      INTEGER NOT NULL DEFAULT 0,"
        "  ip          TEXT    NOT NULL DEFAULT '',"
        "  port        INTEGER NOT NULL DEFAULT 0,"
        "  msg_counter INTEGER NOT NULL DEFAULT 0"
        ");",
        NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "db_init (tabla users): %s\n", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }

    /* Tabla de mensajes pendientes: se guardan aquí hasta que el receptor se conecte */
    rc = sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS messages ("
        "  id       INTEGER NOT NULL,"
        "  sender   TEXT    NOT NULL,"
        "  receiver TEXT    NOT NULL,"
        "  message  TEXT    NOT NULL"
        ");",
        NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "db_init (tabla messages): %s\n", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }

    return 0;
}

/* Cierra la conexión con la BD de forma segura */
void db_close(void) {
    if (db) {
        sqlite3_close_v2(db);
        db = NULL;
    }
}

/* Registra un nuevo usuario en la BD con status desconectado.
   Devuelve 0 si OK, 1 si el nombre ya existía, 2 si hubo un error. */
int user_add(const char *name) {
    pthread_mutex_lock(&db_mutex);

    /* Primero comprobamos si el nombre ya está en uso */
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "SELECT 1 FROM users WHERE name = ?;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db_mutex);
        return 2;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_ROW) {
        pthread_mutex_unlock(&db_mutex);
        return 1; /* ya existe */
    }

    /* Si no existe, lo insertamos con todos los campos a sus valores iniciales */
    rc = sqlite3_prepare_v2(db,
        "INSERT INTO users (name, status, ip, port, msg_counter) "
        "VALUES (?, 0, '', 0, 0);",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db_mutex);
        return 2;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&db_mutex);
    return (rc == SQLITE_DONE) ? 0 : 2;
}

/* Elimina un usuario de la BD junto con todos sus mensajes pendientes.
   Devuelve 0 si OK, 1 si el usuario no existía, 2 si hubo un error. */
int user_remove(const char *name) {
    pthread_mutex_lock(&db_mutex);

    /* Comprobamos que el usuario existe antes de intentar borrarlo */
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "SELECT 1 FROM users WHERE name = ?;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db_mutex);
        return 2;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_ROW) {
        pthread_mutex_unlock(&db_mutex);
        return 1; /* no existe */
    }

    /* Borramos primero sus mensajes pendientes para no dejar huérfanos en la tabla */
    rc = sqlite3_prepare_v2(db,
        "DELETE FROM messages WHERE receiver = ?;",
        -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    rc = sqlite3_prepare_v2(db,
        "DELETE FROM users WHERE name = ?;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db_mutex);
        return 2;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&db_mutex);
    return (rc == SQLITE_DONE) ? 0 : 2;
}

/* Marca al usuario como conectado y guarda su IP y puerto de escucha.
   Devuelve 0 si OK, 1 si no existe, 2 si ya estaba conectado, 3 si hubo un error. */
int user_connect(const char *name, const char *ip, uint16_t port) {
    pthread_mutex_lock(&db_mutex);

    /* Leemos el estado actual para saber si existe y si ya estaba conectado */
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "SELECT status FROM users WHERE name = ?;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db_mutex);
        return 3;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);

    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        return 1; /* no existe */
    }
    int status = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    if (status == 1) {
        pthread_mutex_unlock(&db_mutex);
        return 2; /* ya conectado */
    }

    /* Guardamos la IP y puerto para que el servidor pueda enviarle mensajes después */
    rc = sqlite3_prepare_v2(db,
        "UPDATE users SET status = 1, ip = ?, port = ? WHERE name = ?;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db_mutex);
        return 3;
    }
    sqlite3_bind_text(stmt, 1, ip,   -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 2, (int)port);
    sqlite3_bind_text(stmt, 3, name, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&db_mutex);
    return (rc == SQLITE_DONE) ? 0 : 3;
}

/* Marca al usuario como desconectado y borra su IP y puerto de la BD.
   Devuelve 0 si OK, 1 si no existe, 2 si ya estaba desconectado, 3 si hubo un error. */
int user_disconnect(const char *name) {
    pthread_mutex_lock(&db_mutex);

    /* Leemos el estado actual para verificar que tiene sentido desconectarlo */
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "SELECT status FROM users WHERE name = ?;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db_mutex);
        return 3;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);

    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        return 1; /* no existe */
    }
    int status = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    if (status == 0) {
        pthread_mutex_unlock(&db_mutex);
        return 2; /* ya desconectado */
    }

    /* Limpiamos la IP y puerto para que nadie intente conectarse a una dirección vieja */
    rc = sqlite3_prepare_v2(db,
        "UPDATE users SET status = 0, ip = '', port = 0 WHERE name = ?;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db_mutex);
        return 3;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&db_mutex);
    return (rc == SQLITE_DONE) ? 0 : 3;
}

/* Obtiene la IP y puerto de escucha de un usuario si está conectado.
   Devuelve 0 si está conectado (y rellena ip/port), 1 si no existe, 2 si no está conectado. */
int user_get_conn_info(const char *name, char *ip, uint16_t *port) {
    pthread_mutex_lock(&db_mutex);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "SELECT status, ip, port FROM users WHERE name = ?;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db_mutex);
        return 1;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);

    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        return 1; /* no existe */
    }

    int status = sqlite3_column_int(stmt, 0);
    if (status == 1) {
        /* Solo copiamos la IP y puerto si el usuario está conectado */
        const char *stored_ip   = (const char *)sqlite3_column_text(stmt, 1);
        int         stored_port = sqlite3_column_int(stmt, 2);
        if (ip)   { strncpy(ip, stored_ip ? stored_ip : "", 15); ip[15] = '\0'; }
        if (port) { *port = (uint16_t)stored_port; }
    }
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&db_mutex);
    return (status == 1) ? 0 : 2;
}

/* Devuelve los nombres de todos los usuarios conectados en el momento de la consulta.
   Rellena el array names y devuelve cuántos hay, o -1 si hubo un error. */
int users_get_connected(char (*names)[MAX_NAME], int max) {
    pthread_mutex_lock(&db_mutex);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "SELECT name FROM users WHERE status = 1;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max) {
        const char *n = (const char *)sqlite3_column_text(stmt, 0);
        if (n) {
            strncpy(names[count], n, MAX_NAME - 1);
            names[count][MAX_NAME - 1] = '\0';
            count++;
        }
    }
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&db_mutex);
    return count;
}

/* Guarda un mensaje en la BD para el receptor y le asigna un ID único.
   El ID se obtiene incrementando msg_counter del receptor, y vuelve a 1 al llegar al máximo.
   Devuelve el ID asignado (>0) o 0 si el receptor no existe o hubo un error. */
unsigned int msg_add(const char *receiver, const char *sender, const char *text) {
    pthread_mutex_lock(&db_mutex);

    /* Incrementamos el contador de mensajes del receptor para obtener el nuevo ID */
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "UPDATE users "
        "SET msg_counter = CASE WHEN msg_counter >= 4294967295 THEN 1 "
        "                       ELSE msg_counter + 1 END "
        "WHERE name = ?;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db_mutex);
        return 0;
    }
    sqlite3_bind_text(stmt, 1, receiver, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    /* Si no se modificó ninguna fila, el receptor no existe */
    if (rc != SQLITE_DONE || sqlite3_changes(db) == 0) {
        pthread_mutex_unlock(&db_mutex);
        return 0;
    }

    /* Leemos el valor actualizado del contador para saber qué ID usar */
    rc = sqlite3_prepare_v2(db,
        "SELECT msg_counter FROM users WHERE name = ?;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db_mutex);
        return 0;
    }
    sqlite3_bind_text(stmt, 1, receiver, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        return 0;
    }
    unsigned int new_id = (unsigned int)sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);

    /* Insertamos el mensaje en la tabla con el ID recién calculado */
    rc = sqlite3_prepare_v2(db,
        "INSERT INTO messages (id, sender, receiver, message) "
        "VALUES (?, ?, ?, ?);",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db_mutex);
        return 0;
    }
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)new_id);
    sqlite3_bind_text (stmt, 2, sender,   -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 3, receiver, -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 4, text,     -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&db_mutex);
    return (rc == SQLITE_DONE) ? new_id : 0;
}

/* Lee el primer mensaje pendiente del receptor SIN borrarlo de la BD.
   Hay que llamar a msg_delete después de entregarlo correctamente.
   Devuelve 0 si encontró un mensaje, 1 si no hay ninguno, -1 si hubo un error. */
int msg_get_next(const char *receiver, unsigned int *id, char *sender, char *text) {
    pthread_mutex_lock(&db_mutex);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "SELECT id, sender, message FROM messages WHERE receiver = ? LIMIT 1;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, receiver, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);

    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        return 1; /* sin mensajes pendientes */
    }

    *id = (unsigned int)sqlite3_column_int64(stmt, 0);

    const char *s = (const char *)sqlite3_column_text(stmt, 1);
    const char *m = (const char *)sqlite3_column_text(stmt, 2);
    if (sender) { strncpy(sender, s ? s : "", MAX_NAME - 1); sender[MAX_NAME - 1] = '\0'; }
    if (text)   { strncpy(text,   m ? m : "", MAX_MSG   - 1); text[MAX_MSG   - 1]   = '\0'; }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);
    return 0;
}

/* Borra un mensaje concreto de la BD una vez que se ha entregado correctamente.
   Devuelve 0 si OK, -1 si hubo un error. */
int msg_delete(const char *receiver, unsigned int id) {
    pthread_mutex_lock(&db_mutex);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "DELETE FROM messages WHERE receiver = ? AND id = ?;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }
    sqlite3_bind_text (stmt, 1, receiver, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&db_mutex);
    return (rc == SQLITE_DONE) ? 0 : -1;
}
