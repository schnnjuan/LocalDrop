#ifndef STORAGE_H
#define STORAGE_H

#include <stddef.h>
#include <stdio.h>
#include <time.h>

typedef struct {
    FILE *fp;
    char original_name[256];
    char staging_path[512];
    size_t bytes_received;
    int sealed;
} UploadStage;

int storage_init(void);
void storage_cleanup(void);
int upload_stage_open(UploadStage *stage, const char *filename, char *error_message, size_t error_size);
int upload_stage_write(UploadStage *stage,
                       const char *data,
                       size_t size,
                       size_t max_file_size,
                       char *error_message,
                       size_t error_size);
int upload_stage_seal(UploadStage *stage, char *error_message, size_t error_size);
int upload_stage_commit(UploadStage *stage, char *final_path, size_t final_path_size, char *error_message, size_t error_size);
void upload_stage_abort(UploadStage *stage);
int storage_delete_file(const char *path);
void cleanup_expired_staging_files(time_t max_age_seconds);

#endif
