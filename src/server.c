#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "inc/common.h"
#include "inc/users.h"

static int server_fd = -1;

typedef struct {
    int fd;
    char ip[16];
} ClientArg;

static int recv_str(int fd, char *buf, int max) {
    int n = 0;
    char c;
    while (n < max - 1) {
        if (recv(fd, &c, 1, 0) <= 0) {
            return -1;
        }
        buf[n++] = c;
        if (c == '\0') {
            return n;
        }
    }
    buf[n] = '\0';
    return n;
}

static void send_code(int fd, uint8_t code) {
    send(fd, &code, 1, 0);
}

static int connect_to(const char *ip, uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void handle_register(int fd) {
    char name[MAX_NAME];
    if (recv_str(fd, name, sizeof(name)) < 0) return;
    int r = user_add(name);
    send_code(fd, (uint8_t)r);
    if (r == 0) {
        printf("s> REGISTER %s OK\n", name);
    } else {
        printf("s> REGISTER %s FAIL\n", name);
    }
}

static void handle_unregister(int fd) {
    char name[MAX_NAME];
    if (recv_str(fd, name, sizeof(name)) < 0) return;
    int r = user_remove(name);
    send_code(fd, (uint8_t)r);
    if (r == 0) {
        printf("s> UNREGISTER %s OK\n", name);
    } else {
        printf("s> UNREGISTER %s FAIL\n", name);
    }
}

static void handle_connect(int fd, const char *client_ip) {
    char name[MAX_NAME], port_str[16];
    if (recv_str(fd, name, sizeof(name)) < 0) return;
    if (recv_str(fd, port_str, sizeof(port_str)) < 0) return;

    uint16_t port = (uint16_t)atoi(port_str);
    int r = user_connect(name, client_ip, port);
    send_code(fd, (uint8_t)r);

    if (r == 0) {
        printf("s> CONNECT %s OK\n", name);

        pthread_mutex_lock(&users_mutex);
        int idx = user_find(name);
        while (idx != -1 && users[idx].pending) {
            Msg *m = msg_pop(idx);
            char dest_ip[16];
            uint16_t dest_port;
            strncpy(dest_ip, users[idx].ip, sizeof(dest_ip));
            dest_port = users[idx].port;
            char from_copy[MAX_NAME];
            strncpy(from_copy, m->from, MAX_NAME);
            pthread_mutex_unlock(&users_mutex);
            int cfd = connect_to(dest_ip, dest_port);
            if (cfd >= 0) {
                char id_str[32];
                snprintf(id_str, seizeof(id_str), "%u", m->id);
                send(cfd, "SEND_MESSAGE", strlen("SEND_MESSAGE") + 1, 0);
                send(cfd, from_copy, strlen(from_copy) + 1, 0);
                send(cfd, id_str, strlen(id_str) + 1, 0);
                send(cfd, m->text, strlen(m->text) + 1, 0);
                close(cfd);
                printf("s> SEND MESSAGE %u FROM %s TO %s\n", m->id, from_copy, name);
            }
            free(m);
            pthread_mutex_lock(&users_mutex);
            idx = user_find(name);
        }
        pthread_mutex_unlock(&users_mutex);
    } else {
        printf("s> CONNECT %s FAIL\n", name);
    }
}

static void sigint_handler(int sig) {
    (void)sig;
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    exit(0);
}

static char *get_local_ip(void) {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) < 0) {
        return "127.0.0.1";
    }

    struct hostent *he = gethostbyname(hostname);
    if (!he) {
        return "127.0.0.1";
    }
    return inet_ntoa(*(struct in_addr *)he->h_addr_list[0]);
}

static void *handle_client(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);

    close(client_fd);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3 || strcmp(argv[1], "-p") != 0) {
        fprintf(stderr, "Uso: %s -p <puerto>\n", argv[0]);
        return 1;
    }

    uint16_t port = (uint16_t)atoi(argv[2]);
    signal(SIGINT, sigint_handler);
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("s> init server %s:%d\n", get_local_ip(), port);
    printf("s> ");
    fflush(stdout);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (1) {
        int *client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (*client_fd < 0) {
            free(client_fd);
            break;
        }

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, client_fd);
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}