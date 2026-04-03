#include "auth.h"

#include "crypto.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MAX_UI_SESSIONS 32
#define MAX_PEER_TOKENS 64
#define MAX_RATE_LIMITS 64
#define PAIRING_CODE_TTL_SECONDS 300
#define UI_SESSION_TTL_SECONDS 43200

typedef struct {
    int active;
    char token[65];
    char client_ip[46];
    time_t expires_at;
} UiSession;

typedef struct {
    int active;
    char peer_ip[46];
    char peer_name[256];
    uint16_t peer_port;
    char token[65];
    time_t expires_at;
} PeerTokenRecord;

typedef struct {
    int active;
    char client_ip[46];
    char scope[32];
    unsigned int count;
    time_t window_started_at;
} RateLimitRecord;

static pthread_mutex_t auth_lock;
static int auth_initialized = 0;
static char admin_code[7];
static char pairing_code[7];
static time_t pairing_code_expires_at = 0;
static UiSession ui_sessions[MAX_UI_SESSIONS];
static PeerTokenRecord peer_tokens[MAX_PEER_TOKENS];
static RateLimitRecord rate_limits[MAX_RATE_LIMITS];

static void copy_string(char *dest, size_t dest_size, const char *src) {
    if (!dest || dest_size == 0) {
        return;
    }

    snprintf(dest, dest_size, "%s", src ? src : "");
}

static int ensure_pairing_code_locked(void) {
    time_t now = time(NULL);

    if (pairing_code_expires_at > now && pairing_code[0] != '\0') {
        return 0;
    }

    if (crypto_random_numeric_code(pairing_code, sizeof(pairing_code), 6) != 0) {
        return -1;
    }

    pairing_code_expires_at = now + PAIRING_CODE_TTL_SECONDS;
    return 0;
}

int auth_init(void) {
    if (auth_initialized) {
        return 0;
    }

    if (pthread_mutex_init(&auth_lock, NULL) != 0) {
        return -1;
    }

    memset(ui_sessions, 0, sizeof(ui_sessions));
    memset(peer_tokens, 0, sizeof(peer_tokens));
    memset(rate_limits, 0, sizeof(rate_limits));

    if (crypto_random_numeric_code(admin_code, sizeof(admin_code), 6) != 0) {
        pthread_mutex_destroy(&auth_lock);
        return -1;
    }

    if (crypto_random_numeric_code(pairing_code, sizeof(pairing_code), 6) != 0) {
        pthread_mutex_destroy(&auth_lock);
        return -1;
    }

    pairing_code_expires_at = time(NULL) + PAIRING_CODE_TTL_SECONDS;
    auth_initialized = 1;
    return 0;
}

void auth_cleanup(void) {
    if (!auth_initialized) {
        return;
    }

    pthread_mutex_destroy(&auth_lock);
    auth_initialized = 0;
}

int auth_get_admin_code(char *dest, size_t dest_size) {
    int status = -1;

    if (!auth_initialized || !dest || dest_size == 0) {
        return -1;
    }

    if (pthread_mutex_lock(&auth_lock) != 0) {
        return -1;
    }

    copy_string(dest, dest_size, admin_code);
    status = 0;
    pthread_mutex_unlock(&auth_lock);
    return status;
}

int auth_create_ui_session(const char *client_ip, const char *provided_admin_code, char *token_out, size_t token_size) {
    size_t index;
    int status = -1;

    if (!auth_initialized || !client_ip || !provided_admin_code || !token_out || token_size < 65) {
        return -1;
    }

    if (pthread_mutex_lock(&auth_lock) != 0) {
        return -1;
    }

    if (strcmp(admin_code, provided_admin_code) != 0) {
        pthread_mutex_unlock(&auth_lock);
        return -1;
    }

    for (index = 0; index < MAX_UI_SESSIONS; index++) {
        if (!ui_sessions[index].active || ui_sessions[index].expires_at <= time(NULL) ||
            strcmp(ui_sessions[index].client_ip, client_ip) == 0) {
            if (crypto_random_token(ui_sessions[index].token, sizeof(ui_sessions[index].token), 32) != 0) {
                pthread_mutex_unlock(&auth_lock);
                return -1;
            }

            ui_sessions[index].active = 1;
            copy_string(ui_sessions[index].client_ip, sizeof(ui_sessions[index].client_ip), client_ip);
            ui_sessions[index].expires_at = time(NULL) + UI_SESSION_TTL_SECONDS;
            copy_string(token_out, token_size, ui_sessions[index].token);
            status = 0;
            break;
        }
    }

    pthread_mutex_unlock(&auth_lock);
    return status;
}

int auth_validate_ui_session(const char *client_ip, const char *token) {
    size_t index;
    int valid = -1;

    if (!auth_initialized || !client_ip || !token || token[0] == '\0') {
        return -1;
    }

    if (pthread_mutex_lock(&auth_lock) != 0) {
        return -1;
    }

    for (index = 0; index < MAX_UI_SESSIONS; index++) {
        if (!ui_sessions[index].active) {
            continue;
        }

        if (ui_sessions[index].expires_at <= time(NULL)) {
            ui_sessions[index].active = 0;
            continue;
        }

        if (strcmp(ui_sessions[index].client_ip, client_ip) == 0 &&
            strcmp(ui_sessions[index].token, token) == 0) {
            valid = 0;
            break;
        }
    }

    pthread_mutex_unlock(&auth_lock);
    return valid;
}

int auth_get_pairing_code(char *code_out, size_t code_size, time_t *expires_at) {
    int status = -1;

    if (!auth_initialized || !code_out || code_size == 0) {
        return -1;
    }

    if (pthread_mutex_lock(&auth_lock) != 0) {
        return -1;
    }

    if (ensure_pairing_code_locked() == 0) {
        copy_string(code_out, code_size, pairing_code);
        if (expires_at) {
            *expires_at = pairing_code_expires_at;
        }
        status = 0;
    }

    pthread_mutex_unlock(&auth_lock);
    return status;
}

static int upsert_peer_token_locked(const char *peer_ip,
                                    uint16_t peer_port,
                                    const char *peer_name,
                                    const char *token,
                                    time_t expires_at) {
    size_t index;
    size_t empty_index = MAX_PEER_TOKENS;

    for (index = 0; index < MAX_PEER_TOKENS; index++) {
        if (peer_tokens[index].active &&
            strcmp(peer_tokens[index].peer_ip, peer_ip) == 0 &&
            peer_tokens[index].peer_port == peer_port) {
            copy_string(peer_tokens[index].peer_name, sizeof(peer_tokens[index].peer_name), peer_name);
            copy_string(peer_tokens[index].token, sizeof(peer_tokens[index].token), token);
            peer_tokens[index].expires_at = expires_at;
            return 0;
        }

        if (!peer_tokens[index].active && empty_index == MAX_PEER_TOKENS) {
            empty_index = index;
        }
    }

    if (empty_index == MAX_PEER_TOKENS) {
        return -1;
    }

    peer_tokens[empty_index].active = 1;
    copy_string(peer_tokens[empty_index].peer_ip, sizeof(peer_tokens[empty_index].peer_ip), peer_ip);
    copy_string(peer_tokens[empty_index].peer_name, sizeof(peer_tokens[empty_index].peer_name), peer_name);
    peer_tokens[empty_index].peer_port = peer_port;
    copy_string(peer_tokens[empty_index].token, sizeof(peer_tokens[empty_index].token), token);
    peer_tokens[empty_index].expires_at = expires_at;
    return 0;
}

int auth_issue_pair_token(const char *peer_ip,
                          const char *peer_name,
                          uint16_t peer_port,
                          const char *provided_pair_code,
                          char *token_out,
                          size_t token_size,
                          time_t *expires_at) {
    char token[65];
    time_t token_expires_at;
    int status = -1;

    if (!auth_initialized || !peer_ip || !provided_pair_code || !token_out || token_size < sizeof(token)) {
        return -1;
    }

    if (pthread_mutex_lock(&auth_lock) != 0) {
        return -1;
    }

    if (ensure_pairing_code_locked() != 0 || strcmp(pairing_code, provided_pair_code) != 0) {
        pthread_mutex_unlock(&auth_lock);
        return -1;
    }

    if (crypto_random_token(token, sizeof(token), 32) != 0) {
        pthread_mutex_unlock(&auth_lock);
        return -1;
    }

    token_expires_at = time(NULL) + (7 * 24 * 60 * 60);
    if (upsert_peer_token_locked(peer_ip, peer_port, peer_name, token, token_expires_at) == 0) {
        copy_string(token_out, token_size, token);
        if (expires_at) {
            *expires_at = token_expires_at;
        }
        status = 0;
    }

    pthread_mutex_unlock(&auth_lock);
    return status;
}

int auth_store_peer_token(const char *peer_ip,
                          uint16_t peer_port,
                          const char *peer_name,
                          const char *token,
                          time_t expires_at) {
    int status = -1;

    if (!auth_initialized || !peer_ip || !token) {
        return -1;
    }

    if (pthread_mutex_lock(&auth_lock) != 0) {
        return -1;
    }

    status = upsert_peer_token_locked(peer_ip, peer_port, peer_name, token, expires_at);
    pthread_mutex_unlock(&auth_lock);
    return status;
}

int auth_get_peer_token(const char *peer_ip, uint16_t peer_port, char *token_out, size_t token_size) {
    size_t index;
    int status = -1;

    if (!auth_initialized || !peer_ip || !token_out || token_size == 0) {
        return -1;
    }

    if (pthread_mutex_lock(&auth_lock) != 0) {
        return -1;
    }

    for (index = 0; index < MAX_PEER_TOKENS; index++) {
        if (!peer_tokens[index].active) {
            continue;
        }

        if (peer_tokens[index].expires_at <= time(NULL)) {
            peer_tokens[index].active = 0;
            continue;
        }

        if (strcmp(peer_tokens[index].peer_ip, peer_ip) == 0 &&
            peer_tokens[index].peer_port == peer_port) {
            copy_string(token_out, token_size, peer_tokens[index].token);
            status = 0;
            break;
        }
    }

    pthread_mutex_unlock(&auth_lock);
    return status;
}

int auth_validate_peer_token(const char *peer_ip, const char *token) {
    size_t index;
    int status = -1;

    if (!auth_initialized || !peer_ip || !token || token[0] == '\0') {
        return -1;
    }

    if (pthread_mutex_lock(&auth_lock) != 0) {
        return -1;
    }

    for (index = 0; index < MAX_PEER_TOKENS; index++) {
        if (!peer_tokens[index].active) {
            continue;
        }

        if (peer_tokens[index].expires_at <= time(NULL)) {
            peer_tokens[index].active = 0;
            continue;
        }

        if (strcmp(peer_tokens[index].peer_ip, peer_ip) == 0 &&
            strcmp(peer_tokens[index].token, token) == 0) {
            status = 0;
            break;
        }
    }

    pthread_mutex_unlock(&auth_lock);
    return status;
}

int auth_rate_limit_allow(const char *client_ip,
                          const char *scope,
                          unsigned int limit,
                          time_t window_seconds,
                          char *message,
                          size_t message_size) {
    size_t index;
    size_t empty_index = MAX_RATE_LIMITS;
    time_t now = time(NULL);

    if (!auth_initialized || !client_ip || !scope || limit == 0 || window_seconds <= 0) {
        return -1;
    }

    if (pthread_mutex_lock(&auth_lock) != 0) {
        return -1;
    }

    for (index = 0; index < MAX_RATE_LIMITS; index++) {
        if (!rate_limits[index].active) {
            if (empty_index == MAX_RATE_LIMITS) {
                empty_index = index;
            }
            continue;
        }

        if (rate_limits[index].window_started_at + window_seconds <= now) {
            rate_limits[index].active = 0;
            if (empty_index == MAX_RATE_LIMITS) {
                empty_index = index;
            }
            continue;
        }

        if (strcmp(rate_limits[index].client_ip, client_ip) == 0 &&
            strcmp(rate_limits[index].scope, scope) == 0) {
            if (rate_limits[index].count >= limit) {
                if (message && message_size > 0) {
                    snprintf(message, message_size, "Limite de requisicoes atingido para %s", scope);
                }
                pthread_mutex_unlock(&auth_lock);
                return -1;
            }

            rate_limits[index].count++;
            pthread_mutex_unlock(&auth_lock);
            return 0;
        }
    }

    if (empty_index == MAX_RATE_LIMITS) {
        pthread_mutex_unlock(&auth_lock);
        return -1;
    }

    rate_limits[empty_index].active = 1;
    copy_string(rate_limits[empty_index].client_ip, sizeof(rate_limits[empty_index].client_ip), client_ip);
    copy_string(rate_limits[empty_index].scope, sizeof(rate_limits[empty_index].scope), scope);
    rate_limits[empty_index].count = 1;
    rate_limits[empty_index].window_started_at = now;
    pthread_mutex_unlock(&auth_lock);
    return 0;
}
