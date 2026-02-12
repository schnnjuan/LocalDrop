#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <microhttpd.h>
#include <qrencode.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <signal.h>
#include "discovery.h"

#define PORT 8080

volatile int keep_running = 1;

void sigint_handler(int sig) {
    keep_running = 0;
}

// HTML da interface (atualizado para mostrar dispositivos)
const char* html_page = 
"<!DOCTYPE html>"
"<html>"
"<head>"
"  <meta charset='UTF-8'>"
"  <meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"  <title>LocalDrop</title>"
"  <style>"
"    body { font-family: Arial; max-width: 800px; margin: 50px auto; padding: 20px; background: #f5f5f5; }"
"    h1 { color: #333; text-align: center; }"
"    .section { background: white; padding: 20px; margin: 20px 0; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }"
"    .device { padding: 15px; margin: 10px 0; background: #f9f9f9; border-radius: 5px; border-left: 4px solid #007bff; }"
"    .device h3 { margin: 0 0 5px 0; }"
"    .device p { margin: 5px 0; color: #666; }"
"    .upload-area { border: 2px dashed #ccc; padding: 40px; text-align: center; border-radius: 10px; }"
"    input[type='file'] { margin: 20px 0; }"
"    button { background: #007bff; color: white; padding: 10px 30px; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; }"
"    button:hover { background: #0056b3; }"
"    .send-btn { background: #28a745; padding: 5px 15px; font-size: 14px; }"
"    .send-btn:hover { background: #218838; }"
"    #status { margin-top: 20px; padding: 10px; border-radius: 5px; }"
"    .success { background: #d4edda; color: #155724; }"
"    .error { background: #f8d7da; color: #721c24; }"
"    .online { color: #28a745; }"
"    .offline { color: #dc3545; }"
"    .progress { background: #e9ecef; border-radius: 5px; height: 30px; margin: 10px 0; overflow: hidden; }"
"    .progress-bar { background: #007bff; height: 100%; transition: width 0.3s; text-align: center; color: white; line-height: 30px; }"
"    .hidden { display: none; }"
"  </style>"
"</head>"
"<body>"
"  <h1>📡 LocalDrop</h1>"
"  "
"  <div class='section'>"
"    <h2>🔍 Dispositivos Descobertos</h2>"
"    <div id='devices'>Buscando dispositivos...</div>"
"    <button onclick='refreshDevices()'>🔄 Atualizar</button>"
"  </div>"
"  "
"  <div class='section'>"
"    <div class='upload-area'>"
"      <h2>📤 Receber arquivos neste dispositivo</h2>"
"      <form id='uploadForm' enctype='multipart/form-data'>"
"        <input type='file' name='file' id='fileInput' multiple required><br>"
"        <button type='submit'>Enviar para este PC</button>"
"      </form>"
"      <div id='progress-container' class='hidden'>"
"        <div class='progress'>"
"          <div id='progress-bar' class='progress-bar'>0%</div>"
"        </div>"
"      </div>"
"      <div id='status'></div>"
"    </div>"
"  </div>"
"  "
"  <!-- Modal para enviar arquivo -->"
"  <input type='file' id='sendFileInput' style='display:none' />"
"  "
"  <script>"
"    let selectedDevice = null;"
"    "
"    async function refreshDevices() {"
"      const response = await fetch('/api/devices');"
"      const devices = await response.json();"
"      const container = document.getElementById('devices');"
"      "
"      if (devices.length === 0) {"
"        container.innerHTML = '<p>Nenhum dispositivo encontrado</p>';"
"        return;"
"      }"
"      "
"      container.innerHTML = devices.map(d => `"
"        <div class='device'>"
"          <h3>${d.name} <span class='${d.active ? \"online\" : \"offline\"}'>${d.active ? '🟢 Online' : '🔴 Offline'}</span></h3>"
"          <p>📍 ${d.ip}:${d.port}</p>"
"          <button class='send-btn' onclick='selectDeviceToSend(\"${d.name}\", \"${d.ip}\", ${d.port})' ${!d.active ? 'disabled' : ''}>📤 Enviar arquivo</button>"
"        </div>"
"      `).join('');"
"    }"
"    "
"    function selectDeviceToSend(name, ip, port) {"
"      selectedDevice = {name, ip, port};"
"      document.getElementById('sendFileInput').click();"
"    }"
"    "
"    document.getElementById('sendFileInput').onchange = async function(e) {"
"      const file = e.target.files[0];"
"      if (!file || !selectedDevice) return;"
"      "
"      const formData = new FormData();"
"      formData.append('file', file);"
"      formData.append('target_ip', selectedDevice.ip);"
"      formData.append('target_port', selectedDevice.port);"
"      "
"      try {"
"        const response = await fetch('/api/send', {"
"          method: 'POST',"
"          body: formData"
"        });"
"        "
"        if (response.ok) {"
"          alert('✅ Arquivo enviado para ' + selectedDevice.name);"
"        } else {"
"          alert('❌ Erro ao enviar arquivo');"
"        }"
"      } catch (error) {"
"        alert('❌ Erro: ' + error.message);"
"      }"
"      "
"      e.target.value = '';"
"      selectedDevice = null;"
"    };"
"    "
"    document.getElementById('uploadForm').onsubmit = async (e) => {"
"      e.preventDefault();"
"      const formData = new FormData();"
"      const files = document.getElementById('fileInput').files;"
"      const progressContainer = document.getElementById('progress-container');"
"      const progressBar = document.getElementById('progress-bar');"
"      const status = document.getElementById('status');"
"      "
"      for (let file of files) formData.append('file', file);"
"      "
"      progressContainer.classList.remove('hidden');"
"      status.textContent = '';"
"      "
"      try {"
"        const xhr = new XMLHttpRequest();"
"        "
"        xhr.upload.onprogress = (e) => {"
"          if (e.lengthComputable) {"
"            const percent = Math.round((e.loaded / e.total) * 100);"
"            progressBar.style.width = percent + '%';"
"            progressBar.textContent = percent + '%';"
"          }"
"        };"
"        "
"        xhr.onload = () => {"
"          if (xhr.status === 200) {"
"            status.className = 'success';"
"            status.textContent = '✅ Arquivo recebido com sucesso!';"
"            progressBar.style.width = '100%';"
"            progressBar.textContent = '100%';"
"          } else {"
"            status.className = 'error';"
"            status.textContent = '❌ Erro ao receber arquivo';"
"          }"
"          setTimeout(() => progressContainer.classList.add('hidden'), 2000);"
"        };"
"        "
"        xhr.onerror = () => {"
"          status.className = 'error';"
"          status.textContent = '❌ Erro na conexão';"
"        };"
"        "
"        xhr.open('POST', '/upload');"
"        xhr.send(formData);"
"      } catch (error) {"
"        status.className = 'error';"
"        status.textContent = '❌ Erro: ' + error.message;"
"      }"
"    };"
"    "
"    refreshDevices();"
"    setInterval(refreshDevices, 3000);"
"  </script>"
"</body>"
"</html>";
// Função para obter o IP local
char* get_local_ip() {
    struct ifaddrs *ifaddr, *ifa;
    static char host[NI_MAXHOST];
    
    if (getifaddrs(&ifaddr) == -1) {
        return NULL;
    }
    
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        
        int family = ifa->ifa_addr->sa_family;
        
        if (family == AF_INET) {
            int s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                              host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
            if (s == 0 && strcmp(host, "127.0.0.1") != 0) {
                freeifaddrs(ifaddr);
                return host;
            }
        }
    }
    
    freeifaddrs(ifaddr);
    return NULL;
}

// Função para imprimir QR code
void print_qrcode(const char *text) {
    QRcode *qrcode = QRcode_encodeString(text, 0, QR_ECLEVEL_L, QR_MODE_8, 1);
    
    if (!qrcode) {
        printf("❌ Erro ao gerar QR code\n");
        return;
    }
    
    int size = qrcode->width;
    unsigned char *data = qrcode->data;
    
    printf("\n");
    
    for (int i = 0; i < size + 2; i++) printf("██");
    printf("\n");
    
    for (int y = 0; y < size; y++) {
        printf("██");
        for (int x = 0; x < size; x++) {
            if (data[y * size + x] & 1) {
                printf("  ");
            } else {
                printf("██");
            }
        }
        printf("██\n");
    }
    
    for (int i = 0; i < size + 2; i++) printf("██");
    printf("\n\n");
    
    QRcode_free(qrcode);
}

// Callback HTTP
static enum MHD_Result answer_to_connection(void *cls, struct MHD_Connection *connection,
                          const char *url, const char *method,
                          const char *version, const char *upload_data,
                          size_t *upload_data_size, void **con_cls) {
    
    struct MHD_Response *response;
    enum MHD_Result ret;
    
    // Servir página principal
    if (strcmp(url, "/") == 0 && strcmp(method, "GET") == 0) {
        response = MHD_create_response_from_buffer(strlen(html_page),
                                                   (void*)html_page,
                                                   MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Content-Type", "text/html");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }
    
    // API: listar dispositivos descobertos
    if (strcmp(url, "/api/devices") == 0 && strcmp(method, "GET") == 0) {
        char json[4096] = "[";
        
        for (int i = 0; i < device_count; i++) {
            char device_json[512];
            snprintf(device_json, sizeof(device_json),
                    "%s{\"name\":\"%s\",\"ip\":\"%s\",\"port\":%d,\"active\":%s}",
                    i > 0 ? "," : "",
                    discovered_devices[i].name,
                    discovered_devices[i].ip,
                    discovered_devices[i].port,
                    discovered_devices[i].active ? "true" : "false");
            strcat(json, device_json);
        }
        strcat(json, "]");
        
        response = MHD_create_response_from_buffer(strlen(json),
                                                   (void*)json,
                                                   MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "application/json");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }
    
    // Upload de arquivo (simplificado)
    if (strcmp(url, "/upload") == 0 && strcmp(method, "POST") == 0) {
        const char *response_text = "OK";
        response = MHD_create_response_from_buffer(strlen(response_text),
                                                   (void*)response_text,
                                                   MHD_RESPMEM_PERSISTENT);
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        
        printf("📥 Arquivo recebido!\n");
        return ret;
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

int main() {
    struct MHD_Daemon *daemon;
    char *ip = get_local_ip();
    char url[256];
    char hostname[256];
    
    signal(SIGINT, sigint_handler);
    
    if (!ip) {
        printf("❌ Não foi possível obter o IP local\n");
        return 1;
    }
    
    gethostname(hostname, sizeof(hostname));
    snprintf(url, sizeof(url), "http://%s:%d", ip, PORT);
    
    printf("🚀 LocalDrop iniciado!\n\n");
    printf("📱 Escaneie o QR code:\n");
    print_qrcode(url);
    printf("💡 Ou acesse: %s\n\n", url);
    
    // Inicializar descoberta mDNS
    if (discovery_init(hostname, PORT) < 0) {
        printf("❌ Erro ao inicializar descoberta\n");
        return 1;
    }
    
    // Iniciar servidor HTTP
    daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, PORT, NULL, NULL,
                             &answer_to_connection, NULL, MHD_OPTION_END);
    
    if (daemon == NULL) {
        printf("❌ Erro ao iniciar servidor\n");
        discovery_cleanup();
        return 1;
    }
    
    printf("⏳ Aguardando conexões... (Ctrl+C para sair)\n\n");
    
    // Loop principal - processar descoberta
    while (keep_running) {
        discovery_poll();
        usleep(100000);  // 100ms
    }
    
    printf("\n🛑 Encerrando...\n");
    
    MHD_stop_daemon(daemon);
    discovery_cleanup();
    
    return 0;
}