#ifndef TRANSFER_H
#define TRANSFER_H

#include <stddef.h>

// Callback para progresso do upload
typedef void (*progress_callback)(size_t bytes_sent, size_t total_bytes);

// Enviar arquivo para outro dispositivo
int transfer_send_file(const char *filepath, const char *dest_ip, int dest_port, 
                       progress_callback callback);

#endif