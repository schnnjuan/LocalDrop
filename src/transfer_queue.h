#ifndef TRANSFER_QUEUE_H
#define TRANSFER_QUEUE_H

#include <stddef.h>
#include <stdint.h>

int transfer_queue_init(void);
void transfer_queue_shutdown(void);
int transfer_enqueue(const char *filepath,
                     const char *target_ip,
                     uint16_t target_port,
                     const char *pair_token,
                     char *job_id_out,
                     size_t job_id_size);
int transfer_status_get(const char *job_id, char **json_out);
int transfer_status_list_json(char **json_out);

#endif
