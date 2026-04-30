#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include "inc/users.h"

User users[MAX_USERS];
int nusers = 0;
pthread_mutex_t users_mutex = PTHREAD_MUTEX_INITIALIZER;

int user_find(const char *name) {
    // Devuelve el índice del usuario o -1 si no existe
    for (int i = 0; i < nusers; i++) {
        if (strcmp(users[i].name, name) == 0) {
            return i;
        } 
    }
    return -1;
}

int user_add(const char *name) {
    pthread_mutex_lock(&users_mutex);
    if (user_find(name) != -1) {
        pthread_mutex_unlock(&users_mutex);
        return 1;       // Si devuelve 1, el usuario ya existe
    }
    if (nusers >= MAX_USERS) {
        pthread_mutex_unlock(&users_mutex);
        return 2;       // Devuelve 2 en caso de error
    }

    strncpy(users[nusers].name, name, MAX_NAME - 1);
    users[nusers].connected = 0;
    users[nusers].ip[0] = '\0';
    users[nusers].port = 0;
    users[nusers].pending = NULL;
    users[nusers].msg_counter = 0;
    nusers++;

    pthread_mutex_unlock(&users_mutex);
    return 0;
}

int user_remove(const char *name) {
    pthread_mutex_lock(&users_mutex);
    int idx = user_find(name);
    if (idx == -1) {
        pthread_mutex_unlock(&users_mutex);
        return 1;       // Devuelve 1 si no existe el usuario
    }

    // Liberar mensajes pendientes
    Msg *m = users[idx].pending;
    while(m) {
        Msg *next = m -> next;
        free(m);
        m = next;
    }
    
    // Reducir el array de usuarios
    users[idx] = users[nusers - 1];
    nusers--;

    pthread_mutex_unlock(&users_mutex);
    return 0;
}

int user_connect(const char *name, const char *ip, uint16_t port) {
    pthread_mutex_lock(&users_mutex);
    int idx = user_find(name);
    if (idx == -1) {
        pthread_mutex_unlock(&users_mutex);
        return 1;       // Devuelve 1 si no existe el usuario
    }
    if (users[idx].connected) {
        pthread_mutex_unlock(&users_mutex);
        return 2;       // Devuelve 2 si el usuario ya está conectado
    }

    strncpy(users[idx].ip, ip, sizeof(users[idx].ip) - 1);
    users[idx].port = port;
    users[idx].connected = 1;

    pthread_mutex_unlock(&users_mutex);
    return 0;
}

int user_disconnect(const char *name) {
    pthread_mutex_lock(&users_mutex);
    int idx = user_find(name);

    if (idx == -1) {
        pthread_mutex_unlock(&users_mutex);
        return 1;           // Devuelve 1 si no existe el usuario
    }
    if (!users[idx].connected) {
        pthread_mutex_unlock(&users_mutex);
        return 2;           // Devuelve 2 si no está conectado
    }

    users[idx].connected = 0;
    users[idx].ip[0] = '\0';
    users[idx].port = 0;

    pthread_mutex_unlock(&users_mutex);
    return 0;
}

unsigned int msg_add(int idx, const char *from, const char *text) {
    Msg *m = malloc(sizeof(Msg));
    if (!m) return 0;

    users[idx].msg_counter++;
    if (users[idx].msg_counter == 0) {
        users[idx].msg_counter = 1;
    }

    m->id = users[idx].msg_counter;
    strncpy(m->from, from, MAX_NAME -1);
    strncpy(m->text, text, MAX_MSG - 1);
    m->next = NULL;
    
    if (!users[idx].pending) {
        users[idx].pending = m;
    } else {
        Msg *cur = users[idx].pending;
        while (cur->next) {
            cur = cur-> next;
        }
        cur->next = m;
    }

    return m->id;
}

Msg *msg_pop(int idx) {
    if (!users[idx].pending) {
        return NULL;
    }
    Msg *m = users[idx].pending;
    users[idx].pending = m->next;
    m->next = NULL;
    return m;
}