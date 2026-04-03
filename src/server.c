#include "server.h"
#include "peer_registry.h"
#include "transfer.h"
#include "ui.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define DOWNLOADS_DIR "downloads"
#define MAX_FILE_SIZE (1024 * 1024 * 100)  // 100MB

// Estrutura para processar upload
struct connection_info {
    struct MHD_PostProcessor *postprocessor;
    FILE *fp;
    char filename[256];
    char target_ip[64];
    char target_port[16];
    size_t bytes_received;
    int upload_in_progress;
    int file_saved;
    int response_status;
    char response_message[256];
};

// Função para criar diretório se não existir
static void ensure_downloads_dir() {
    struct stat st = {0};
    if (stat(DOWNLOADS_DIR, &st) == -1) {
        mkdir(DOWNLOADS_DIR, 0700);
    }
}

static void set_error(struct connection_info *con_info, int status, const char *message) {
    con_info->response_status = status;
    snprintf(con_info->response_message, sizeof(con_info->response_message), "%s", message);
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

static void sanitize_filename(const char *filename, char *dest, size_t dest_size) {
    const char *base;
    size_t j = 0;

    if (!dest || dest_size == 0) {
        return;
    }

    dest[0] = '\0';

    if (!filename || filename[0] == '\0') {
        snprintf(dest, dest_size, "upload.bin");
        return;
    }

    base = strrchr(filename, '/');
    if (!base) {
        base = strrchr(filename, '\\');
    }
    base = base ? base + 1 : filename;

    for (size_t i = 0; base[i] != '\0' && j + 1 < dest_size; i++) {
        char c = base[i];
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == '-') {
            dest[j++] = c;
        } else {
            dest[j++] = '_';
        }
    }

    dest[j] = '\0';

    if (dest[0] == '\0') {
        snprintf(dest, dest_size, "upload.bin");
    }
}

static int finalize_uploaded_file(struct connection_info *con_info) {
    if (!con_info->fp) {
        return 0;
    }

    if (fflush(con_info->fp) != 0) {
        fclose(con_info->fp);
        con_info->fp = NULL;
        set_error(con_info, MHD_HTTP_INTERNAL_SERVER_ERROR, "Falha ao salvar o arquivo recebido");
        return -1;
    }

    if (fclose(con_info->fp) != 0) {
        con_info->fp = NULL;
        set_error(con_info, MHD_HTTP_INTERNAL_SERVER_ERROR, "Falha ao fechar o arquivo recebido");
        return -1;
    }

    con_info->fp = NULL;
    con_info->file_saved = 1;

    if (con_info->upload_in_progress) {
        printf("✅ Arquivo salvo: %s\n", con_info->filename);
    }

    return 0;
}

static enum MHD_Result queue_json_response(struct MHD_Connection *connection,
                                           unsigned int status_code,
                                           const char *status_text,
                                           const char *message) {
    char response_text[512];
    struct MHD_Response *response;
    enum MHD_Result ret;

    snprintf(response_text, sizeof(response_text),
             "{\"status\":\"%s\",\"message\":\"%s\"}",
             status_text, message);

    response = MHD_create_response_from_buffer(strlen(response_text),
                                               (void *)response_text,
                                               MHD_RESPMEM_MUST_COPY);
    if (!response) {
        return MHD_NO;
    }

    MHD_add_response_header(response, "Content-Type", "application/json");
    ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);
    return ret;
}

// Callback chamado para cada parte do upload (multipart)
static enum MHD_Result iterate_post(void *coninfo_cls, enum MHD_ValueKind kind,
                    const char *key, const char *filename, const char *content_type,
                    const char *transfer_encoding, const char *data,
                    uint64_t off, size_t size) {
    struct connection_info *con_info = coninfo_cls;
    (void)kind;
    (void)content_type;
    (void)transfer_encoding;

    if (con_info->response_status >= 400) {
        return MHD_YES;
    }
    
    // Se é o campo "file"
    if (strcmp(key, "file") == 0) {
        if (filename && !con_info->fp) {
            char safe_filename[128];

            // Primeira vez - abrir arquivo para escrita
            ensure_downloads_dir();
            sanitize_filename(filename, safe_filename, sizeof(safe_filename));
            
            // Criar nome único com timestamp
            time_t now = time(NULL);
            snprintf(con_info->filename, sizeof(con_info->filename),
                    "%s/%ld_%s", DOWNLOADS_DIR, now, safe_filename);
            
            con_info->fp = fopen(con_info->filename, "wb");
            if (!con_info->fp) {
                printf("❌ Erro ao criar arquivo: %s\n", con_info->filename);
                set_error(con_info, MHD_HTTP_INTERNAL_SERVER_ERROR, "Nao foi possivel criar o arquivo recebido");
                return MHD_YES;
            }
            
            printf("📥 Recebendo: %s\n", safe_filename);
            con_info->upload_in_progress = 1;
        }
        
        // Escrever dados recebidos
        if (con_info->fp && size > 0) {
            if (con_info->bytes_received + size > MAX_FILE_SIZE) {
                set_error(con_info, MHD_HTTP_CONTENT_TOO_LARGE, "Arquivo excede o limite de 100 MB");
                return MHD_YES;
            }

            if (fwrite(data, 1, size, con_info->fp) != size) {
                set_error(con_info, MHD_HTTP_INTERNAL_SERVER_ERROR, "Falha ao gravar o arquivo recebido");
            } else {
                con_info->bytes_received += size;
            }
        }
    }
    
    // Se é o campo "target_ip" ou "target_port" (para envio)
    if (strcmp(key, "target_ip") == 0) {
        copy_form_value(con_info->target_ip, sizeof(con_info->target_ip), data, off, size);
    } else if (strcmp(key, "target_port") == 0) {
        copy_form_value(con_info->target_port, sizeof(con_info->target_port), data, off, size);
    }
    
    return MHD_YES;
}

// Callback chamado quando a requisição termina
static void request_completed(void *cls, struct MHD_Connection *connection,
                             void **con_cls, enum MHD_RequestTerminationCode toe) {
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

    if (con_info->fp) {
        fclose(con_info->fp);
    }

    if (!con_info->file_saved && con_info->filename[0] != '\0') {
        unlink(con_info->filename);
    }
    
    free(con_info);
    *con_cls = NULL;
}

// Callback principal HTTP
static enum MHD_Result answer_to_connection(void *cls, struct MHD_Connection *connection,
                          const char *url, const char *method,
                          const char *version, const char *upload_data,
                          size_t *upload_data_size, void **con_cls) {
    struct MHD_Response *response;
    enum MHD_Result ret;
    (void)cls;
    (void)version;

    // Primeira chamada - configurar estrutura de conexão
    if (*con_cls == NULL) {
        struct connection_info *con_info = calloc(1, sizeof(struct connection_info));
        if (!con_info) {
            return MHD_NO;
        }
        con_info->response_status = MHD_HTTP_OK;
        snprintf(con_info->response_message, sizeof(con_info->response_message), "OK");

        if (strcmp(method, "POST") == 0) {
            con_info->postprocessor = MHD_create_post_processor(connection, 65536,
                                                                iterate_post, con_info);
            if (!con_info->postprocessor) {
                free(con_info);
                return MHD_NO;
            }
        }
        
        *con_cls = (void*)con_info;
        return MHD_YES;
    }
    
    // Servir página principal
    if (strcmp(url, "/") == 0 && strcmp(method, "GET") == 0) {
        char *page = ui_render_index_page(NULL);
        if (!page) {
            return queue_json_response(connection,
                                       MHD_HTTP_INTERNAL_SERVER_ERROR,
                                       "error",
                                       "Falha ao renderizar a interface");
        }

        response = MHD_create_response_from_buffer(strlen(page),
                                                   (void*)page,
                                                   MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(response, "Content-Type", "text/html; charset=utf-8");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }
    
    // API: listar dispositivos
    if (strcmp(url, "/api/devices") == 0 && strcmp(method, "GET") == 0) {
        char *json = NULL;

        if (peer_registry_snapshot_json(&json) != 0) {
            return queue_json_response(connection,
                                       MHD_HTTP_INTERNAL_SERVER_ERROR,
                                       "error",
                                       "Falha ao serializar os peers descobertos");
        }

        response = MHD_create_response_from_buffer(strlen(json),
                                                   (void*)json,
                                                   MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(response, "Content-Type", "application/json");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }
    
    // Upload de arquivo
    if (strcmp(url, "/upload") == 0 && strcmp(method, "POST") == 0) {
        struct connection_info *con_info = *con_cls;
        
        if (*upload_data_size != 0) {
            // Processar dados do upload
            if (con_info->postprocessor &&
                MHD_post_process(con_info->postprocessor, upload_data, *upload_data_size) != MHD_YES &&
                con_info->response_status < 400) {
                set_error(con_info, MHD_HTTP_BAD_REQUEST, "Falha ao processar os dados do upload");
            }
            *upload_data_size = 0;
            return MHD_YES;
        } else {
            if (con_info->response_status >= 400) {
                finalize_uploaded_file(con_info);
                return queue_json_response(connection,
                                           con_info->response_status,
                                           "error",
                                           con_info->response_message);
            }

            if (!con_info->upload_in_progress) {
                return queue_json_response(connection,
                                           MHD_HTTP_BAD_REQUEST,
                                           "error",
                                           "Nenhum arquivo foi enviado");
            }

            if (finalize_uploaded_file(con_info) != 0) {
                return queue_json_response(connection,
                                           con_info->response_status,
                                           "error",
                                           con_info->response_message);
            }

            return queue_json_response(connection,
                                       MHD_HTTP_OK,
                                       "ok",
                                       "Arquivo recebido com sucesso");
        }
    }
    
    // API: enviar arquivo para outro dispositivo
    if (strcmp(url, "/api/send") == 0 && strcmp(method, "POST") == 0) {
        struct connection_info *con_info = *con_cls;
        
        if (*upload_data_size != 0) {
            if (con_info->postprocessor &&
                MHD_post_process(con_info->postprocessor, upload_data, *upload_data_size) != MHD_YES &&
                con_info->response_status < 400) {
                set_error(con_info, MHD_HTTP_BAD_REQUEST, "Falha ao processar os dados do envio");
            }
            *upload_data_size = 0;
            return MHD_YES;
        } else {
            char *endptr = NULL;
            long port;

            if (con_info->response_status >= 400) {
                finalize_uploaded_file(con_info);
                return queue_json_response(connection,
                                           con_info->response_status,
                                           "error",
                                           con_info->response_message);
            }

            if (!con_info->upload_in_progress) {
                return queue_json_response(connection,
                                           MHD_HTTP_BAD_REQUEST,
                                           "error",
                                           "Nenhum arquivo foi enviado");
            }

            if (con_info->target_ip[0] == '\0' || con_info->target_port[0] == '\0') {
                return queue_json_response(connection,
                                           MHD_HTTP_BAD_REQUEST,
                                           "error",
                                           "Destino invalido para o envio");
            }

            port = strtol(con_info->target_port, &endptr, 10);
            if (*con_info->target_port == '\0' || (endptr && *endptr != '\0') || port <= 0 || port > 65535) {
                return queue_json_response(connection,
                                           MHD_HTTP_BAD_REQUEST,
                                           "error",
                                           "Porta de destino invalida");
            }

            if (finalize_uploaded_file(con_info) != 0) {
                return queue_json_response(connection,
                                           con_info->response_status,
                                           "error",
                                           con_info->response_message);
            }

            printf("📤 Encaminhando %s para %s:%ld\n",
                   con_info->filename,
                   con_info->target_ip,
                   port);

            if (transfer_send_file(con_info->filename, con_info->target_ip, (int)port, NULL) != 0) {
                return queue_json_response(connection,
                                           MHD_HTTP_BAD_GATEWAY,
                                           "error",
                                           "Falha ao enviar o arquivo para o dispositivo remoto");
            }

            return queue_json_response(connection,
                                       MHD_HTTP_OK,
                                       "ok",
                                       "Arquivo enviado com sucesso");
        }
    }
    
    // 404
    const char *not_found = "404 Not Found";
    response = MHD_create_response_from_buffer(strlen(not_found),
                                               (void*)not_found,
                                               MHD_RESPMEM_PERSISTENT);
    ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
    MHD_destroy_response(response);
    return ret;
}

// Inicializar servidor
struct MHD_Daemon* server_start(int port) {
    return MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_THREAD_PER_CONNECTION, port,
                           NULL, NULL,
                           &answer_to_connection, NULL,
                           MHD_OPTION_NOTIFY_COMPLETED, request_completed, NULL,
                           MHD_OPTION_END);
}

// Parar servidor
void server_stop(struct MHD_Daemon *daemon) {
    if (daemon) {
        MHD_stop_daemon(daemon);
    }
}
