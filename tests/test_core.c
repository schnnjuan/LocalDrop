#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "auth.h"
#include "json_utils.h"
#include "peer_registry.h"
#include "storage.h"

static void test_json_escape(void) {
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;

    assert(json_buffer_append_escaped(&buffer, &length, &capacity, "peer\"x\n") == 0);
    assert(strcmp(buffer, "peer\\\"x\\n") == 0);
    free(buffer);
}

static void test_peer_registry_snapshot(void) {
    char *json = NULL;

    assert(peer_registry_init() == 0);
    assert(peer_registry_add_or_update("peer\"1", "127.0.0.1", 8080) == 0);
    assert(peer_registry_set_paired("127.0.0.1", 8080, 1) == 0);
    assert(peer_registry_snapshot_json(&json) == 0);
    assert(strstr(json, "peer\\\"1") != NULL);
    assert(strstr(json, "\"paired\":true") != NULL);
    free(json);
    peer_registry_cleanup();
}

static void test_authentication_flow(void) {
    char admin_code[16];
    char ui_token[65];
    char pair_code[16];
    char pair_token[65];
    char stored_token[65];
    time_t expires_at = 0;

    assert(auth_init() == 0);
    assert(auth_get_admin_code(admin_code, sizeof(admin_code)) == 0);
    assert(auth_create_ui_session("127.0.0.1", "000000", ui_token, sizeof(ui_token)) != 0);
    assert(auth_create_ui_session("127.0.0.1", admin_code, ui_token, sizeof(ui_token)) == 0);
    assert(auth_validate_ui_session("127.0.0.1", ui_token) == 0);
    assert(auth_get_pairing_code(pair_code, sizeof(pair_code), &expires_at) == 0);
    assert(expires_at > time(NULL));
    assert(auth_issue_pair_token("127.0.0.1", "peer-a", 8080, pair_code, pair_token, sizeof(pair_token), &expires_at) == 0);
    assert(auth_validate_peer_token("127.0.0.1", pair_token) == 0);
    assert(auth_store_peer_token("192.168.0.10", 9090, "peer-b", "manual-token", time(NULL) + 60) == 0);
    assert(auth_get_peer_token("192.168.0.10", 9090, stored_token, sizeof(stored_token)) == 0);
    assert(strcmp(stored_token, "manual-token") == 0);
    auth_cleanup();
}

static void test_storage_stage_commit(void) {
    UploadStage stage;
    char final_path[512];
    char error_message[128];
    FILE *saved_file;
    char content[16] = {0};
    struct stat st = {0};

    assert(storage_init() == 0);
    memset(&stage, 0, sizeof(stage));
    memset(error_message, 0, sizeof(error_message));

    assert(upload_stage_open(&stage, "fixture.txt", error_message, sizeof(error_message)) == 0);
    assert(upload_stage_write(&stage, "hello", 5, 64, error_message, sizeof(error_message)) == 0);
    assert(upload_stage_commit(&stage, final_path, sizeof(final_path), error_message, sizeof(error_message)) == 0);
    assert(stat(final_path, &st) == 0);

    saved_file = fopen(final_path, "rb");
    assert(saved_file != NULL);
    assert(fread(content, 1, 5, saved_file) == 5);
    fclose(saved_file);
    assert(strcmp(content, "hello") == 0);

    assert(storage_delete_file(final_path) == 0);
    storage_cleanup();
}

int main(void) {
    test_json_escape();
    test_peer_registry_snapshot();
    test_authentication_flow();
    test_storage_stage_commit();
    printf("localdrop_unit_tests: ok\n");
    return 0;
}
