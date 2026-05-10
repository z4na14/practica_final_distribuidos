#include <stdint.h>
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
#include "log_client.h"

// global para cerrarlo desde el signal handler
static int server_fd = -1;

typedef struct {
    int fd;
    char ip[16];
} ClientArg;

// entrega el mensaje al receptor, lo borra de la BD y envía el ACK al emisor
static int conn_deliver(const char *receiver, const char *sender,
                        const unsigned int msg_id, const char *text, const char *filename) {
    char msg_id_str[32];
    snprintf(msg_id_str, sizeof(msg_id_str), "%u", msg_id);

    char message[MAX_NAME + MAX_MSG + 512];
    size_t message_len;

    if (filename && strlen(filename) > 0) {
        message_len = snprintf(message, sizeof(message), "SEND_MESSAGE_ATTACH#%s#%s#%s#%s",
                               sender, msg_id_str, text, filename);
    } else {
        message_len = snprintf(message, sizeof(message), "SEND_MESSAGE#%s#%s#%s",
                               sender, msg_id_str, text);
    }

    if (message_len == 0 || message_len >= (int) sizeof(message) - 1) {
        return -1;
    }
    message[message_len] = '\0';

    char receiver_ip[16];
    uint16_t receiver_port;
    if (user_get_conn_info(receiver, receiver_ip, &receiver_port) != 0) {
        return -1;
    }

    int receiver_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (receiver_fd < 0) {
        return -1;
    }

    struct sockaddr_in receiver_addr = {0};
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(receiver_port);
    receiver_addr.sin_addr.s_addr = inet_addr(receiver_ip);

    if (connect(receiver_fd, (struct sockaddr *) &receiver_addr, sizeof(receiver_addr)) < 0) {
        // se fue sin desconectarse
        user_disconnect(receiver);
        close(receiver_fd);
        return -1;
    }

    ssize_t bytes_sent = send(receiver_fd, message, (message_len + 1), 0);
    close(receiver_fd);

    if (bytes_sent <= 0) {
        user_disconnect(receiver);
        return -1;
    }

    if (filename && strlen(filename) > 0) {
        printf("s> SEND MESSAGE ATTACH %u FROM %s TO %s\n", msg_id, sender, receiver);
    } else {
        printf("s> SEND MESSAGE %u FROM %s TO %s\n", msg_id, sender, receiver);
    }

    msg_delete(receiver, msg_id);

    char ack_message[256];
    size_t ack_len;
    if (filename && strlen(filename) > 0) {
        ack_len = snprintf(ack_message, sizeof(ack_message), "SEND_MESS_ATTACH_ACK#%s#%s", msg_id_str, filename);
    } else {
        ack_len = snprintf(ack_message, sizeof(ack_message), "SEND_MESS_ACK#%s", msg_id_str);
    }

    if (ack_len <= 0 || ack_len >= (int) sizeof(ack_message)) {
        return 0; // el ACK es best-effort; el mensaje ya fue entregado
    }
    ack_message[ack_len] = '\0';

    char sender_ip[16];
    uint16_t sender_port;
    if (user_get_conn_info(sender, sender_ip, &sender_port) != 0) {
        return 0; // el emisor puede haberse desconectado ya
    }

    int sender_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sender_fd < 0) {
        return 0;
    }

    struct sockaddr_in sender_addr = {0};
    sender_addr.sin_family = AF_INET;
    sender_addr.sin_port = htons(sender_port);
    sender_addr.sin_addr.s_addr = inet_addr(sender_ip);

    if (connect(sender_fd, (struct sockaddr *) &sender_addr, sizeof(sender_addr)) == 0) {
        send(sender_fd, ack_message, (size_t) (ack_len + 1), 0);
    }
    close(sender_fd);
    return 0;
}

static void send_code(const int client_fd, const uint8_t code) {
    send(client_fd, &code, 1, 0);
}

// lee el mensaje hasta '\0' y lo divide por '#' en fields; devuelve el número de campos
static int recv_msg(const int client_fd, char fields[][MAX_NAME]) {
    char raw_buf[MAX_NAME * 5 + 64];
    int bytes_read = 0;
    char c;

    while (bytes_read < (int) sizeof(raw_buf) - 1) {
        if (recv(client_fd, &c, 1, 0) <= 0) {
            return -1;
        }
        if (c == '\0') {
            break;
        }
        raw_buf[bytes_read++] = c;
    }
    raw_buf[bytes_read] = '\0';

    if (bytes_read == 0) {
        return -1;
    }

    int field_count = 0;
    char *cursor = raw_buf;
    char *separator = NULL;

    while (field_count < MAX_MSG_FIELDS) {
        separator = strchr(cursor, '#');
        size_t field_len = separator ? (size_t) (separator - cursor) : strlen(cursor);
        if (field_len >= MAX_NAME) {
            field_len = MAX_NAME - 1;
        }
        memcpy(fields[field_count], cursor, field_len);
        fields[field_count][field_len] = '\0';
        field_count++;
        if (!separator) {
            break;
        }
        cursor = separator + 1;
        if (*cursor == '\0') {
            break;
        }
    }
    return field_count;
}

static char *get_local_ip(void) {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) < 0) {
        return "127.0.0.1";
    }
    struct hostent *host_entry = gethostbyname(hostname);
    if (!host_entry) {
        return "127.0.0.1";
    }
    return inet_ntoa(*(struct in_addr *) host_entry->h_addr_list[0]);
}

static void handle_register(int client_fd, char fields[][MAX_NAME], int field_count) {
    if (field_count < 2) {
        send_code(client_fd, 2);
        return;
    }
    int result = user_add(fields[1]);
    send_code(client_fd, (uint8_t) result);
    if (result == 0) {
        printf("s> REGISTER %s OK\n", fields[1]);
        rpc_log(fields[1], "REGISTER", "");
    } else {
        printf("s> REGISTER %s FAIL\n", fields[1]);
    }
}

static void handle_unregister(int client_fd, char fields[][MAX_NAME], int field_count) {
    if (field_count < 2) {
        send_code(client_fd, 2);
        return;
    }
    int result = user_remove(fields[1]);
    send_code(client_fd, (uint8_t) result);
    if (result == 0) {
        printf("s> UNREGISTER %s OK\n", fields[1]);
        rpc_log(fields[1], "UNREGISTER", "");
    } else {
        printf("s> UNREGISTER %s FAIL\n", fields[1]);
    }
}

// cierra el fd antes de entregar pendientes para que el cliente tenga tiempo de abrir su socket
static void handle_connect(int client_fd, const char *client_ip,
                           char fields[][MAX_NAME], int field_count) {
    if (field_count < 3) {
        send_code(client_fd, 3);
        return;
    }
    const char *username = fields[1];
    uint16_t listen_port = (uint16_t) atoi(fields[2]);

    int result = user_connect(username, client_ip, listen_port);
    send_code(client_fd, (uint8_t) result);

    if (result != 0) {
        printf("s> CONNECT %s FAIL\n", username);
        return;
    }
    printf("s> CONNECT %s OK\n", username);
    rpc_log(username, "CONNECT", "");

    usleep(100000);

    unsigned int msg_id;
    char pending_sender[MAX_NAME];
    char pending_text[MAX_MSG];
    char pending_filename[MAX_MSG];

    while (msg_get_next(username, &msg_id, pending_sender, pending_text, pending_filename) == 0) {
        if (conn_deliver(username, pending_sender, msg_id, pending_text, pending_filename) < 0) {
            break;
        }
    }
}

static void handle_disconnect(int client_fd, const char *client_ip,
                              char fields[][MAX_NAME], int field_count) {
    if (field_count < 2) {
        send_code(client_fd, 3);
        return;
    }
    const char *username = fields[1];

    /* rechazamos si viene de otra IP; stored_ip solo está relleno si conn_status == 0 */
    char stored_ip[16];
    uint16_t stored_port;
    int conn_status = user_get_conn_info(username, stored_ip, &stored_port);

    if (conn_status == 1 || conn_status == 2 || strcmp(stored_ip, client_ip) != 0) {
        send_code(client_fd, (conn_status == 1) ? 1 : ((conn_status == 2) ? 2 : 3));
        printf("s> DISCONNECT %s FAIL\n", username);
        return;
    }

    int result = user_disconnect(username);
    send_code(client_fd, (uint8_t) result);
    if (result == 0) {
        printf("s> DISCONNECT %s OK\n", username);
        rpc_log(username, "DISCONNECT", "");
    } else {
        printf("s> DISCONNECT %s FAIL\n", username);
    }
}

static void handle_send(int client_fd, char fields[][MAX_NAME], int field_count) {
    if (field_count < 4) {
        send_code(client_fd, 2);
        return;
    }
    const char *sender = fields[1];
    const char *receiver = fields[2];
    const char *text = fields[3];

    // el receptor puede estar offline; solo rechazamos si no existe
    char unused_ip[16];
    uint16_t unused_port;
    if (user_get_conn_info(receiver, unused_ip, &unused_port) == 1) {
        send_code(client_fd, 1);
        return;
    }

    unsigned int msg_id = msg_add(receiver, sender, text, NULL);
    if (msg_id == 0) {
        send_code(client_fd, 2);
        return;
    }

    send_code(client_fd, 0);
    char msg_id_str[32];
    snprintf(msg_id_str, sizeof(msg_id_str), "%u", msg_id);
    send(client_fd, msg_id_str, strlen(msg_id_str) + 1, 0);
    rpc_log(sender, "SEND", "");

    if (conn_deliver(receiver, sender, msg_id, text, NULL) < 0) {
        printf("s> MESSAGE %u FROM %s TO %s STORED\n", msg_id, sender, receiver);
    }
}

static void handle_send_attached(int client_fd, char fields[][MAX_NAME], int field_count) {
    if (field_count < 5) {
        send_code(client_fd, 2);
        return;
    }
    const char *sender = fields[1];
    const char *receiver = fields[2];
    const char *text = fields[3];
    const char *filename = fields[4];

    char unused_ip[16];
    uint16_t unused_port;
    if (user_get_conn_info(receiver, unused_ip, &unused_port) == 1) {
        send_code(client_fd, 1);
        return;
    }

    unsigned int msg_id = msg_add(receiver, sender, text, filename);
    if (msg_id == 0) {
        send_code(client_fd, 2);
        return;
    }

    send_code(client_fd, 0);
    char msg_id_str[32];
    snprintf(msg_id_str, sizeof(msg_id_str), "%u", msg_id);
    send(client_fd, msg_id_str, strlen(msg_id_str) + 1, 0);
    rpc_log(sender, "SENDATTACH", filename);

    if (conn_deliver(receiver, sender, msg_id, text, filename) < 0) {
        printf("s> MESSAGE %u FROM %s TO %s STORED\n", msg_id, sender, receiver);
    }
}

static void handle_users(int client_fd, char fields[][MAX_NAME], int field_count) {
    if (field_count < 2) {
        send_code(client_fd, 2);
        return;
    }
    const char *username = fields[1];

    char unused_ip[16];
    uint16_t unused_port;
    int conn_status = user_get_conn_info(username, unused_ip, &unused_port);

    // los códigos de respuesta son inversos a conn_status (1→no conectado, 2→no existe)
    if (conn_status == 1 || conn_status == 2) {
        send_code(client_fd, (conn_status == 1) ? 2 : 1);
        printf("s> CONNECTEDUSERS FAIL\n");
        return;
    }

    char connected_names[MAX_USERS][MAX_NAME];
    int user_count = users_get_connected(connected_names, MAX_USERS);
    if (user_count < 0) {
        send_code(client_fd, 2);
        printf("s> CONNECTEDUSERS FAIL\n");
        return;
    }

    send_code(client_fd, 0);
    char count_str[16];
    snprintf(count_str, sizeof(count_str), "%d", user_count);
    send(client_fd, count_str, strlen(count_str) + 1, 0);

    for (int i = 0; i < user_count; i++) {
        char user_ip[16];
        uint16_t user_port;

        if (user_get_conn_info(connected_names[i], user_ip, &user_port) == 0) {
            char user_info[MAX_NAME + 32];
            snprintf(user_info, sizeof(user_info), "%.*s::%.15s::%u",
                     MAX_NAME, connected_names[i], user_ip, user_port);
            send(client_fd, user_info, strlen(user_info) + 1, 0);
        }
    }

    printf("s> CONNECTEDUSERS OK\n");
    rpc_log(username, "USERS", "");
}

static void *handle_client(void *arg) {
    ClientArg *client_arg = (ClientArg *) arg;
    int client_fd = client_arg->fd;
    char client_ip[16];
    strncpy(client_ip, client_arg->ip, sizeof(client_ip) - 1);
    client_ip[sizeof(client_ip) - 1] = '\0';
    free(client_arg);

    char message_fields[MAX_MSG_FIELDS][MAX_NAME];
    int field_count = recv_msg(client_fd, message_fields);
    if (field_count < 1) {
        close(client_fd);
        return NULL;
    }

    int operation = -1;
    if (strcmp(message_fields[0], "REGISTER") == 0) {
        operation = 1;
    } else if (strcmp(message_fields[0], "UNREGISTER") == 0) {
        operation = 2;
    } else if (strcmp(message_fields[0], "CONNECT") == 0) {
        operation = 3;
    } else if (strcmp(message_fields[0], "DISCONNECT") == 0) {
        operation = 4;
    } else if (strcmp(message_fields[0], "SEND") == 0) {
        operation = 5;
    } else if (strcmp(message_fields[0], "SENDATTACH") == 0) {
        operation = 6;
    } else if (strcmp(message_fields[0], "USERS") == 0) {
        operation = 7;
    }

    switch (operation) {
        case 1:
            handle_register(client_fd, message_fields, field_count);
            break;
        case 2:
            handle_unregister(client_fd, message_fields, field_count);
            break;
        case 3:
            handle_connect(client_fd, client_ip, message_fields, field_count);
            break;
        case 4:
            handle_disconnect(client_fd, client_ip, message_fields, field_count);
            break;
        case 5:
            handle_send(client_fd, message_fields, field_count);
            break;
        case 6:
            handle_send_attached(client_fd, message_fields, field_count);
            break;
        case 7:
            handle_users(client_fd, message_fields, field_count);
            break;
        default: break;
    }

    close(client_fd);
    return NULL;
}

static void sigint_handler(const int sig) {
    (void) sig;
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    db_close();
    exit(0);
}

static int initialize_listening_sock(uint16_t port) {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Error al inicializar el socket");
        db_close();
        return -1;
    }

    /* SO_REUSEADDR permite reutilizar el puerto inmediatamente tras reiniciar el servidor */
    const int reuse_opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_opt, sizeof(reuse_opt));

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        db_close();
        return -1;
    }
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        db_close();
        return -1;
    }

    return 0;
}

int main(const int argc, char *argv[]) {
    if (argc != 3 || strcmp(argv[1], "-p") != 0) {
        fprintf(stderr, "Uso: %s -p <puerto>\n", argv[0]);
        return 1;
    }

    if (db_init() != 0) {
        fprintf(stderr, "Error al inicializar la base de datos\n");
        return 1;
    }

    signal(SIGINT, sigint_handler);

    const uint16_t listen_port = (uint16_t) atoi(argv[2]);
    if (initialize_listening_sock(listen_port) < 0) {
        fprintf(stderr, "Error al inicializar el socket del servidor\n");
        return 1;
    }

    printf("s> init server %s:%d\n", get_local_ip(), listen_port);
    fflush(stdout);

    struct sockaddr_in incoming_addr;
    socklen_t incoming_addr_len = sizeof(incoming_addr);

    while (1) {
        ClientArg *client_arg = malloc(sizeof(ClientArg)); // Liberado dentro del hilo
        if (!client_arg) { break; }

        client_arg->fd = accept(server_fd, (struct sockaddr *) &incoming_addr, &incoming_addr_len);
        if (client_arg->fd < 0) {
            free(client_arg);
            break;
        }
        inet_ntop(AF_INET, &incoming_addr.sin_addr, client_arg->ip, sizeof(client_arg->ip));

        pthread_t thread_id;
        pthread_create(&thread_id, NULL, handle_client, client_arg);
        pthread_detach(thread_id); // el hilo se limpia solo al terminar
    }

    close(server_fd);
    db_close();
    return 0;
}
