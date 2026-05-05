#ifndef LOG_CLIENT_H
#define LOG_CLIENT_H

/* Envía al servidor RPC la operación realizada por el usuario.
   Pasa "" como fichero para operaciones sin adjunto.
   No hace nada si LOG_RPC_IP no está definida en el entorno. */
void rpc_log(const char *username, const char *operation, const char *fichero);

#endif /* LOG_CLIENT_H */
