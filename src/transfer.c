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

// Enviar arquivo via HTTP POST
int transfer_send_file(const char *filepath, const char *dest_ip, int dest_port,
                       progress_callback callback) {
    CURL *curl;
    CURLcode res;
    curl_mime *mime = NULL;
    curl_mimepart *part = NULL;
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
        return -1;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code < 200 || response_code >= 300) {
        printf("❌ Dispositivo remoto respondeu com HTTP %ld\n", response_code);
        curl_easy_cleanup(curl);
        curl_mime_free(mime);
        return -1;
    }

    printf("✅ Arquivo enviado com sucesso!\n");

    curl_easy_cleanup(curl);
    curl_mime_free(mime);
    return 0;
}
