#ifndef DISCOVERY_H
#define DISCOVERY_H

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-client/lookup.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/error.h>
#include <stdint.h>

#define SERVICE_TYPE "_localdrop._tcp"

// Estrutura para representar um dispositivo descoberto
typedef struct {
    char name[256];
    char ip[46];  // IPv4 ou IPv6
    uint16_t port;
    int active;   // 1 se ativo, 0 se offline
} Device;

// Lista de dispositivos descobertos (máximo 50)
extern Device discovered_devices[50];
extern int device_count;

// Funções públicas
int discovery_init(const char *device_name, uint16_t port);
void discovery_cleanup();
void discovery_poll();  // Processar eventos de descoberta

#endif