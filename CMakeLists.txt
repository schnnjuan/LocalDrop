cmake_minimum_required(VERSION 3.10)
project(LocalDrop C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(LOCALDROP_ENABLE_HARDENING "Enable compile and link hardening" ON)
option(LOCALDROP_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(LOCALDROP_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(LOCALDROP_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

set(LOCALDROP_SANITIZER_COUNT 0)
if(LOCALDROP_ENABLE_ASAN)
    math(EXPR LOCALDROP_SANITIZER_COUNT "${LOCALDROP_SANITIZER_COUNT} + 1")
endif()
if(LOCALDROP_ENABLE_UBSAN)
    math(EXPR LOCALDROP_SANITIZER_COUNT "${LOCALDROP_SANITIZER_COUNT} + 1")
endif()
if(LOCALDROP_ENABLE_TSAN)
    math(EXPR LOCALDROP_SANITIZER_COUNT "${LOCALDROP_SANITIZER_COUNT} + 1")
endif()
if(LOCALDROP_SANITIZER_COUNT GREATER 1)
    message(FATAL_ERROR "Enable at most one sanitizer at a time")
endif()

find_package(PkgConfig REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(AVAHI REQUIRED avahi-client)
pkg_check_modules(MHD REQUIRED libmicrohttpd)
pkg_check_modules(QRENCODE REQUIRED libqrencode)

include_directories(${AVAHI_INCLUDE_DIRS} ${MHD_INCLUDE_DIRS} ${QRENCODE_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS})
include_directories(${CMAKE_SOURCE_DIR}/src)

set(LOCALDROP_CORE_SOURCES
    src/auth.c
    src/crypto.c
    src/json_utils.c
    src/peer_registry.c
    src/storage.c
    src/transfer.c
    src/transfer_queue.c
)

set(LOCALDROP_APP_SOURCES
    ${LOCALDROP_CORE_SOURCES}
    src/main.c
    src/discovery.c
    src/server.c
    src/ui.c
)

set(LOCALDROP_COMMON_LIBS
    ${AVAHI_LIBRARIES}
    ${MHD_LIBRARIES}
    ${QRENCODE_LIBRARIES}
    ${CURL_LIBRARIES}
    OpenSSL::Crypto
    Threads::Threads
)

function(localdrop_apply_target_options target_name)
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)

    if(LOCALDROP_ENABLE_HARDENING)
        target_compile_options(${target_name} PRIVATE -O2 -fstack-protector-strong -D_FORTIFY_SOURCE=2)
        if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.13")
            target_link_options(${target_name} PRIVATE -Wl,-z,relro -Wl,-z,now)
        endif()
    endif()

    if(LOCALDROP_ENABLE_ASAN)
        target_compile_options(${target_name} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(${target_name} PRIVATE -fsanitize=address)
    endif()

    if(LOCALDROP_ENABLE_UBSAN)
        target_compile_options(${target_name} PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
        target_link_options(${target_name} PRIVATE -fsanitize=undefined)
    endif()

    if(LOCALDROP_ENABLE_TSAN)
        target_compile_options(${target_name} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(${target_name} PRIVATE -fsanitize=thread)
    endif()
endfunction()

add_executable(localdrop ${LOCALDROP_APP_SOURCES})
target_link_libraries(localdrop ${LOCALDROP_COMMON_LIBS})
localdrop_apply_target_options(localdrop)

include(CTest)

if(BUILD_TESTING)
    add_executable(localdrop_unit_tests tests/test_core.c ${LOCALDROP_CORE_SOURCES})
    target_link_libraries(localdrop_unit_tests ${CURL_LIBRARIES} OpenSSL::Crypto Threads::Threads)
    localdrop_apply_target_options(localdrop_unit_tests)

    add_test(NAME localdrop_unit_tests COMMAND localdrop_unit_tests)
    set_tests_properties(localdrop_unit_tests PROPERTIES LABELS "unit")

    add_test(NAME localdrop_integration
             COMMAND bash ${CMAKE_SOURCE_DIR}/tests/integration.sh ${CMAKE_SOURCE_DIR} $<TARGET_FILE:localdrop>)
    set_tests_properties(localdrop_integration PROPERTIES LABELS "integration" TIMEOUT 60)
endif()
