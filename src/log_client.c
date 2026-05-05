#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log_rpc.h"
#include "log_client.h"

/* Crea un cliente RPC hacia LOG_RPC_IP, invoca registrar y lo destruye.
   Fallo silencioso: si el servidor RPC no está disponible, el servidor
   de mensajeria continua funcionando. */
void rpc_log(const char *username, const char *operation, const char *fichero)
{
    const char *rpc_ip = getenv("LOG_RPC_IP");
    if (!rpc_ip || rpc_ip[0] == '\0') return;

    CLIENT *clnt = clnt_create(rpc_ip, LOG, LOGVER, "tcp");
    if (clnt == NULL) {
        clnt_pcreateerror(rpc_ip);
        return;
    }

    peticion arg;
    strncpy(arg.nombre,    username,  sizeof(arg.nombre)    - 1);
    strncpy(arg.operacion, operation, sizeof(arg.operacion) - 1);
    strncpy(arg.fichero,   fichero,   sizeof(arg.fichero)   - 1);
    arg.nombre[sizeof(arg.nombre) - 1]       = '\0';
    arg.operacion[sizeof(arg.operacion) - 1] = '\0';
    arg.fichero[sizeof(arg.fichero) - 1]     = '\0';

    int result_1;
    enum clnt_stat retval = registrar_1(arg, &result_1, clnt);
    if (retval != RPC_SUCCESS) {
        clnt_perror(clnt, "registrar_1");
    }
    clnt_destroy(clnt);
}
