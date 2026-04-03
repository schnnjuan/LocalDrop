#include "transfer.h"

#include <arpa/inet.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Estrutura para tracking de progresso
struct upload_status {
    size_t total_bytes;
    size_t uploaded_bytes;
    progress_callback callback;
};

struct response_buffer {
    char *data;
    size_t length;
};

static int curl_ready = 0;

// Callback de progresso do CURL
static int progress_func(void *ptr, curl_off_t total_to_download, curl_off_t downloaded,
                        curl_off_t total_to_upload, curl_off_t uploaded) {
    struct upload_status *status = (struct upload_status *)ptr;

    (void)total_to_download;
    (void)downloaded;

    if (status->callback && total_to_upload > 0 && uploaded >= 0) {
        status->uploaded_bytes = (size_t)uploaded;
        status->callback((size_t)uploaded, (size_t)total_to_upload);
    }

    return 0;
}

int transfer_global_init(void) {
    if (curl_ready) {
        return 0;
    }

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return -1;
    }

    curl_ready = 1;
    return 0;
}

void transfer_global_cleanup(void) {
    if (!curl_ready) {
        return;
    }

    curl_global_cleanup();
    curl_ready = 0;
}

static int validate_destination_ip(const char *dest_ip) {
    struct in6_addr ipv6_addr;
    struct in_addr ipv4_addr;

    if (!dest_ip || dest_ip[0] == '\0') {
        return -1;
    }

    if (inet_pton(AF_INET, dest_ip, &ipv4_addr) == 1) {
        return 0;
    }

    if (inet_pton(AF_INET6, dest_ip, &ipv6_addr) == 1) {
        return 0;
    }

    return -1;
}

static size_t write_response(void *contents, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    struct response_buffer *buffer = userdata;
    char *resized;

    if (!buffer || total == 0) {
        return 0;
    }

    resized = realloc(buffer->data, buffer->length + total + 1);
    if (!resized) {
        return 0;
    }

    buffer->data = resized;
    memcpy(buffer->data + buffer->length, contents, total);
    buffer->length += total;
    buffer->data[buffer->length] = '\0';
    return total;
}

static int extract_json_string_field(const char *json, const char *field, char *dest, size_t dest_size) {
    char pattern[64];
    const char *start;
    const char *end;
    size_t length;

    if (!json || !field || !dest || dest_size == 0) {
        return -1;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\":\"", field);
    start = strstr(json, pattern);
    if (!start) {
        dest[0] = '\0';
        return -1;
    }

    start += strlen(pattern);
    end = strchr(start, '"');
    if (!end) {
        dest[0] = '\0';
        return -1;
    }

    length = (size_t)(end - start);
    if (length >= dest_size) {
        length = dest_size - 1;
    }

    memcpy(dest, start, length);
    dest[length] = '\0';
    return 0;
}

static int extract_json_long_field(const char *json, const char *field, long *value) {
    char pattern[64];
    const char *start;

    if (!json || !field || !value) {
        return -1;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\":", field);
    start = strstr(json, pattern);
    if (!start) {
        return -1;
    }

    start += strlen(pattern);
    *value = strtol(start, NULL, 10);
    return 0;
}

// Enviar arquivo via HTTP POST
int transfer_send_file(const char *filepath, const char *dest_ip, int dest_port,
                       const char *pair_token, progress_callback callback) {
    CURL *curl;
    CURLcode res;
    curl_mime *mime = NULL;
    curl_mimepart *part = NULL;
    struct curl_slist *headers = NULL;
    char url[512];
    struct upload_status status = {0};
    long response_code = 0;
    FILE *f;

    if (!curl_ready || validate_destination_ip(dest_ip) != 0) {
        printf("❌ Destino invalido para envio\n");
        return -1;
    }

    f = fopen(filepath, "rb");
    if (!f) {
        printf("❌ Arquivo não encontrado: %s\n", filepath);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    status.total_bytes = (size_t)ftell(f);
    fclose(f);

    status.callback = callback;

    printf("📤 Enviando %s (%zu bytes) para %s:%d\n",
           filepath, status.total_bytes, dest_ip, dest_port);

    curl = curl_easy_init();
    if (!curl) {
        printf("❌ Erro ao inicializar CURL\n");
        return -1;
    }

    mime = curl_mime_init(curl);
    if (!mime) {
        printf("❌ Erro ao criar payload do upload\n");
        curl_easy_cleanup(curl);
        return -1;
    }

    part = curl_mime_addpart(mime);
    if (!part ||
        curl_mime_name(part, "file") != CURLE_OK ||
        curl_mime_filedata(part, filepath) != CURLE_OK) {
        printf("❌ Erro ao anexar arquivo para envio\n");
        curl_mime_free(mime);
        curl_easy_cleanup(curl);
        return -1;
    }

    if (strchr(dest_ip, ':')) {
        snprintf(url, sizeof(url), "http://[%s]:%d/upload", dest_ip, dest_port);
    } else {
        snprintf(url, sizeof(url), "http://%s:%d/upload", dest_ip, dest_port);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    if (pair_token && pair_token[0] != '\0') {
        char header_value[128];

        snprintf(header_value, sizeof(header_value), "X-LocalDrop-Pair-Token: %s", pair_token);
        headers = curl_slist_append(headers, header_value);
        if (headers) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }
    }
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_func);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &status);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        printf("❌ Erro no envio: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        curl_mime_free(mime);
        if (headers) {
            curl_slist_free_all(headers);
        }
        return -1;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code < 200 || response_code >= 300) {
        printf("❌ Dispositivo remoto respondeu com HTTP %ld\n", response_code);
        curl_easy_cleanup(curl);
        curl_mime_free(mime);
        if (headers) {
            curl_slist_free_all(headers);
        }
        return -1;
    }

    printf("✅ Arquivo enviado com sucesso!\n");

    curl_easy_cleanup(curl);
    curl_mime_free(mime);
    if (headers) {
        curl_slist_free_all(headers);
    }
    return 0;
}

int transfer_pair_peer(const char *dest_ip,
                       uint16_t dest_port,
                       const char *peer_name,
                       uint16_t peer_port,
                       const char *pair_code,
                       char *token_out,
                       size_t token_size,
                       long *expires_at,
                       char *message_out,
                       size_t message_size) {
    CURL *curl;
    CURLcode res;
    curl_mime *mime = NULL;
    curl_mimepart *part = NULL;
    struct response_buffer response = {0};
    char url[512];
    long response_code = 0;

    if (!curl_ready || validate_destination_ip(dest_ip) != 0 || !pair_code || !token_out || token_size == 0) {
        return -1;
    }

    curl = curl_easy_init();
    if (!curl) {
        return -1;
    }

    mime = curl_mime_init(curl);
    if (!mime) {
        curl_easy_cleanup(curl);
        return -1;
    }

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "pair_code");
    curl_mime_data(part, pair_code, CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "peer_name");
    curl_mime_data(part, peer_name ? peer_name : "localdrop-peer", CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "peer_port");
    snprintf(url, sizeof(url), "%u", (unsigned int)peer_port);
    curl_mime_data(part, url, CURL_ZERO_TERMINATED);

    if (strchr(dest_ip, ':')) {
        snprintf(url, sizeof(url), "http://[%s]:%u/api/pair/exchange", dest_ip, (unsigned int)dest_port);
    } else {
        snprintf(url, sizeof(url), "http://%s:%u/api/pair/exchange", dest_ip, (unsigned int)dest_port);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        if (message_out && message_size > 0) {
            snprintf(message_out, message_size, "%s", curl_easy_strerror(res));
        }
        curl_mime_free(mime);
        curl_easy_cleanup(curl);
        free(response.data);
        return -1;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_mime_free(mime);
    curl_easy_cleanup(curl);

    if (response_code < 200 || response_code >= 300) {
        if (message_out && message_size > 0) {
            if (extract_json_string_field(response.data, "message", message_out, message_size) != 0) {
                snprintf(message_out, message_size, "Pareamento recusado pelo peer remoto");
            }
        }
        free(response.data);
        return -1;
    }

    if (extract_json_string_field(response.data, "token", token_out, token_size) != 0) {
        free(response.data);
        return -1;
    }

    if (expires_at) {
        long parsed_expires_at = 0;
        if (extract_json_long_field(response.data, "expires_at", &parsed_expires_at) == 0) {
            *expires_at = parsed_expires_at;
        } else {
            *expires_at = 0;
        }
    }

    if (message_out && message_size > 0) {
        if (extract_json_string_field(response.data, "message", message_out, message_size) != 0) {
            snprintf(message_out, message_size, "Pareamento realizado com sucesso");
        }
    }

    free(response.data);
    return 0;
}
