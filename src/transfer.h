#ifndef TRANSFER_H
#define TRANSFER_H

#include <stddef.h>
#include <stdint.h>

// Callback para progresso do upload
typedef void (*progress_callback)(size_t bytes_sent, size_t total_bytes);

int transfer_global_init(void);
void transfer_global_cleanup(void);

// Enviar arquivo para outro dispositivo
int transfer_send_file(const char *filepath,
                       const char *dest_ip,
                       int dest_port,
                       const char *pair_token,
                       progress_callback callback);
int transfer_pair_peer(const char *dest_ip,
                       uint16_t dest_port,
                       const char *peer_name,
                       uint16_t peer_port,
                       const char *pair_code,
                       char *token_out,
                       size_t token_size,
                       long *expires_at,
                       char *message_out,
                       size_t message_size);

#endif
