#ifndef DISCOVERY_H
#define DISCOVERY_H

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-client/lookup.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/error.h>
#include <stdint.h>

#define SERVICE_TYPE "_localdrop._tcp"

// Funções públicas
int discovery_init(const char *device_name, uint16_t port);
void discovery_cleanup();
void discovery_poll();  // Processar eventos de descoberta

#endif
