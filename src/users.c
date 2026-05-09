#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <pthread.h>

#include "sqlite/sqlite3.h"
#include "users.h"
#include "common.h"

// db_mutex serializa todos los accesos; SQLite no es thread-safe por defecto
static sqlite3 *db = NULL;
static pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;

int db_init(void) {
    int rc = sqlite3_open_v2(DB_PATH, &db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "db_init: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    char *errmsg = NULL;

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

    rc = sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS messages ("
        "  id       INTEGER NOT NULL,"
        "  sender   TEXT    NOT NULL,"
        "  receiver TEXT    NOT NULL,"
        "  message  TEXT    NOT NULL,"
        "  filename TEXT    DEFAULT NULL"
        ");",
        NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "db_init (tabla messages): %s\n", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }

    return 0;
}

void db_close(void) {
    if (db) {
        sqlite3_close_v2(db); // v2 no falla si quedan statements sin finalizar
        db = NULL;
    }
}

int user_add(const char *name) {
    pthread_mutex_lock(&db_mutex);

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
        return 1;
    }

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

int user_remove(const char *name) {
    pthread_mutex_lock(&db_mutex);

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
        return 1;
    }

    // borramos sus mensajes antes que al usuario para no dejar huérfanos
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

int user_connect(const char *name, const char *ip, uint16_t port) {
    pthread_mutex_lock(&db_mutex);

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
        return 1; // no existe
    }
    int status = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    if (status == 1) {
        pthread_mutex_unlock(&db_mutex);
        return 2;
    }

    rc = sqlite3_prepare_v2(db,
        "UPDATE users SET status = 1, ip = ?, port = ? WHERE name = ?;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db_mutex);
        return 3;
    }
    sqlite3_bind_text(stmt, 1, ip, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, (int)port);
    sqlite3_bind_text(stmt, 3, name, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&db_mutex);
    return (rc == SQLITE_DONE) ? 0 : 3;
}

int user_disconnect(const char *name) {
    pthread_mutex_lock(&db_mutex);

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
        return 1; // no existe
    }
    int status = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    if (status == 0) {
        pthread_mutex_unlock(&db_mutex);
        return 2;
    }

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
        return 1; // no existe
    }

    int status = sqlite3_column_int(stmt, 0);
    if (status == 1) {
        const char *stored_ip = (const char *)sqlite3_column_text(stmt, 1);
        int stored_port = sqlite3_column_int(stmt, 2);
        if (ip) { strncpy(ip, stored_ip ? stored_ip : "", 15); ip[15] = '\0'; }
        if (port) { *port = (uint16_t)stored_port; }
    }
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&db_mutex);
    return (status == 1) ? 0 : 2;
}

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

unsigned int msg_add(const char *receiver, const char *sender, const char *text, const char *filename) {
    pthread_mutex_lock(&db_mutex);

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

    // si changes()==0 el receptor no existe
    if (rc != SQLITE_DONE || sqlite3_changes(db) == 0) {
        pthread_mutex_unlock(&db_mutex);
        return 0;
    }

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

    rc = sqlite3_prepare_v2(db,
        "INSERT INTO messages (id, sender, receiver, message, filename) "
        "VALUES (?, ?, ?, ?, ?);",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db_mutex);
        return 0;
    }
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)new_id);
    sqlite3_bind_text(stmt, 2, sender, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, receiver, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, text, -1, SQLITE_STATIC);
    
    if (filename && strlen(filename) > 0) {
        sqlite3_bind_text(stmt, 5, filename, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 5);
    }
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&db_mutex);
    return (rc == SQLITE_DONE) ? new_id : 0;
}

int msg_get_next(const char *receiver, unsigned int *id, char *sender, char *text, char *filename) {
    pthread_mutex_lock(&db_mutex);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db,
        "SELECT id, sender, message, filename FROM messages WHERE receiver = ? LIMIT 1;",
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
        return 1;
    }

    *id = (unsigned int)sqlite3_column_int64(stmt, 0);

    const char *s = (const char *)sqlite3_column_text(stmt, 1);
    const char *m = (const char *)sqlite3_column_text(stmt, 2);
    const char *f = (const char *)sqlite3_column_text(stmt, 3);

    if (sender) { strncpy(sender, s ? s : "", MAX_NAME - 1); sender[MAX_NAME - 1] = '\0'; }
    if (text) { strncpy(text, m ? m : "", MAX_MSG - 1); text[MAX_MSG - 1] = '\0'; }
    if (filename) { strncpy(filename, f ? f : "", MAX_MSG - 1); filename[MAX_MSG - 1] = '\0'; }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);
    return 0;
}

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
    sqlite3_bind_text(stmt, 1, receiver, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&db_mutex);
    return (rc == SQLITE_DONE) ? 0 : -1;
}
