cmake_minimum_required(VERSION 3.10)
project(LocalDrop C)

set(CMAKE_C_STANDARD 11)

# Procurar as bibliotecas
find_package(PkgConfig REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(AVAHI REQUIRED avahi-client)
pkg_check_modules(MHD REQUIRED libmicrohttpd)
pkg_check_modules(QRENCODE REQUIRED libqrencode)

# Adicionar diretórios de include
include_directories(${AVAHI_INCLUDE_DIRS} ${MHD_INCLUDE_DIRS} ${QRENCODE_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS})
include_directories(${CMAKE_SOURCE_DIR}/src)

# Arquivos fonte
set(SOURCES
    src/auth.c
    src/crypto.c
    src/json_utils.c
    src/main.c
    src/peer_registry.c
    src/discovery.c
    src/transfer.c
    src/server.c
    src/ui.c
)

# Criar o executável
add_executable(localdrop ${SOURCES})

# Linkar as bibliotecas
target_link_libraries(localdrop ${AVAHI_LIBRARIES} ${MHD_LIBRARIES} ${QRENCODE_LIBRARIES} ${CURL_LIBRARIES} OpenSSL::Crypto Threads::Threads)
