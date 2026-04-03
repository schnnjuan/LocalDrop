#ifndef PEER_REGISTRY_H
#define PEER_REGISTRY_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define PEER_REGISTRY_MAX_PEERS 50

typedef struct {
    char name[256];
    char ip[46];
    uint16_t port;
    int active;
    int paired;
    time_t last_seen;
} PeerInfo;

int peer_registry_init(void);
void peer_registry_cleanup(void);
int peer_registry_add_or_update(const char *name, const char *ip, uint16_t port);
int peer_registry_mark_inactive(const char *name);
int peer_registry_set_paired(const char *ip, uint16_t port, int paired);
int peer_registry_find_by_endpoint(const char *ip, uint16_t port, PeerInfo *out_peer);
size_t peer_registry_copy_snapshot(PeerInfo *peers, size_t max_peers);
int peer_registry_snapshot_json(char **json_out);

#endif
