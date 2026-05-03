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

#include "common.h"
#include "users.h"

static int server_fd = -1;

typedef struct {
    int  fd;
    char ip[16];
} ClientArg;

/* * The server must act proactively and open a new socket to the target's
 * IP and port to deliver messages or ACKs.
 */
static int conn_deliver(const char *receiver, const char *sender,
                        unsigned int id, const char *text) {
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%u", id);

    char msg[MAX_NAME + MAX_MSG + 64];
    int  mlen = snprintf(msg, sizeof(msg), "SEND_MESSAGE#%s#%s#%s",
                         sender, id_str, text);
    if (mlen < 0 || mlen >= (int)sizeof(msg)) return -1;
    msg[mlen] = '\0';

    char recv_ip[16];
    uint16_t recv_port;
    if (user_get_conn_info(receiver, recv_ip, &recv_port) != 0) {
        return -1; // Receiver is not connected
    }

    // 1. Connect to receiver's listening thread
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(recv_port);
    addr.sin_addr.s_addr = inet_addr(recv_ip);

    if (connect(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        user_disconnect(receiver); // Drop user if unreachable
        close(sfd);
        return -1;
    }

    ssize_t sent = send(sfd, msg, (size_t)(mlen + 1), 0);
    close(sfd);

    if (sent <= 0) {
        user_disconnect(receiver);
        return -1;
    }

    printf("s> SEND MESSAGE %u FROM %s TO %s\n", id, sender, receiver);
    msg_delete(receiver, id);

    // 2. Send ACK to the sender's listening thread if they are connected
    char ack[64];
    int  alen = snprintf(ack, sizeof(ack), "SEND_MESS_ACK#%s", id_str);
    if (alen > 0 && alen < (int)sizeof(ack)) {
        ack[alen] = '\0';

        char sender_ip[16];
        uint16_t sender_port;
        if (user_get_conn_info(sender, sender_ip, &sender_port) == 0) {
            int ack_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (ack_fd >= 0) {
                struct sockaddr_in saddr;
                memset(&saddr, 0, sizeof(saddr));
                saddr.sin_family      = AF_INET;
                saddr.sin_port        = htons(sender_port);
                saddr.sin_addr.s_addr = inet_addr(sender_ip);

                if (connect(ack_fd, (struct sockaddr *)&saddr, sizeof(saddr)) == 0) {
                    send(ack_fd, ack, (size_t)(alen + 1), 0);
                }
                close(ack_fd);
            }
        }
    }
    return 0;
}

static void send_code(int fd, uint8_t code) {
    (void)send(fd, &code, 1, 0);
}

static int recv_msg(int fd, char fields[][MAX_NAME], int max_fields) {
    char buf[MAX_NAME * 3 + 64];
    int  n = 0;
    char c;

    while (n < (int)sizeof(buf) - 1) {
        if (recv(fd, &c, 1, 0) <= 0) return -1;
        if (c == '\0') break;
        buf[n++] = c;
    }
    buf[n] = '\0';
    if (n == 0) return -1;

    int   count = 0;
    char *p     = buf;
    while (count < max_fields) {
        char  *sep = strchr(p, '#');
        size_t len = sep ? (size_t)(sep - p) : strlen(p);
        if (len >= MAX_NAME) len = MAX_NAME - 1;
        memcpy(fields[count], p, len);
        fields[count][len] = '\0';
        count++;
        if (!sep) break;
        p = sep + 1;
        if (*p == '\0') break;
    }
    return count;
}

static char *get_local_ip(void) {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) < 0) return "127.0.0.1";
    struct hostent *he = gethostbyname(hostname);
    if (!he) return "127.0.0.1";
    return inet_ntoa(*(struct in_addr *)he->h_addr_list[0]);
}

static void handle_register(int fd, char fields[][MAX_NAME], int nfields) {
    if (nfields < 2) { send_code(fd, 2); return; }
    int r = user_add(fields[1]);
    send_code(fd, (uint8_t)r);
    if (r == 0) printf("s> REGISTER %s OK\n",   fields[1]);
    else        printf("s> REGISTER %s FAIL\n", fields[1]);
}

static void handle_unregister(int fd, char fields[][MAX_NAME], int nfields) {
    if (nfields < 2) { send_code(fd, 2); return; }
    int r = user_remove(fields[1]);
    send_code(fd, (uint8_t)r);
    if (r == 0) printf("s> UNREGISTER %s OK\n",   fields[1]);
    else        printf("s> UNREGISTER %s FAIL\n", fields[1]);
}

static void handle_connect(int fd, const char *client_ip,
                           char fields[][MAX_NAME], int nfields) {
    // Port is sent by the Python client in fields[2]
    if (nfields < 3) { send_code(fd, 3); close(fd); return; }
    const char *name = fields[1];
    uint16_t client_port = (uint16_t) atoi(fields[2]);

    int r = user_connect(name, client_ip, client_port);
    send_code(fd, (uint8_t)r);

    if (r != 0) {
        printf("s> CONNECT %s FAIL\n", name);
        close(fd);
        return;
    }
    printf("s> CONNECT %s OK\n", name);

    // Close the original CONNECT fd to let the Python client proceed
    close(fd);

    // Give the client time to `bind` and `listen` on its new socket
    usleep(100000);

    /* entregar mensajes pendientes que haya en la BD */
    unsigned int mid;
    char         sender[MAX_NAME], text[MAX_MSG];
    while (msg_get_next(name, &mid, sender, text) == 0) {
        if (conn_deliver(name, sender, mid, text) < 0) break;
    }
}

static void handle_disconnect(int fd, const char *client_ip,
                              char fields[][MAX_NAME], int nfields) {
    if (nfields < 2) { send_code(fd, 3); return; }

    const char *name = fields[1];

    char     stored_ip[16];
    uint16_t stored_port;
    int conn = user_get_conn_info(name, stored_ip, &stored_port);

    if (conn == 1 || conn == 2 || strcmp(stored_ip, client_ip) != 0) {
        send_code(fd, (conn == 1) ? 1 : ((conn == 2) ? 2 : 3));
        printf("s> DISCONNECT %s FAIL\n", name);
        return;
    }

    int r = user_disconnect(name);
    send_code(fd, (uint8_t)r);
    if (r == 0) printf("s> DISCONNECT %s OK\n",   name);
    else        printf("s> DISCONNECT %s FAIL\n", name);
}

static void handle_send(int fd, char fields[][MAX_NAME], int nfields) {
    if (nfields < 4) { send_code(fd, 2); return; }
    const char *sender   = fields[1];
    const char *receiver = fields[2];
    const char *text     = fields[3];

    char     dummy_ip[16];
    uint16_t dummy_port;
    if (user_get_conn_info(receiver, dummy_ip, &dummy_port) == 1) {
        send_code(fd, 1);
        return;
    }

    unsigned int mid = msg_add(receiver, sender, text);
    if (mid == 0) { send_code(fd, 2); return; }

    send_code(fd, 0);
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%u", mid);
    (void)send(fd, id_str, strlen(id_str) + 1, 0);

    if (conn_deliver(receiver, sender, mid, text) < 0) {
        printf("s> MESSAGE %u FROM %s TO %s STORED\n", mid, sender, receiver);
    }
}

static void handle_users(int fd, char fields[][MAX_NAME], int nfields) {
    if (nfields < 2) { send_code(fd, 2); return; }
    const char *name = fields[1];

    char     dummy_ip[16];
    uint16_t dummy_port;
    int conn = user_get_conn_info(name, dummy_ip, &dummy_port);

    if (conn == 1 || conn == 2) {
        send_code(fd, (conn == 1) ? 2 : 1);
        printf("s> CONNECTEDUSERS FAIL\n");
        return;
    }

    char names[MAX_USERS][MAX_NAME];
    int  count = users_get_connected(names, MAX_USERS);
    if (count < 0) {
        send_code(fd, 2);
        printf("s> CONNECTEDUSERS FAIL\n");
        return;
    }

    send_code(fd, 0);
    char count_str[16];
    snprintf(count_str, sizeof(count_str), "%d", count);
    (void)send(fd, count_str, strlen(count_str) + 1, 0);
    for (int i = 0; i < count; i++) {
        (void)send(fd, names[i], strlen(names[i]) + 1, 0);
    }
    printf("s> CONNECTEDUSERS OK\n");
}

static void *handle_client(void *arg) {
    ClientArg *ca = (ClientArg *)arg;
    int  fd = ca->fd;
    char ip[16];
    strncpy(ip, ca->ip, sizeof(ip) - 1);
    ip[sizeof(ip) - 1] = '\0';
    free(ca);

    char fields[8][MAX_NAME];
    int  nfields = recv_msg(fd, fields, 8);
    if (nfields < 1) { close(fd); return NULL; }

    // Use strcmp since the protocol sends commands as STRINGS, not integers
    int op = -1;
    if (strcmp(fields[0], "REGISTER") == 0) op = 1;
    else if (strcmp(fields[0], "UNREGISTER") == 0) op = 2;
    else if (strcmp(fields[0], "CONNECT") == 0) op = 3;
    else if (strcmp(fields[0], "DISCONNECT") == 0) op = 4;
    else if (strcmp(fields[0], "SEND") == 0) op = 5;
    else if (strcmp(fields[0], "USERS") == 0) op = 7;

    switch (op) {
        case 1: handle_register  (fd,     fields, nfields); break;
        case 2: handle_unregister(fd,     fields, nfields); break;
        case 3: handle_connect   (fd, ip, fields, nfields); return NULL; /* fd managed inside */
        case 4: handle_disconnect(fd, ip, fields, nfields); break;
        case 5: handle_send      (fd,     fields, nfields); break;
        case 7: handle_users     (fd,     fields, nfields); break;
        default: break;
    }

    close(fd);
    return NULL;
}

static void sigint_handler(int sig) {
    (void)sig;
    if (server_fd >= 0) { close(server_fd); server_fd = -1; }
    db_close();
    exit(0);
}

int main(int argc, char *argv[]) {
    if (argc != 3 || strcmp(argv[1], "-p") != 0) {
        fprintf(stderr, "Uso: %s -p <puerto>\n", argv[0]);
        return 1;
    }

    if (db_init() != 0) {
        fprintf(stderr, "Error al inicializar la base de datos\n");
        return 1;
    }

    signal(SIGINT, sigint_handler);

    uint16_t port = (uint16_t)atoi(argv[2]);
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); db_close(); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(server_fd); db_close(); return 1;
    }
    if (listen(server_fd, 10) < 0) {
        perror("listen"); close(server_fd); db_close(); return 1;
    }

    printf("s> init server %s:%d\n", get_local_ip(), port);
    printf("s> ");
    fflush(stdout);

    struct sockaddr_in client_addr;
    socklen_t          client_len = sizeof(client_addr);

    while (1) {
        ClientArg *ca = malloc(sizeof(ClientArg));
        if (!ca) break;

        ca->fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (ca->fd < 0) { free(ca); break; }
        inet_ntop(AF_INET, &client_addr.sin_addr, ca->ip, sizeof(ca->ip));

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, ca);
        pthread_detach(tid);
    }

    close(server_fd);
    db_close();
    return 0;
}