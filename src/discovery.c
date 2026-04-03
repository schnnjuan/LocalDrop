#include "discovery.h"
#include "peer_registry.h"

#include <stdio.h>
#include <string.h>

static AvahiSimplePoll *simple_poll = NULL;
static AvahiClient *client = NULL;
static AvahiServiceBrowser *browser = NULL;
static AvahiEntryGroup *group = NULL;
static char local_device_name[256] = {0};

// Callback quando resolve os detalhes de um dispositivo
static void resolve_callback(
    AvahiServiceResolver *r,
    AvahiIfIndex interface,
    AvahiProtocol protocol,
    AvahiResolverEvent event,
    const char *name,
    const char *type,
    const char *domain,
    const char *host_name,
    const AvahiAddress *address,
    uint16_t port,
    AvahiStringList *txt,
    AvahiLookupResultFlags flags,
    void* userdata) {
    (void)interface;
    (void)protocol;
    (void)type;
    (void)domain;
    (void)host_name;
    (void)txt;
    (void)flags;
    (void)userdata;

    if (event == AVAHI_RESOLVER_FOUND) {
        char addr[AVAHI_ADDRESS_STR_MAX];
        if (address) {
            avahi_address_snprint(addr, sizeof(addr), address);
            if (peer_registry_add_or_update(name, addr, port) == 0) {
                printf("✅ Dispositivo descoberto: %s (%s:%u)\n", name, addr, (unsigned int)port);
            } else {
                printf("❌ Limite de dispositivos atingido ou registro indisponivel\n");
            }
        }
    }
    
    avahi_service_resolver_free(r);
}

// Callback quando encontra um serviço
static void browse_callback(
    AvahiServiceBrowser *b,
    AvahiIfIndex interface,
    AvahiProtocol protocol,
    AvahiBrowserEvent event,
    const char *name,
    const char *type,
    const char *domain,
    AvahiLookupResultFlags flags,
    void* userdata) {
    AvahiClient *avahi_client = userdata;
    (void)b;
    (void)flags;

    if ((event == AVAHI_BROWSER_NEW || event == AVAHI_BROWSER_REMOVE) &&
        name &&
        local_device_name[0] != '\0' &&
        strcmp(name, local_device_name) == 0) {
        return;
    }
    
    if (event == AVAHI_BROWSER_NEW) {
        if (!name) {
            return;
        }
        avahi_service_resolver_new(avahi_client, interface, protocol, name, type, domain,
                                   AVAHI_PROTO_UNSPEC, 0, resolve_callback, avahi_client);
    } else if (event == AVAHI_BROWSER_REMOVE) {
        if (!name) {
            return;
        }
        if (peer_registry_mark_inactive(name) == 0) {
            printf("❌ Dispositivo saiu: %s\n", name);
        }
    } else if (event == AVAHI_BROWSER_FAILURE) {
        printf("❌ Falha no browser Avahi\n");
    }
}

// Callback para criar o grupo de entrada (anúncio)
static void entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, void *userdata) {
    (void)userdata;
    group = g;
    
    if (state == AVAHI_ENTRY_GROUP_ESTABLISHED) {
        printf("✅ Serviço anunciado com sucesso!\n");
    } else if (state == AVAHI_ENTRY_GROUP_FAILURE) {
        printf("❌ Falha ao anunciar serviço\n");
    }
}

// Callback do cliente Avahi
static void client_callback(AvahiClient *c, AvahiClientState state, void *userdata) {
    (void)c;
    (void)userdata;
    if (state == AVAHI_CLIENT_FAILURE) {
        printf("❌ Falha no cliente Avahi\n");
    }
}

// Inicializar descoberta e anúncio
int discovery_init(const char *device_name, uint16_t port) {
    int error;

    snprintf(local_device_name, sizeof(local_device_name), "%s", device_name);
    
    // Criar poll
    simple_poll = avahi_simple_poll_new();
    if (!simple_poll) {
        printf("❌ Erro ao criar simple_poll\n");
        return -1;
    }
    
    // Criar cliente
    client = avahi_client_new(avahi_simple_poll_get(simple_poll), 0, 
                             client_callback, NULL, &error);
    if (!client) {
        printf("❌ Erro ao criar cliente: %s\n", avahi_strerror(error));
        return -1;
    }
    
    // Criar grupo para anunciar serviço
    group = avahi_entry_group_new(client, entry_group_callback, NULL);
    if (!group) {
        printf("❌ Erro ao criar entry group\n");
        return -1;
    }
    
    // Anunciar o serviço LocalDrop
    error = avahi_entry_group_add_service(
        group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 0,
        device_name, SERVICE_TYPE, NULL, NULL, port, NULL
    );
    
    if (error < 0) {
        printf("❌ Erro ao adicionar serviço: %s\n", avahi_strerror(error));
        return -1;
    }
    
    // Commit do grupo
    error = avahi_entry_group_commit(group);
    if (error < 0) {
        printf("❌ Erro ao fazer commit: %s\n", avahi_strerror(error));
        return -1;
    }
    
    // Criar browser para descobrir outros dispositivos
    browser = avahi_service_browser_new(
        client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
        SERVICE_TYPE, NULL, 0, browse_callback, client
    );
    
    if (!browser) {
        printf("❌ Erro ao criar browser\n");
        return -1;
    }
    
    printf("🔍 Descoberta iniciada - anunciando como '%s'\n", device_name);
    return 0;
}

// Processar eventos (chamar periodicamente)
void discovery_poll() {
    if (simple_poll) {
        avahi_simple_poll_iterate(simple_poll, 0);  // Non-blocking
    }
}

// Limpar recursos
void discovery_cleanup() {
    if (browser) avahi_service_browser_free(browser);
    if (group) avahi_entry_group_free(group);
    if (client) avahi_client_free(client);
    if (simple_poll) avahi_simple_poll_free(simple_poll);
}
