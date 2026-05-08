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

/* fd del socket principal de escucha del servidor; necesitamos acceso global
   para poder cerrarlo limpiamente desde el manejador de señales */
static int server_fd = -1;

/* Datos que se pasan a cada hilo al aceptar una conexión entrante */
typedef struct {
    int fd;
    char ip[16];
} ClientArg;

/* Abre una conexión saliente hacia el socket de escucha del receptor
   y le entrega el mensaje. Si el receptor también está conectado, le
   manda un ACK al emisor. Devuelve 0 si todo fue bien, -1 si falló. */
static int conn_deliver(const char *receiver, const char *sender,
                        const unsigned int msg_id, const char *text) {
    char msg_id_str[32];
    snprintf(msg_id_str, sizeof(msg_id_str), "%u", msg_id);

    /* Construimos la trama que recibirá el cliente destinatario */
    char message[MAX_NAME + MAX_MSG + 64];
    const size_t message_len = snprintf(message, sizeof(message), "SEND_MESSAGE#%s#%s#%s",
                               sender, msg_id_str, text);
    if (message_len == 0 || message_len >= (int) sizeof(message) - 1) {
        return -1;
    }
    message[message_len] = '\0';

    /* Consultamos la BD para obtener la IP y puerto del receptor */
    char receiver_ip[16];
    uint16_t receiver_port;
    if (user_get_conn_info(receiver, receiver_ip, &receiver_port) != 0) {
        return -1; /* el receptor no está conectado */
    }

    /* Abrimos una conexión hacia el socket de escucha del receptor y le mandamos el mensaje */
    const int receiver_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (receiver_fd < 0) {
        return -1;
    }

    struct sockaddr_in receiver_addr = {0};
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(receiver_port);
    receiver_addr.sin_addr.s_addr = inet_addr(receiver_ip);

    if (connect(receiver_fd, (struct sockaddr *) &receiver_addr, sizeof(receiver_addr)) < 0) {
        /* Si no podemos conectar, el receptor se fue sin desconectarse: lo marcamos offline */
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

    printf("s> SEND MESSAGE %u FROM %s TO %s\n", msg_id, sender, receiver);
    msg_delete(receiver, msg_id);

    /* Ahora le avisamos al emisor de que el mensaje llegó correctamente */
    char ack_message[64];
    const size_t ack_len = snprintf(ack_message, sizeof(ack_message), "SEND_MESS_ACK#%s", msg_id_str);
    if (ack_len <= 0 || ack_len >= (int) sizeof(ack_message)) {
        return 0; /* el mensaje se entregó aunque no podamos mandar el ACK */
    }
    ack_message[ack_len] = '\0';

    char sender_ip[16];
    uint16_t sender_port;
    if (user_get_conn_info(sender, sender_ip, &sender_port) != 0) {
        return 0; /* el emisor ya se desconectó, no pasa nada */
    }

    const int sender_fd = socket(AF_INET, SOCK_STREAM, 0);
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

/* Envía un byte de código de respuesta al cliente */
static void send_code(const int client_fd, const uint8_t code) {
    (void) send(client_fd, &code, 1, 0);
}

/* Lee un mensaje separado por '#' y terminado en '\0' del socket.
   Rellena el array de campos y devuelve cuántos campos se leyeron,
   o -1 si la conexión se cerró o si el mensaje está vacío. */
static int recv_msg(const int client_fd, char fields[][MAX_NAME]) {
    char raw_buf[MAX_NAME * 3 + 64];
    int bytes_read = 0;
    char c;

    /* Leer byte a byte hasta encontrar '\0' */
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

    /* Partir el buffer por '#' y copiar cada trozo en su campo */
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

/* Devuelve la IP local de la máquina para mostrarla al arrancar */
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

/* Registra un nuevo usuario en el sistema. El nombre viene en fields[1].
   Devuelve 0 si OK, 1 si el nombre ya estaba en uso, 2 si hubo un error. */
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

/* Da de baja a un usuario del sistema. Borra también sus mensajes pendientes.
   Devuelve 0 si OK, 1 si el usuario no existía, 2 si hubo un error. */
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

/* Marca al usuario como conectado y guarda su IP y puerto de escucha en la BD.
   Tras confirmar la conexión, entrega los mensajes que se acumularon mientras estaba offline.
   Devuelve 0 si OK, 1 si el usuario no existe, 2 si ya estaba conectado, 3 si hubo un error. */
static void handle_connect(int client_fd, const char *client_ip,
                           char fields[][MAX_NAME], int field_count) {
    /* El cliente Python manda su puerto de escucha en fields[2] */
    if (field_count < 3) {
        send_code(client_fd, 3);
        close(client_fd);
        return;
    }
    const char *username = fields[1];
    uint16_t listen_port = (uint16_t) atoi(fields[2]);

    int result = user_connect(username, client_ip, listen_port);
    send_code(client_fd, (uint8_t) result);

    if (result != 0) {
        printf("s> CONNECT %s FAIL\n", username);
        close(client_fd);
        return;
    }
    printf("s> CONNECT %s OK\n", username);
    rpc_log(username, "CONNECT", "");

    /* Cerramos el fd de la petición CONNECT para que el cliente Python pueda
       continuar y abrir su socket de escucha en el puerto indicado */
    close(client_fd);

    /* Pequeña espera para asegurarnos de que el cliente ya está escuchando
       antes de intentar enviarle mensajes pendientes */
    usleep(100000);

    /* Entregar todos los mensajes que se acumularon mientras estaba offline */
    unsigned int msg_id;
    char pending_sender[MAX_NAME];
    char pending_text[MAX_MSG];
    while (msg_get_next(username, &msg_id, pending_sender, pending_text) == 0) {
        if (conn_deliver(username, pending_sender, msg_id, pending_text) < 0) {
            break;
        }
    }
}

/* Marca al usuario como desconectado en la BD. Solo acepta la petición si viene
   de la misma IP con la que el usuario se conectó, para evitar suplantaciones.
   Devuelve 0 si OK, 1 si el usuario no existe, 2 si no estaba conectado, 3 si hubo un error. */
static void handle_disconnect(int client_fd, const char *client_ip,
                              char fields[][MAX_NAME], int field_count) {
    if (field_count < 2) {
        send_code(client_fd, 3);
        return;
    }
    const char *username = fields[1];

    /* Comprobamos que la petición viene de la misma IP que usó para conectarse,
       para evitar que otro host desconecte a un usuario ajeno */
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

/* Guarda el mensaje en la BD y lo intenta entregar al receptor de inmediato.
   Si el receptor no está conectado, el mensaje queda almacenado hasta que se conecte.
   Devuelve 0 y el ID del mensaje si OK, 1 si el destinatario no existe, 2 si hubo un error. */
static void handle_send(int client_fd, char fields[][MAX_NAME], int field_count) {
    if (field_count < 4) {
        send_code(client_fd, 2);
        return;
    }
    const char *sender = fields[1];
    const char *receiver = fields[2];
    const char *text = fields[3];

    /* Verificamos que el destinatario existe (si no existe, devolvemos error 1) */
    char unused_ip[16];
    uint16_t unused_port;
    if (user_get_conn_info(receiver, unused_ip, &unused_port) == 1) {
        send_code(client_fd, 1);
        return;
    }

    /* Guardamos el mensaje en la BD para garantizar que no se pierde */
    unsigned int msg_id = msg_add(receiver, sender, text);
    if (msg_id == 0) {
        send_code(client_fd, 2);
        return;
    }

    /* Confirmamos al emisor el éxito y le mandamos el ID asignado al mensaje */
    send_code(client_fd, 0);
    char msg_id_str[32];
    snprintf(msg_id_str, sizeof(msg_id_str), "%u", msg_id);
    (void) send(client_fd, msg_id_str, strlen(msg_id_str) + 1, 0);
    rpc_log(sender, "SEND", "");

    /* Intentamos entrega inmediata si el receptor está conectado.
       Si falla, el mensaje se queda en la BD y se entregará cuando el receptor vuelva. */
    if (conn_deliver(receiver, sender, msg_id, text) < 0) {
        printf("s> MESSAGE %u FROM %s TO %s STORED\n", msg_id, sender, receiver);
    }
}

static void handle_send_attached(int client_fd, char fields[][MAX_NAME], int field_count) {
    if (field_count != 5) {
        send_code(client_fd, 2);
        return;
    }

    const char *sender   = fields[1];
    const char *receiver = fields[2];
    const char *message  = fields[3];
    const char *filename = fields[4];
  
}

/* Devuelve la lista de usuarios conectados en el momento de la consulta.
   Solo pueden pedirla usuarios que estén ellos mismos conectados.
   Devuelve 0 y el número de usuarios seguido de sus nombres, o 1/2 si hubo un error. */
static void handle_users(int client_fd, char fields[][MAX_NAME], int field_count) {
    if (field_count < 2) {
        send_code(client_fd, 2);
        return;
    }
    const char *username = fields[1];

    /* Solo los usuarios conectados pueden pedir la lista */
    char unused_ip[16];
    uint16_t unused_port;
    int conn_status = user_get_conn_info(username, unused_ip, &unused_port);

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

    /* Enviamos primero el número de usuarios y luego un nombre por línea */
    send_code(client_fd, 0);
    char count_str[16];
    snprintf(count_str, sizeof(count_str), "%d", user_count);
    (void) send(client_fd, count_str, strlen(count_str) + 1, 0);
    for (int i = 0; i < user_count; i++) {
        (void) send(client_fd, connected_names[i], strlen(connected_names[i]) + 1, 0);
    }

    printf("s> CONNECTEDUSERS OK\n");
    rpc_log(username, "USERS", "");
}

/* Función ejecutada por cada hilo: lee un comando del socket y lo despacha */
static void *handle_client(void *arg) {
    ClientArg *client_arg = (ClientArg *) arg;
    const int client_fd = client_arg->fd;
    char client_ip[16];
    strncpy(client_ip, client_arg->ip, sizeof(client_ip) - 1);
    client_ip[sizeof(client_ip) - 1] = '\0';
    free(client_arg);

    char message_fields[MAX_MSG_FIELDS][MAX_NAME];
    const int field_count = recv_msg(client_fd, message_fields);
    if (field_count < 1) {
        close(client_fd);
        return NULL;
    }

    /* El protocolo manda el comando como string, no como entero */
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
    } else if (strcmp(message_fields[0], "SENDATTACH")) {
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
            return NULL; /* el fd lo cierra handle_connect */
        case 4:
            handle_disconnect(client_fd, client_ip, message_fields, field_count);
            break;
        case 5:
            handle_send(client_fd, message_fields, field_count);
            break;
        case 6:
            handle_send_attached();
            break;
        case 7:
            handle_users(client_fd, message_fields, field_count);
            break;
        default: break;
    }

    close(client_fd);
    return NULL;
}

/* Handler de Ctrl+C: cierra el socket principal y la BD antes de salir */
static void sigint_handler(const int sig) {
    (void) sig;
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    db_close();
    exit(0);
}

static uint16_t initialize_listening_sock(uint16_t port) {

    const uint16_t listen_port = port;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Error al inicializar el socket");
        db_close();
        return -1;
    }

    /* SO_REUSEADDR permite reutilizar el puerto inmediatamente tras reiniciar el servidor */
    constexpr int reuse_opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_opt, sizeof(reuse_opt));

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(listen_port);
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

    const uint16_t listen_port = initialize_listening_sock((uint16_t)atoi(argv[2])); 
    if (listen_port < 0) {
        fprintf(stderr, "Error al inicializar el socket del servidor\n");
        return 1;
    }

    printf("s> init server %s:%d\n", get_local_ip(), listen_port);
    printf("s> ");
    fflush(stdout);

    struct sockaddr_in incoming_addr;
    socklen_t incoming_addr_len = sizeof(incoming_addr);

    /* Bucle principal: aceptar conexiones y lanzar un hilo por cada una */
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
        pthread_detach(thread_id); /* el hilo se limpia solo al terminar */
    }

    close(server_fd);
    db_close();
    return 0;
}
