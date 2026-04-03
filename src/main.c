#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <qrencode.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "auth.h"
#include "discovery.h"
#include "peer_registry.h"
#include "server.h"
#include "storage.h"
#include "transfer.h"
#include "transfer_queue.h"

#define PORT 8080

static volatile sig_atomic_t keep_running = 1;

static void sigint_handler(int sig) {
    (void)sig;
    keep_running = 0;
}

static char *get_local_ip(void) {
    struct ifaddrs *ifaddr = NULL;
    struct ifaddrs *ifa = NULL;
    static char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) {
        snprintf(host, sizeof(host), "127.0.0.1");
        return host;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        int status;

        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        status = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                             host, sizeof(host), NULL, 0, NI_NUMERICHOST);
        if (status == 0 && strcmp(host, "127.0.0.1") != 0) {
            freeifaddrs(ifaddr);
            return host;
        }
    }

    freeifaddrs(ifaddr);
    snprintf(host, sizeof(host), "127.0.0.1");
    return host;
}

static void print_qrcode(const char *text) {
    QRcode *qrcode;
    unsigned char *data;
    int size;

    qrcode = QRcode_encodeString(text, 0, QR_ECLEVEL_L, QR_MODE_8, 1);
    if (!qrcode) {
        printf("❌ Erro ao gerar QR code\n");
        return;
    }

    size = qrcode->width;
    data = qrcode->data;

    printf("\n");
    for (int i = 0; i < size + 2; i++) {
        printf("██");
    }
    printf("\n");

    for (int y = 0; y < size; y++) {
        printf("██");
        for (int x = 0; x < size; x++) {
            printf((data[y * size + x] & 1) ? "  " : "██");
        }
        printf("██\n");
    }

    for (int i = 0; i < size + 2; i++) {
        printf("██");
    }
    printf("\n\n");

    QRcode_free(qrcode);
}

int main(void) {
    struct MHD_Daemon *daemon = NULL;
    char *ip = get_local_ip();
    char url[1200];
    char hostname[256];
    char admin_code[16];
    int discovery_enabled = 0;

    signal(SIGINT, sigint_handler);

    if (peer_registry_init() != 0) {
        printf("❌ Erro ao inicializar o registro de peers\n");
        return 1;
    }

    if (transfer_global_init() != 0) {
        printf("❌ Erro ao inicializar libcurl\n");
        peer_registry_cleanup();
        return 1;
    }

    if (auth_init() != 0) {
        printf("❌ Erro ao inicializar autenticacao local\n");
        transfer_global_cleanup();
        peer_registry_cleanup();
        return 1;
    }

    if (storage_init() != 0) {
        printf("❌ Erro ao inicializar armazenamento local\n");
        auth_cleanup();
        transfer_global_cleanup();
        peer_registry_cleanup();
        return 1;
    }

    if (transfer_queue_init() != 0) {
        printf("❌ Erro ao inicializar fila de transferencias\n");
        storage_cleanup();
        auth_cleanup();
        transfer_global_cleanup();
        peer_registry_cleanup();
        return 1;
    }

    if (!ip) {
        printf("❌ Não foi possível obter o IP local\n");
        transfer_queue_shutdown();
        storage_cleanup();
        auth_cleanup();
        transfer_global_cleanup();
        peer_registry_cleanup();
        return 1;
    }

    gethostname(hostname, sizeof(hostname));
    hostname[sizeof(hostname) - 1] = '\0';
    snprintf(url, sizeof(url), "http://%s:%d", ip, PORT);

    printf("🚀 LocalDrop iniciado!\n\n");
    printf("📱 Escaneie o QR code:\n");
    print_qrcode(url);
    printf("💡 Ou acesse: %s\n\n", url);
    if (auth_get_admin_code(admin_code, sizeof(admin_code)) == 0) {
        printf("🔐 Codigo administrativo local: %s\n\n", admin_code);
    }

    if (discovery_init(hostname, PORT) < 0) {
        printf("⚠️  Descoberta mDNS indisponivel; iniciando apenas o servidor web\n");
    } else {
        discovery_enabled = 1;
    }

    daemon = server_start(PORT);
    if (!daemon) {
        printf("❌ Erro ao iniciar servidor\n");
        if (discovery_enabled) {
            discovery_cleanup();
        }
        transfer_queue_shutdown();
        storage_cleanup();
        auth_cleanup();
        transfer_global_cleanup();
        peer_registry_cleanup();
        return 1;
    }

    printf("⏳ Aguardando conexões... (Ctrl+C para sair)\n\n");

    while (keep_running) {
        if (discovery_enabled) {
            discovery_poll();
        }
        usleep(100000);
    }

    printf("\n🛑 Encerrando...\n");

    server_stop(daemon);
    if (discovery_enabled) {
        discovery_cleanup();
    }
    transfer_queue_shutdown();
    storage_cleanup();
    auth_cleanup();
    transfer_global_cleanup();
    peer_registry_cleanup();
    return 0;
}
