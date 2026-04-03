#ifndef AUTH_H
#define AUTH_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

int auth_init(void);
void auth_cleanup(void);

int auth_get_admin_code(char *dest, size_t dest_size);
int auth_create_ui_session(const char *client_ip, const char *admin_code, char *token_out, size_t token_size);
int auth_validate_ui_session(const char *client_ip, const char *token);

int auth_get_pairing_code(char *code_out, size_t code_size, time_t *expires_at);
int auth_issue_pair_token(const char *peer_ip,
                          const char *peer_name,
                          uint16_t peer_port,
                          const char *pair_code,
                          char *token_out,
                          size_t token_size,
                          time_t *expires_at);
int auth_store_peer_token(const char *peer_ip,
                          uint16_t peer_port,
                          const char *peer_name,
                          const char *token,
                          time_t expires_at);
int auth_get_peer_token(const char *peer_ip, uint16_t peer_port, char *token_out, size_t token_size);
int auth_validate_peer_token(const char *peer_ip, const char *token);

int auth_rate_limit_allow(const char *client_ip,
                          const char *scope,
                          unsigned int limit,
                          time_t window_seconds,
                          char *message,
                          size_t message_size);

#endif
