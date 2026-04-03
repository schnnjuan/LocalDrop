#ifndef SERVER_H
#define SERVER_H

#include <microhttpd.h>

// Inicializar servidor HTTP
struct MHD_Daemon* server_start(int port);

// Parar servidor
void server_stop(struct MHD_Daemon *daemon);

#endif