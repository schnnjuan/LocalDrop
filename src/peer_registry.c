#include "peer_registry.h"

#include "json_utils.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PeerInfo registry[PEER_REGISTRY_MAX_PEERS];
static size_t registry_count = 0;
static pthread_rwlock_t registry_lock;
static int registry_initialized = 0;

static void copy_string(char *dest, size_t dest_size, const char *src) {
    if (!dest || dest_size == 0) {
        return;
    }

    snprintf(dest, dest_size, "%s", src ? src : "");
}

int peer_registry_init(void) {
    if (registry_initialized) {
        return 0;
    }

    memset(registry, 0, sizeof(registry));
    registry_count = 0;
    if (pthread_rwlock_init(&registry_lock, NULL) != 0) {
        return -1;
    }

    registry_initialized = 1;
    return 0;
}

void peer_registry_cleanup(void) {
    if (!registry_initialized) {
        return;
    }

    pthread_rwlock_destroy(&registry_lock);
    registry_initialized = 0;
}

int peer_registry_add_or_update(const char *name, const char *ip, uint16_t port) {
    size_t index;

    if (!registry_initialized || !name || !ip) {
        return -1;
    }

    if (pthread_rwlock_wrlock(&registry_lock) != 0) {
        return -1;
    }

    for (index = 0; index < registry_count; index++) {
        if (strcmp(registry[index].ip, ip) == 0 && registry[index].port == port) {
            copy_string(registry[index].name, sizeof(registry[index].name), name);
            registry[index].active = 1;
            registry[index].last_seen = time(NULL);
            pthread_rwlock_unlock(&registry_lock);
            return 0;
        }
    }

    if (registry_count >= PEER_REGISTRY_MAX_PEERS) {
        pthread_rwlock_unlock(&registry_lock);
        return -1;
    }

    copy_string(registry[registry_count].name, sizeof(registry[registry_count].name), name);
    copy_string(registry[registry_count].ip, sizeof(registry[registry_count].ip), ip);
    registry[registry_count].port = port;
    registry[registry_count].active = 1;
    registry[registry_count].paired = 0;
    registry[registry_count].last_seen = time(NULL);
    registry_count++;

    pthread_rwlock_unlock(&registry_lock);
    return 0;
}

int peer_registry_mark_inactive(const char *name) {
    size_t index;
    int updated = 0;

    if (!registry_initialized || !name) {
        return -1;
    }

    if (pthread_rwlock_wrlock(&registry_lock) != 0) {
        return -1;
    }

    for (index = 0; index < registry_count; index++) {
        if (strcmp(registry[index].name, name) == 0) {
            registry[index].active = 0;
            updated = 1;
        }
    }

    pthread_rwlock_unlock(&registry_lock);
    return updated ? 0 : -1;
}

int peer_registry_set_paired(const char *ip, uint16_t port, int paired) {
    size_t index;

    if (!registry_initialized || !ip) {
        return -1;
    }

    if (pthread_rwlock_wrlock(&registry_lock) != 0) {
        return -1;
    }

    for (index = 0; index < registry_count; index++) {
        if (strcmp(registry[index].ip, ip) == 0 && registry[index].port == port) {
            registry[index].paired = paired ? 1 : 0;
            pthread_rwlock_unlock(&registry_lock);
            return 0;
        }
    }

    pthread_rwlock_unlock(&registry_lock);
    return -1;
}

int peer_registry_find_by_endpoint(const char *ip, uint16_t port, PeerInfo *out_peer) {
    size_t index;
    int found = -1;

    if (!registry_initialized || !ip) {
        return -1;
    }

    if (pthread_rwlock_rdlock(&registry_lock) != 0) {
        return -1;
    }

    for (index = 0; index < registry_count; index++) {
        if (strcmp(registry[index].ip, ip) == 0 && registry[index].port == port) {
            if (out_peer) {
                *out_peer = registry[index];
            }
            found = 0;
            break;
        }
    }

    pthread_rwlock_unlock(&registry_lock);
    return found;
}

size_t peer_registry_copy_snapshot(PeerInfo *peers, size_t max_peers) {
    size_t count = 0;

    if (!registry_initialized || !peers || max_peers == 0) {
        return 0;
    }

    if (pthread_rwlock_rdlock(&registry_lock) != 0) {
        return 0;
    }

    count = registry_count < max_peers ? registry_count : max_peers;
    memcpy(peers, registry, count * sizeof(PeerInfo));

    pthread_rwlock_unlock(&registry_lock);
    return count;
}

int peer_registry_snapshot_json(char **json_out) {
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    size_t index;

    if (!registry_initialized || !json_out) {
        return -1;
    }

    if (pthread_rwlock_rdlock(&registry_lock) != 0) {
        return -1;
    }

    if (json_buffer_append(&buffer, &length, &capacity, "[") != 0) {
        pthread_rwlock_unlock(&registry_lock);
        return -1;
    }

    for (index = 0; index < registry_count; index++) {
        if (index > 0 && json_buffer_append(&buffer, &length, &capacity, ",") != 0) {
            free(buffer);
            pthread_rwlock_unlock(&registry_lock);
            return -1;
        }

        if (json_buffer_append(&buffer, &length, &capacity, "{\"name\":\"") != 0 ||
            json_buffer_append_escaped(&buffer, &length, &capacity, registry[index].name) != 0 ||
            json_buffer_append(&buffer, &length, &capacity, "\",\"ip\":\"") != 0 ||
            json_buffer_append_escaped(&buffer, &length, &capacity, registry[index].ip) != 0 ||
            json_buffer_appendf(&buffer, &length, &capacity,
                                "\",\"port\":%u,\"active\":%s,\"paired\":%s,\"last_seen\":%ld}",
                                (unsigned int)registry[index].port,
                                registry[index].active ? "true" : "false",
                                registry[index].paired ? "true" : "false",
                                (long)registry[index].last_seen) != 0) {
            free(buffer);
            pthread_rwlock_unlock(&registry_lock);
            return -1;
        }
    }

    pthread_rwlock_unlock(&registry_lock);

    if (json_buffer_append(&buffer, &length, &capacity, "]") != 0) {
        free(buffer);
        return -1;
    }

    *json_out = buffer;
    return 0;
}
