#include "server.h"

#include "auth.h"
#include "json_utils.h"
#include "peer_registry.h"
#include "storage.h"
#include "transfer.h"
#include "transfer_queue.h"
#include "ui.h"

#include <arpa/inet.h>
#include <errno.h>
#include <microhttpd.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAX_FILE_SIZE (1024 * 1024 * 100)
#define UI_TOKEN_HEADER "X-LocalDrop-UI-Token"
#define PAIR_TOKEN_HEADER "X-LocalDrop-Pair-Token"

struct connection_info {
    struct MHD_PostProcessor *postprocessor;
    UploadStage upload_stage;
    char filename[512];
    char target_ip[64];
    char target_port[16];
    char target_name[256];
    char admin_code[16];
    char pair_code[16];
    char peer_name[256];
    char peer_port[16];
    char client_ip[46];
    int upload_in_progress;
    int file_saved;
    int response_status;
    int auth_checked;
    int request_authorized;
    char response_message[256];
};

static int server_port = 0;

static void set_error(struct connection_info *con_info, int status, const char *message) {
    if (!con_info) {
        return;
    }

    con_info->response_status = status;
    snprintf(con_info->response_message, sizeof(con_info->response_message), "%s", message ? message : "Erro");
}

static void copy_form_value(char *dest, size_t dest_size, const char *data, uint64_t off, size_t size) {
    size_t copy_size;

    if (!dest || !data || dest_size == 0 || off >= dest_size - 1) {
        return;
    }

    copy_size = size;
    if (off + copy_size >= dest_size) {
        copy_size = dest_size - off - 1;
    }

    memcpy(dest + off, data, copy_size);
    dest[off + copy_size] = '\0';
}

static int get_client_ip(struct MHD_Connection *connection, char *dest, size_t dest_size) {
    const union MHD_ConnectionInfo *connection_info;
    const struct sockaddr *address;
    const void *source = NULL;

    if (!connection || !dest || dest_size == 0) {
        return -1;
    }

    connection_info = MHD_get_connection_info(connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS);
    if (!connection_info || !connection_info->client_addr) {
        return -1;
    }

    address = connection_info->client_addr;
    if (address->sa_family == AF_INET) {
        source = &((const struct sockaddr_in *)address)->sin_addr;
    } else if (address->sa_family == AF_INET6) {
        source = &((const struct sockaddr_in6 *)address)->sin6_addr;
    } else {
        return -1;
    }

    if (!inet_ntop(address->sa_family, source, dest, (socklen_t)dest_size)) {
        return -1;
    }

    return 0;
}

static int validate_ip_address(const char *value) {
    struct in_addr ipv4_addr;
    struct in6_addr ipv6_addr;

    if (!value || value[0] == '\0') {
        return -1;
    }

    if (inet_pton(AF_INET, value, &ipv4_addr) == 1) {
        return 0;
    }

    if (inet_pton(AF_INET6, value, &ipv6_addr) == 1) {
        return 0;
    }

    return -1;
}

static int commit_uploaded_file(struct connection_info *con_info) {
    if (upload_stage_commit(&con_info->upload_stage,
                            con_info->filename,
                            sizeof(con_info->filename),
                            con_info->response_message,
                            sizeof(con_info->response_message)) != 0) {
        con_info->response_status = MHD_HTTP_INTERNAL_SERVER_ERROR;
        return -1;
    }

    con_info->file_saved = 1;
    printf("✅ Arquivo salvo: %s\n", con_info->filename);
    return 0;
}

static int seal_uploaded_stage(struct connection_info *con_info) {
    if (upload_stage_seal(&con_info->upload_stage,
                          con_info->response_message,
                          sizeof(con_info->response_message)) != 0) {
        con_info->response_status = MHD_HTTP_INTERNAL_SERVER_ERROR;
        return -1;
    }

    snprintf(con_info->filename, sizeof(con_info->filename), "%s", con_info->upload_stage.staging_path);
    con_info->file_saved = 1;
    return 0;
}

static enum MHD_Result queue_json_payload(struct MHD_Connection *connection, unsigned int status_code, char *payload) {
    struct MHD_Response *response;
    enum MHD_Result result;

    response = MHD_create_response_from_buffer(strlen(payload), (void *)payload, MHD_RESPMEM_MUST_FREE);
    if (!response) {
        free(payload);
        return MHD_NO;
    }

    MHD_add_response_header(response, "Content-Type", "application/json");
    result = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);
    return result;
}

static enum MHD_Result queue_json_status_message(struct MHD_Connection *connection,
                                                 unsigned int status_code,
                                                 const char *status_text,
                                                 const char *message) {
    char *payload = NULL;
    size_t length = 0;
    size_t capacity = 0;

    if (json_buffer_append(&payload, &length, &capacity, "{\"status\":\"") != 0 ||
        json_buffer_append_escaped(&payload, &length, &capacity, status_text) != 0 ||
        json_buffer_append(&payload, &length, &capacity, "\",\"message\":\"") != 0 ||
        json_buffer_append_escaped(&payload, &length, &capacity, message) != 0 ||
        json_buffer_append(&payload, &length, &capacity, "\"}") != 0) {
        free(payload);
        return MHD_NO;
    }

    return queue_json_payload(connection, status_code, payload);
}

static enum MHD_Result queue_json_session_response(struct MHD_Connection *connection,
                                                   unsigned int status_code,
                                                   const char *message,
                                                   const char *token) {
    char *payload = NULL;
    size_t length = 0;
    size_t capacity = 0;

    if (json_buffer_append(&payload, &length, &capacity, "{\"status\":\"ok\",\"message\":\"") != 0 ||
        json_buffer_append_escaped(&payload, &length, &capacity, message) != 0 ||
        json_buffer_append(&payload, &length, &capacity, "\",\"token\":\"") != 0 ||
        json_buffer_append_escaped(&payload, &length, &capacity, token) != 0 ||
        json_buffer_append(&payload, &length, &capacity, "\"}") != 0) {
        free(payload);
        return MHD_NO;
    }

    return queue_json_payload(connection, status_code, payload);
}

static enum MHD_Result queue_json_pair_code_response(struct MHD_Connection *connection,
                                                     const char *code,
                                                     time_t expires_at) {
    char *payload = NULL;
    size_t length = 0;
    size_t capacity = 0;

    if (json_buffer_append(&payload, &length, &capacity, "{\"status\":\"ok\",\"code\":\"") != 0 ||
        json_buffer_append_escaped(&payload, &length, &capacity, code) != 0 ||
        json_buffer_appendf(&payload, &length, &capacity, "\",\"expires_at\":%ld}", (long)expires_at) != 0) {
        free(payload);
        return MHD_NO;
    }

    return queue_json_payload(connection, MHD_HTTP_OK, payload);
}

static enum MHD_Result queue_json_pair_response(struct MHD_Connection *connection,
                                                const char *message,
                                                const char *token,
                                                time_t expires_at) {
    char *payload = NULL;
    size_t length = 0;
    size_t capacity = 0;

    if (json_buffer_append(&payload, &length, &capacity, "{\"status\":\"ok\",\"message\":\"") != 0 ||
        json_buffer_append_escaped(&payload, &length, &capacity, message) != 0 ||
        json_buffer_append(&payload, &length, &capacity, "\",\"token\":\"") != 0 ||
        json_buffer_append_escaped(&payload, &length, &capacity, token) != 0 ||
        json_buffer_appendf(&payload, &length, &capacity, "\",\"expires_at\":%ld}", (long)expires_at) != 0) {
        free(payload);
        return MHD_NO;
    }

    return queue_json_payload(connection, MHD_HTTP_OK, payload);
}

static enum MHD_Result queue_json_transfer_response(struct MHD_Connection *connection,
                                                    const char *message,
                                                    const char *job_id) {
    char *payload = NULL;
    size_t length = 0;
    size_t capacity = 0;

    if (json_buffer_append(&payload, &length, &capacity, "{\"status\":\"ok\",\"message\":\"") != 0 ||
        json_buffer_append_escaped(&payload, &length, &capacity, message) != 0 ||
        json_buffer_append(&payload, &length, &capacity, "\",\"job_id\":\"") != 0 ||
        json_buffer_append_escaped(&payload, &length, &capacity, job_id) != 0 ||
        json_buffer_append(&payload, &length, &capacity, "\"}") != 0) {
        free(payload);
        return MHD_NO;
    }

    return queue_json_payload(connection, MHD_HTTP_OK, payload);
}

static int rate_limit_request(struct connection_info *con_info,
                              const char *scope,
                              unsigned int limit,
                              time_t window_seconds) {
    char message[128];

    if (auth_rate_limit_allow(con_info->client_ip, scope, limit, window_seconds, message, sizeof(message)) != 0) {
        set_error(con_info, MHD_HTTP_TOO_MANY_REQUESTS, message[0] != '\0' ? message : "Limite de requisicoes atingido");
        return -1;
    }

    return 0;
}

static int authorize_ui_request(struct MHD_Connection *connection,
                                struct connection_info *con_info,
                                const char *scope,
                                unsigned int limit) {
    const char *ui_token;

    if (!con_info->auth_checked) {
        con_info->auth_checked = 1;

        if (rate_limit_request(con_info, scope, limit, 60) != 0) {
            return -1;
        }

        ui_token = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, UI_TOKEN_HEADER);
        if (!ui_token || auth_validate_ui_session(con_info->client_ip, ui_token) != 0) {
            set_error(con_info, MHD_HTTP_UNAUTHORIZED, "Sessao administrativa invalida ou expirada");
            con_info->request_authorized = 0;
            return -1;
        }

        con_info->request_authorized = 1;
    }

    return con_info->request_authorized ? 0 : -1;
}

static int authorize_upload_request(struct MHD_Connection *connection, struct connection_info *con_info) {
    const char *ui_token;
    const char *pair_token;

    if (!con_info->auth_checked) {
        con_info->auth_checked = 1;

        if (rate_limit_request(con_info, "upload", 20, 60) != 0) {
            return -1;
        }

        ui_token = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, UI_TOKEN_HEADER);
        if (ui_token && auth_validate_ui_session(con_info->client_ip, ui_token) == 0) {
            con_info->request_authorized = 1;
            return 0;
        }

        pair_token = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, PAIR_TOKEN_HEADER);
        if (pair_token && auth_validate_peer_token(con_info->client_ip, pair_token) == 0) {
            con_info->request_authorized = 1;
            return 0;
        }

        set_error(con_info, MHD_HTTP_UNAUTHORIZED, "Upload bloqueado: peer nao pareado ou sessao local ausente");
        con_info->request_authorized = 0;
        return -1;
    }

    return con_info->request_authorized ? 0 : -1;
}

static int validate_target_peer(struct connection_info *con_info, uint16_t *port_out, char *pair_token, size_t token_size) {
    char *endptr = NULL;
    long port_value;
    PeerInfo peer;

    if (validate_ip_address(con_info->target_ip) != 0) {
        set_error(con_info, MHD_HTTP_BAD_REQUEST, "Endereco IP de destino invalido");
        return -1;
    }

    port_value = strtol(con_info->target_port, &endptr, 10);
    if (con_info->target_port[0] == '\0' || !endptr || *endptr != '\0' || port_value <= 0 || port_value > 65535) {
        set_error(con_info, MHD_HTTP_BAD_REQUEST, "Porta de destino invalida");
        return -1;
    }

    if (peer_registry_find_by_endpoint(con_info->target_ip, (uint16_t)port_value, &peer) != 0 || !peer.active) {
        set_error(con_info, MHD_HTTP_FORBIDDEN, "Destino nao pertence a um peer descoberto ativo");
        return -1;
    }

    if (!peer.paired) {
        set_error(con_info, MHD_HTTP_FORBIDDEN, "Peer remoto ainda nao foi pareado");
        return -1;
    }

    if (auth_get_peer_token(con_info->target_ip, (uint16_t)port_value, pair_token, token_size) != 0) {
        set_error(con_info, MHD_HTTP_FORBIDDEN, "Nao existe token de pareamento valido para o peer remoto");
        return -1;
    }

    *port_out = (uint16_t)port_value;
    return 0;
}

static enum MHD_Result iterate_post(void *coninfo_cls,
                                    enum MHD_ValueKind kind,
                                    const char *key,
                                    const char *filename,
                                    const char *content_type,
                                    const char *transfer_encoding,
                                    const char *data,
                                    uint64_t off,
                                    size_t size) {
    struct connection_info *con_info = coninfo_cls;

    (void)kind;
    (void)content_type;
    (void)transfer_encoding;

    if (con_info->response_status >= 400) {
        return MHD_YES;
    }

    if (strcmp(key, "file") == 0) {
        if (filename && !con_info->upload_stage.fp && !con_info->upload_stage.sealed) {
            if (upload_stage_open(&con_info->upload_stage,
                                  filename,
                                  con_info->response_message,
                                  sizeof(con_info->response_message)) != 0) {
                con_info->response_status = MHD_HTTP_INTERNAL_SERVER_ERROR;
                return MHD_YES;
            }

            printf("📥 Recebendo: %s\n", con_info->upload_stage.original_name);
            con_info->upload_in_progress = 1;
        }

        if ((con_info->upload_stage.fp || con_info->upload_stage.sealed) && size > 0) {
            if (upload_stage_write(&con_info->upload_stage,
                                   data,
                                   size,
                                   MAX_FILE_SIZE,
                                   con_info->response_message,
                                   sizeof(con_info->response_message)) != 0) {
                con_info->response_status =
                    strstr(con_info->response_message, "limite") ? MHD_HTTP_CONTENT_TOO_LARGE : MHD_HTTP_INTERNAL_SERVER_ERROR;
                return MHD_YES;
            }
        }
    } else if (strcmp(key, "target_ip") == 0) {
        copy_form_value(con_info->target_ip, sizeof(con_info->target_ip), data, off, size);
    } else if (strcmp(key, "target_port") == 0) {
        copy_form_value(con_info->target_port, sizeof(con_info->target_port), data, off, size);
    } else if (strcmp(key, "target_name") == 0) {
        copy_form_value(con_info->target_name, sizeof(con_info->target_name), data, off, size);
    } else if (strcmp(key, "admin_code") == 0) {
        copy_form_value(con_info->admin_code, sizeof(con_info->admin_code), data, off, size);
    } else if (strcmp(key, "pair_code") == 0) {
        copy_form_value(con_info->pair_code, sizeof(con_info->pair_code), data, off, size);
    } else if (strcmp(key, "peer_name") == 0) {
        copy_form_value(con_info->peer_name, sizeof(con_info->peer_name), data, off, size);
    } else if (strcmp(key, "peer_port") == 0) {
        copy_form_value(con_info->peer_port, sizeof(con_info->peer_port), data, off, size);
    }

    return MHD_YES;
}

static void request_completed(void *cls,
                              struct MHD_Connection *connection,
                              void **con_cls,
                              enum MHD_RequestTerminationCode toe) {
    struct connection_info *con_info = *con_cls;

    (void)cls;
    (void)connection;
    (void)toe;

    if (!con_info) {
        return;
    }

    if (con_info->postprocessor) {
        MHD_destroy_post_processor(con_info->postprocessor);
    }

    if (!con_info->file_saved) {
        upload_stage_abort(&con_info->upload_stage);
    }

    free(con_info);
    *con_cls = NULL;
}

static enum MHD_Result answer_to_connection(void *cls,
                                            struct MHD_Connection *connection,
                                            const char *url,
                                            const char *method,
                                            const char *version,
                                            const char *upload_data,
                                            size_t *upload_data_size,
                                            void **con_cls) {
    struct MHD_Response *response;
    struct connection_info *con_info;
    enum MHD_Result result;

    (void)cls;
    (void)version;

    if (*con_cls == NULL) {
        con_info = calloc(1, sizeof(*con_info));
        if (!con_info) {
            return MHD_NO;
        }

        con_info->response_status = MHD_HTTP_OK;
        snprintf(con_info->response_message, sizeof(con_info->response_message), "%s", "OK");
        if (get_client_ip(connection, con_info->client_ip, sizeof(con_info->client_ip)) != 0) {
            snprintf(con_info->client_ip, sizeof(con_info->client_ip), "%s", "unknown");
        }

        if (strcmp(method, "POST") == 0) {
            con_info->postprocessor = MHD_create_post_processor(connection, 65536, iterate_post, con_info);
            if (!con_info->postprocessor) {
                free(con_info);
                return MHD_NO;
            }
        }

        *con_cls = con_info;
        return MHD_YES;
    }

    con_info = *con_cls;

    if (strcmp(url, "/") == 0 && strcmp(method, "GET") == 0) {
        char *page = ui_render_index_page(NULL);

        if (!page) {
            return queue_json_status_message(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "error", "Falha ao renderizar a interface");
        }

        response = MHD_create_response_from_buffer(strlen(page), page, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(response, "Content-Type", "text/html; charset=utf-8");
        result = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return result;
    }

    if (strcmp(url, "/api/devices") == 0 && strcmp(method, "GET") == 0) {
        char *json = NULL;

        if (peer_registry_snapshot_json(&json) != 0) {
            return queue_json_status_message(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "error", "Falha ao serializar os peers descobertos");
        }

        return queue_json_payload(connection, MHD_HTTP_OK, json);
    }

    if (strcmp(url, "/api/pair/local-code") == 0 && strcmp(method, "GET") == 0) {
        char code[16];
        time_t expires_at = 0;

        if (authorize_ui_request(connection, con_info, "pair_code", 20) != 0) {
            return queue_json_status_message(connection, (unsigned int)con_info->response_status, "error", con_info->response_message);
        }

        if (auth_get_pairing_code(code, sizeof(code), &expires_at) != 0) {
            return queue_json_status_message(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "error", "Nao foi possivel gerar o codigo de pareamento");
        }

        return queue_json_pair_code_response(connection, code, expires_at);
    }

    if (strcmp(url, "/api/session/unlock") == 0 && strcmp(method, "POST") == 0) {
        char token[65];

        if (*upload_data_size != 0) {
            if (!con_info->auth_checked) {
                con_info->auth_checked = 1;
                if (rate_limit_request(con_info, "session_unlock", 8, 60) != 0) {
                    *upload_data_size = 0;
                    return MHD_YES;
                }
            }

            if (con_info->response_status >= 400) {
                *upload_data_size = 0;
                return MHD_YES;
            }

            if (con_info->postprocessor &&
                MHD_post_process(con_info->postprocessor, upload_data, *upload_data_size) != MHD_YES &&
                con_info->response_status < 400) {
                set_error(con_info, MHD_HTTP_BAD_REQUEST, "Falha ao processar os dados de autenticacao");
            }

            *upload_data_size = 0;
            return MHD_YES;
        }

        if (con_info->response_status >= 400) {
            return queue_json_status_message(connection, (unsigned int)con_info->response_status, "error", con_info->response_message);
        }

        if (con_info->admin_code[0] == '\0') {
            return queue_json_status_message(connection, MHD_HTTP_BAD_REQUEST, "error", "Codigo administrativo ausente");
        }

        if (auth_create_ui_session(con_info->client_ip, con_info->admin_code, token, sizeof(token)) != 0) {
            return queue_json_status_message(connection, MHD_HTTP_UNAUTHORIZED, "error", "Codigo administrativo invalido");
        }

        return queue_json_session_response(connection, MHD_HTTP_OK, "Sessao destravada com sucesso", token);
    }

    if (strcmp(url, "/api/pair/exchange") == 0 && strcmp(method, "POST") == 0) {
        char token[65];
        time_t expires_at = 0;
        char *endptr = NULL;
        long peer_port_value;

        if (*upload_data_size != 0) {
            if (!con_info->auth_checked) {
                con_info->auth_checked = 1;
                if (rate_limit_request(con_info, "pair_exchange", 12, 60) != 0) {
                    *upload_data_size = 0;
                    return MHD_YES;
                }
            }

            if (con_info->response_status >= 400) {
                *upload_data_size = 0;
                return MHD_YES;
            }

            if (con_info->postprocessor &&
                MHD_post_process(con_info->postprocessor, upload_data, *upload_data_size) != MHD_YES &&
                con_info->response_status < 400) {
                set_error(con_info, MHD_HTTP_BAD_REQUEST, "Falha ao processar a requisicao de pareamento");
            }

            *upload_data_size = 0;
            return MHD_YES;
        }

        if (con_info->response_status >= 400) {
            return queue_json_status_message(connection, (unsigned int)con_info->response_status, "error", con_info->response_message);
        }

        peer_port_value = strtol(con_info->peer_port, &endptr, 10);
        if (con_info->pair_code[0] == '\0' ||
            con_info->peer_name[0] == '\0' ||
            con_info->peer_port[0] == '\0' ||
            !endptr || *endptr != '\0' || peer_port_value <= 0 || peer_port_value > 65535) {
            return queue_json_status_message(connection, MHD_HTTP_BAD_REQUEST, "error", "Dados de pareamento invalidos");
        }

        if (auth_issue_pair_token(con_info->client_ip,
                                  con_info->peer_name,
                                  (uint16_t)peer_port_value,
                                  con_info->pair_code,
                                  token,
                                  sizeof(token),
                                  &expires_at) != 0) {
            return queue_json_status_message(connection, MHD_HTTP_FORBIDDEN, "error", "Codigo de pareamento invalido ou expirado");
        }

        peer_registry_add_or_update(con_info->peer_name, con_info->client_ip, (uint16_t)peer_port_value);
        peer_registry_set_paired(con_info->client_ip, (uint16_t)peer_port_value, 1);
        return queue_json_pair_response(connection, "Peer pareado com sucesso", token, expires_at);
    }

    if (strcmp(url, "/api/transfers") == 0 && strcmp(method, "GET") == 0) {
        char *json = NULL;

        if (authorize_ui_request(connection, con_info, "transfer_status", 30) != 0) {
            return queue_json_status_message(connection, (unsigned int)con_info->response_status, "error", con_info->response_message);
        }

        if (transfer_status_list_json(&json) != 0) {
            return queue_json_status_message(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "error", "Nao foi possivel consultar a fila de transferencias");
        }

        return queue_json_payload(connection, MHD_HTTP_OK, json);
    }

    if (strncmp(url, "/api/transfers/", strlen("/api/transfers/")) == 0 && strcmp(method, "GET") == 0) {
        char *json = NULL;
        const char *job_id = url + strlen("/api/transfers/");

        if (authorize_ui_request(connection, con_info, "transfer_status", 30) != 0) {
            return queue_json_status_message(connection, (unsigned int)con_info->response_status, "error", con_info->response_message);
        }

        if (transfer_status_get(job_id, &json) != 0) {
            return queue_json_status_message(connection, MHD_HTTP_NOT_FOUND, "error", "Transferencia nao encontrada");
        }

        return queue_json_payload(connection, MHD_HTTP_OK, json);
    }

    if (strcmp(url, "/upload") == 0 && strcmp(method, "POST") == 0) {
        if (*upload_data_size != 0) {
            if (authorize_upload_request(connection, con_info) != 0) {
                *upload_data_size = 0;
                return MHD_YES;
            }

            if (con_info->postprocessor &&
                MHD_post_process(con_info->postprocessor, upload_data, *upload_data_size) != MHD_YES &&
                con_info->response_status < 400) {
                set_error(con_info, MHD_HTTP_BAD_REQUEST, "Falha ao processar os dados do upload");
            }

            *upload_data_size = 0;
            return MHD_YES;
        }

        if (con_info->response_status >= 400) {
            return queue_json_status_message(connection, (unsigned int)con_info->response_status, "error", con_info->response_message);
        }

        if (!con_info->upload_in_progress) {
            return queue_json_status_message(connection, MHD_HTTP_BAD_REQUEST, "error", "Nenhum arquivo foi enviado");
        }

        if (commit_uploaded_file(con_info) != 0) {
            return queue_json_status_message(connection, (unsigned int)con_info->response_status, "error", con_info->response_message);
        }

        return queue_json_status_message(connection, MHD_HTTP_OK, "ok", "Arquivo recebido com sucesso");
    }

    if (strcmp(url, "/api/pair") == 0 && strcmp(method, "POST") == 0) {
        if (*upload_data_size != 0) {
            if (authorize_ui_request(connection, con_info, "pair_initiate", 10) != 0) {
                *upload_data_size = 0;
                return MHD_YES;
            }

            if (con_info->postprocessor &&
                MHD_post_process(con_info->postprocessor, upload_data, *upload_data_size) != MHD_YES &&
                con_info->response_status < 400) {
                set_error(con_info, MHD_HTTP_BAD_REQUEST, "Falha ao processar o pareamento remoto");
            }

            *upload_data_size = 0;
            return MHD_YES;
        }

        if (con_info->response_status >= 400) {
            return queue_json_status_message(connection, (unsigned int)con_info->response_status, "error", con_info->response_message);
        }

        if (validate_ip_address(con_info->target_ip) != 0 || con_info->pair_code[0] == '\0') {
            return queue_json_status_message(connection, MHD_HTTP_BAD_REQUEST, "error", "Destino ou codigo de pareamento invalido");
        }

        {
            char token[65];
            char pair_message[256];
            char hostname[256];
            long expires_at = 0;
            char *endptr = NULL;
            long port_value = strtol(con_info->target_port, &endptr, 10);
            PeerInfo peer;

            if (con_info->target_port[0] == '\0' || !endptr || *endptr != '\0' || port_value <= 0 || port_value > 65535) {
                return queue_json_status_message(connection, MHD_HTTP_BAD_REQUEST, "error", "Porta do peer remoto invalida");
            }

            if (peer_registry_find_by_endpoint(con_info->target_ip, (uint16_t)port_value, &peer) != 0 || !peer.active) {
                return queue_json_status_message(connection, MHD_HTTP_FORBIDDEN, "error", "Pareamento permitido apenas com peers descobertos e ativos");
            }

            gethostname(hostname, sizeof(hostname));
            hostname[sizeof(hostname) - 1] = '\0';

            if (transfer_pair_peer(con_info->target_ip,
                                   (uint16_t)port_value,
                                   hostname,
                                   (uint16_t)server_port,
                                   con_info->pair_code,
                                   token,
                                   sizeof(token),
                                   &expires_at,
                                   pair_message,
                                   sizeof(pair_message)) != 0) {
                return queue_json_status_message(connection,
                                                 MHD_HTTP_BAD_GATEWAY,
                                                 "error",
                                                 pair_message[0] != '\0' ? pair_message : "Peer remoto recusou o pareamento");
            }

            auth_store_peer_token(con_info->target_ip,
                                  (uint16_t)port_value,
                                  con_info->target_name[0] != '\0' ? con_info->target_name : peer.name,
                                  token,
                                  (time_t)expires_at);
            peer_registry_set_paired(con_info->target_ip, (uint16_t)port_value, 1);
            return queue_json_status_message(connection, MHD_HTTP_OK, "ok", "Peer pareado com sucesso");
        }
    }

    if (strcmp(url, "/api/send") == 0 && strcmp(method, "POST") == 0) {
        if (*upload_data_size != 0) {
            if (authorize_ui_request(connection, con_info, "send_file", 12) != 0) {
                *upload_data_size = 0;
                return MHD_YES;
            }

            if (con_info->postprocessor &&
                MHD_post_process(con_info->postprocessor, upload_data, *upload_data_size) != MHD_YES &&
                con_info->response_status < 400) {
                set_error(con_info, MHD_HTTP_BAD_REQUEST, "Falha ao processar os dados do envio");
            }

            *upload_data_size = 0;
            return MHD_YES;
        }

        if (con_info->response_status >= 400) {
            return queue_json_status_message(connection, (unsigned int)con_info->response_status, "error", con_info->response_message);
        }

        if (!con_info->upload_in_progress) {
            return queue_json_status_message(connection, MHD_HTTP_BAD_REQUEST, "error", "Nenhum arquivo foi enviado");
        }

        {
            char job_id[65];
            char pair_token[65];
            uint16_t target_port = 0;

            if (validate_target_peer(con_info, &target_port, pair_token, sizeof(pair_token)) != 0) {
                return queue_json_status_message(connection, (unsigned int)con_info->response_status, "error", con_info->response_message);
            }

            if (seal_uploaded_stage(con_info) != 0) {
                return queue_json_status_message(connection, (unsigned int)con_info->response_status, "error", con_info->response_message);
            }

            if (transfer_enqueue(con_info->filename, con_info->target_ip, target_port, pair_token, job_id, sizeof(job_id)) != 0) {
                con_info->file_saved = 0;
                upload_stage_abort(&con_info->upload_stage);
                return queue_json_status_message(connection, MHD_HTTP_SERVICE_UNAVAILABLE, "error", "Fila de transferencia indisponivel");
            }

            return queue_json_transfer_response(connection, "Transferencia enfileirada com sucesso", job_id);
        }
    }

    response = MHD_create_response_from_buffer(strlen("404 Not Found"), (void *)"404 Not Found", MHD_RESPMEM_PERSISTENT);
    result = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
    MHD_destroy_response(response);
    return result;
}

struct MHD_Daemon *server_start(int port) {
    server_port = port;
    return MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_THREAD_PER_CONNECTION,
                            (uint16_t)port,
                            NULL,
                            NULL,
                            &answer_to_connection,
                            NULL,
                            MHD_OPTION_NOTIFY_COMPLETED,
                            request_completed,
                            NULL,
                            MHD_OPTION_END);
}

void server_stop(struct MHD_Daemon *daemon) {
    if (daemon) {
        MHD_stop_daemon(daemon);
    }
}
