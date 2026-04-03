#include "transfer_queue.h"

#include "crypto.h"
#include "json_utils.h"
#include "storage.h"
#include "transfer.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_TRANSFER_JOBS 128
#define STAGING_RETENTION_SECONDS 86400
#define JOB_RETENTION_SECONDS 3600

typedef enum {
    TRANSFER_JOB_UNUSED = 0,
    TRANSFER_JOB_QUEUED,
    TRANSFER_JOB_IN_PROGRESS,
    TRANSFER_JOB_COMPLETED,
    TRANSFER_JOB_FAILED
} TransferJobStatus;

typedef struct {
    int in_use;
    char job_id[65];
    char filepath[512];
    char target_ip[46];
    uint16_t target_port;
    char pair_token[65];
    TransferJobStatus status;
    char error_message[256];
    size_t bytes_sent;
    size_t total_bytes;
    time_t created_at;
    time_t updated_at;
} TransferJob;

static pthread_mutex_t queue_lock;
static pthread_cond_t queue_cond;
static pthread_t queue_thread;
static int queue_initialized = 0;
static int queue_running = 0;
static TransferJob jobs[MAX_TRANSFER_JOBS];
static size_t pending_queue[MAX_TRANSFER_JOBS];
static size_t queue_head = 0;
static size_t queue_tail = 0;
static size_t queue_count = 0;

static const char *transfer_status_name(TransferJobStatus status) {
    switch (status) {
        case TRANSFER_JOB_QUEUED:
            return "queued";
        case TRANSFER_JOB_IN_PROGRESS:
            return "in_progress";
        case TRANSFER_JOB_COMPLETED:
            return "completed";
        case TRANSFER_JOB_FAILED:
            return "failed";
        case TRANSFER_JOB_UNUSED:
        default:
            return "unused";
    }
}

static void transfer_job_cleanup(TransferJob *job) {
    if (!job || !job->in_use) {
        return;
    }

    if (job->filepath[0] != '\0') {
        storage_delete_file(job->filepath);
    }

    memset(job, 0, sizeof(*job));
}

static void prune_terminal_jobs_locked(void) {
    time_t now = time(NULL);

    for (size_t index = 0; index < MAX_TRANSFER_JOBS; index++) {
        if (!jobs[index].in_use) {
            continue;
        }

        if ((jobs[index].status == TRANSFER_JOB_COMPLETED || jobs[index].status == TRANSFER_JOB_FAILED) &&
            jobs[index].updated_at + JOB_RETENTION_SECONDS <= now) {
            transfer_job_cleanup(&jobs[index]);
        }
    }
}

static int transfer_job_create(const char *filepath,
                               const char *target_ip,
                               uint16_t target_port,
                               const char *pair_token,
                               size_t *job_index_out,
                               char *job_id_out,
                               size_t job_id_size) {
    char token[17];
    size_t index;

    prune_terminal_jobs_locked();

    for (index = 0; index < MAX_TRANSFER_JOBS; index++) {
        if (!jobs[index].in_use) {
            break;
        }
    }

    if (index == MAX_TRANSFER_JOBS || crypto_random_token(token, sizeof(token), 8) != 0) {
        return -1;
    }

    memset(&jobs[index], 0, sizeof(jobs[index]));
    jobs[index].in_use = 1;
    snprintf(jobs[index].job_id, sizeof(jobs[index].job_id), "%ld-%s", (long)time(NULL), token);
    snprintf(jobs[index].filepath, sizeof(jobs[index].filepath), "%s", filepath);
    snprintf(jobs[index].target_ip, sizeof(jobs[index].target_ip), "%s", target_ip);
    snprintf(jobs[index].pair_token, sizeof(jobs[index].pair_token), "%s", pair_token);
    jobs[index].target_port = target_port;
    jobs[index].status = TRANSFER_JOB_QUEUED;
    jobs[index].created_at = time(NULL);
    jobs[index].updated_at = jobs[index].created_at;

    if (job_index_out) {
        *job_index_out = index;
    }

    if (job_id_out && job_id_size > 0) {
        snprintf(job_id_out, job_id_size, "%s", jobs[index].job_id);
    }

    return 0;
}

static void transfer_job_progress(size_t bytes_sent, size_t total_bytes, void *user_data) {
    TransferJob *job = user_data;

    if (!job) {
        return;
    }

    if (pthread_mutex_lock(&queue_lock) != 0) {
        return;
    }

    if (job->in_use) {
        job->bytes_sent = bytes_sent;
        job->total_bytes = total_bytes;
        job->status = TRANSFER_JOB_IN_PROGRESS;
        job->updated_at = time(NULL);
    }

    pthread_mutex_unlock(&queue_lock);
}

static void append_job_json(char **buffer, size_t *length, size_t *capacity, const TransferJob *job) {
    json_buffer_append(buffer, length, capacity, "{\"job_id\":\"");
    json_buffer_append_escaped(buffer, length, capacity, job->job_id);
    json_buffer_append(buffer, length, capacity, "\",\"status\":\"");
    json_buffer_append_escaped(buffer, length, capacity, transfer_status_name(job->status));
    json_buffer_append(buffer, length, capacity, "\",\"target_ip\":\"");
    json_buffer_append_escaped(buffer, length, capacity, job->target_ip);
    json_buffer_appendf(buffer,
                        length,
                        capacity,
                        "\",\"target_port\":%u,\"bytes_sent\":%zu,\"total_bytes\":%zu,\"created_at\":%ld,\"updated_at\":%ld,\"error_message\":\"",
                        (unsigned int)job->target_port,
                        job->bytes_sent,
                        job->total_bytes,
                        (long)job->created_at,
                        (long)job->updated_at);
    json_buffer_append_escaped(buffer, length, capacity, job->error_message);
    json_buffer_append(buffer, length, capacity, "\"}");
}

static void *transfer_worker_run(void *unused) {
    (void)unused;

    while (1) {
        size_t job_index;
        TransferJob *job;
        int send_result;

        pthread_mutex_lock(&queue_lock);
        while (queue_running && queue_count == 0) {
            pthread_cond_wait(&queue_cond, &queue_lock);
        }

        if (!queue_running && queue_count == 0) {
            pthread_mutex_unlock(&queue_lock);
            break;
        }

        job_index = pending_queue[queue_head];
        queue_head = (queue_head + 1) % MAX_TRANSFER_JOBS;
        queue_count--;

        job = &jobs[job_index];
        if (!job->in_use) {
            pthread_mutex_unlock(&queue_lock);
            continue;
        }

        job->status = TRANSFER_JOB_IN_PROGRESS;
        job->updated_at = time(NULL);
        pthread_mutex_unlock(&queue_lock);

        send_result = transfer_send_file(job->filepath,
                                         job->target_ip,
                                         (int)job->target_port,
                                         job->pair_token,
                                         transfer_job_progress,
                                         job);

        pthread_mutex_lock(&queue_lock);
        if (!job->in_use) {
            pthread_mutex_unlock(&queue_lock);
            continue;
        }

        job->updated_at = time(NULL);
        if (send_result == 0) {
            job->status = TRANSFER_JOB_COMPLETED;
            job->error_message[0] = '\0';
            if (job->filepath[0] != '\0') {
                storage_delete_file(job->filepath);
                job->filepath[0] = '\0';
            }
        } else {
            job->status = TRANSFER_JOB_FAILED;
            snprintf(job->error_message, sizeof(job->error_message), "%s", "Falha ao enviar o arquivo para o peer remoto");
        }
        pthread_mutex_unlock(&queue_lock);
    }

    return NULL;
}

int transfer_queue_init(void) {
    if (queue_initialized) {
        return 0;
    }

    if (pthread_mutex_init(&queue_lock, NULL) != 0) {
        return -1;
    }

    if (pthread_cond_init(&queue_cond, NULL) != 0) {
        pthread_mutex_destroy(&queue_lock);
        return -1;
    }

    memset(jobs, 0, sizeof(jobs));
    queue_head = 0;
    queue_tail = 0;
    queue_count = 0;
    queue_running = 1;
    cleanup_expired_staging_files(STAGING_RETENTION_SECONDS);

    if (pthread_create(&queue_thread, NULL, transfer_worker_run, NULL) != 0) {
        pthread_cond_destroy(&queue_cond);
        pthread_mutex_destroy(&queue_lock);
        queue_running = 0;
        return -1;
    }

    queue_initialized = 1;
    return 0;
}

void transfer_queue_shutdown(void) {
    if (!queue_initialized) {
        return;
    }

    pthread_mutex_lock(&queue_lock);
    queue_running = 0;
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_lock);

    pthread_join(queue_thread, NULL);
    pthread_cond_destroy(&queue_cond);
    pthread_mutex_destroy(&queue_lock);
    queue_initialized = 0;
}

int transfer_enqueue(const char *filepath,
                     const char *target_ip,
                     uint16_t target_port,
                     const char *pair_token,
                     char *job_id_out,
                     size_t job_id_size) {
    size_t job_index = 0;

    if (!queue_initialized || !filepath || !target_ip || !pair_token) {
        return -1;
    }

    cleanup_expired_staging_files(STAGING_RETENTION_SECONDS);

    if (pthread_mutex_lock(&queue_lock) != 0) {
        return -1;
    }

    if (queue_count >= MAX_TRANSFER_JOBS ||
        transfer_job_create(filepath, target_ip, target_port, pair_token, &job_index, job_id_out, job_id_size) != 0) {
        pthread_mutex_unlock(&queue_lock);
        return -1;
    }

    pending_queue[queue_tail] = job_index;
    queue_tail = (queue_tail + 1) % MAX_TRANSFER_JOBS;
    queue_count++;

    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_lock);
    return 0;
}

int transfer_status_get(const char *job_id, char **json_out) {
    char *payload = NULL;
    size_t length = 0;
    size_t capacity = 0;
    int status = -1;

    if (!queue_initialized || !job_id || !json_out) {
        return -1;
    }

    if (pthread_mutex_lock(&queue_lock) != 0) {
        return -1;
    }

    for (size_t index = 0; index < MAX_TRANSFER_JOBS; index++) {
        if (jobs[index].in_use && strcmp(jobs[index].job_id, job_id) == 0) {
            append_job_json(&payload, &length, &capacity, &jobs[index]);
            status = 0;
            break;
        }
    }

    pthread_mutex_unlock(&queue_lock);

    if (status != 0) {
        free(payload);
        return -1;
    }

    *json_out = payload;
    return 0;
}

int transfer_status_list_json(char **json_out) {
    char *payload = NULL;
    size_t length = 0;
    size_t capacity = 0;

    if (!queue_initialized || !json_out) {
        return -1;
    }

    if (pthread_mutex_lock(&queue_lock) != 0) {
        return -1;
    }

    json_buffer_append(&payload, &length, &capacity, "[");
    for (size_t index = 0; index < MAX_TRANSFER_JOBS; index++) {
        if (!jobs[index].in_use) {
            continue;
        }

        if (payload[length - 1] != '[') {
            json_buffer_append(&payload, &length, &capacity, ",");
        }

        append_job_json(&payload, &length, &capacity, &jobs[index]);
    }
    json_buffer_append(&payload, &length, &capacity, "]");

    pthread_mutex_unlock(&queue_lock);

    *json_out = payload;
    return 0;
}
