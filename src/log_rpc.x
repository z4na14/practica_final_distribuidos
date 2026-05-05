struct peticion {
    char nombre[256];
    char operacion[256];
    char fichero[256];
};

program LOG {
    version LOGVER {
        int registrar(struct peticion) = 1;
    } = 1;
} = 100522240;
