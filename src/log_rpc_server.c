#include <stdio.h>
#include <string.h>
#include "log_rpc.h"

bool_t registrar_1_svc(struct peticion arg1, int *result, struct svc_req *rqstp)
{
    (void)rqstp;

    if (strcmp(arg1.operacion, "SENDATTACH") == 0) {
        printf("%s %s %s\n", arg1.nombre, arg1.operacion, arg1.fichero);
    } else {
        printf("%s %s\n", arg1.nombre, arg1.operacion);
    }
    fflush(stdout);
    *result = 1;
    return TRUE;
}

int log_1_freeresult(SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{
    (void)transp;
    xdr_free(xdr_result, result);
    return 1;
}
