#include "transfer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <sys/stat.h>

// Estrutura para tracking de progresso
struct upload_status {
    size_t total_bytes;
    size_t uploaded_bytes;
    progress_callback callback;
};

// Callback de progresso do CURL
static int progress_func(void *ptr, curl_off_t total_to_download, curl_off_t downloaded,
                        curl_off_t total_to_upload, curl_off_t uploaded) {
    struct upload_status *status = (struct upload_status *)ptr;
    
    if (status->callback && total_to_upload > 0) {
        status->uploaded_bytes = uploaded;
        status->callback(uploaded, total_to_upload);
    }
    
    return 0;
}

// Enviar arquivo via HTTP POST
int transfer_send_file(const char *filepath, const char *dest_ip, int dest_port,
                       progress_callback callback) {
    CURL *curl;
    CURLcode res;
    struct curl_httppost *formpost = NULL;
    struct curl_httppost *lastptr = NULL;
    char url[512];
    struct upload_status status = {0};
    
    // Verificar se arquivo existe
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        printf("❌ Arquivo não encontrado: %s\n", filepath);
        return -1;
    }
    
    // Obter tamanho do arquivo
    fseek(f, 0, SEEK_END);
    status.total_bytes = ftell(f);
    fclose(f);
    
    status.callback = callback;
    
    printf("📤 Enviando %s (%zu bytes) para %s:%d\n", 
           filepath, status.total_bytes, dest_ip, dest_port);
    
    // Inicializar CURL
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    
    if (!curl) {
        printf("❌ Erro ao inicializar CURL\n");
        return -1;
    }
    
    // Criar form com o arquivo
    curl_formadd(&formpost, &lastptr,
                 CURLFORM_COPYNAME, "file",
                 CURLFORM_FILE, filepath,
                 CURLFORM_END);
    
    // Configurar URL
    snprintf(url, sizeof(url), "http://%s:%d/upload", dest_ip, dest_port);
    
    // Configurar CURL
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
    
    // Configurar progresso
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_func);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &status);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    
    // Timeout
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);  // 5 minutos
    
    // Executar
    res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        printf("❌ Erro no envio: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        curl_formfree(formpost);
        curl_global_cleanup();
        return -1;
    }
    
    printf("✅ Arquivo enviado com sucesso!\n");
    
    // Limpar
    curl_easy_cleanup(curl);
    curl_formfree(formpost);
    curl_global_cleanup();
    
    return 0;
}