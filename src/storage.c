#include "storage.h"

#include "crypto.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define DOWNLOADS_DIR "downloads"
#define STAGING_DIR "staging"

static void copy_string(char *dest, size_t dest_size, const char *src) {
    if (!dest || dest_size == 0) {
        return;
    }

    snprintf(dest, dest_size, "%s", src ? src : "");
}

static void set_error(char *error_message, size_t error_size, const char *message) {
    if (!error_message || error_size == 0) {
        return;
    }

    snprintf(error_message, error_size, "%s", message ? message : "Erro interno");
}

static int ensure_directory(const char *path) {
    struct stat st = {0};

    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }

    if (mkdir(path, 0700) != 0) {
        return -1;
    }

    return 0;
}

static void sanitize_filename(const char *filename, char *dest, size_t dest_size) {
    const char *base;
    size_t write_index = 0;

    if (!dest || dest_size == 0) {
        return;
    }

    dest[0] = '\0';
    if (!filename || filename[0] == '\0') {
        copy_string(dest, dest_size, "upload.bin");
        return;
    }

    base = strrchr(filename, '/');
    if (!base) {
        base = strrchr(filename, '\\');
    }
    base = base ? base + 1 : filename;

    for (size_t index = 0; base[index] != '\0' && write_index + 1 < dest_size; index++) {
        char value = base[index];

        if ((value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') ||
            value == '.' || value == '_' || value == '-') {
            dest[write_index++] = value;
        } else {
            dest[write_index++] = '_';
        }
    }

    dest[write_index] = '\0';
    if (dest[0] == '\0') {
        copy_string(dest, dest_size, "upload.bin");
    }
}

int storage_init(void) {
    if (ensure_directory(DOWNLOADS_DIR) != 0) {
        return -1;
    }

    if (ensure_directory(STAGING_DIR) != 0) {
        return -1;
    }

    return 0;
}

void storage_cleanup(void) {
}

int upload_stage_open(UploadStage *stage, const char *filename, char *error_message, size_t error_size) {
    char random_token[17];
    char safe_filename[256];

    if (!stage) {
        set_error(error_message, error_size, "Contexto de upload invalido");
        return -1;
    }

    memset(stage, 0, sizeof(*stage));
    sanitize_filename(filename, safe_filename, sizeof(safe_filename));
    copy_string(stage->original_name, sizeof(stage->original_name), safe_filename);

    if (crypto_random_token(random_token, sizeof(random_token), 8) != 0) {
        set_error(error_message, error_size, "Falha ao gerar identificador do arquivo");
        return -1;
    }

    snprintf(stage->staging_path,
             sizeof(stage->staging_path),
             "%s/%ld_%s_%s",
             STAGING_DIR,
             (long)time(NULL),
             random_token,
             safe_filename);

    stage->fp = fopen(stage->staging_path, "wb");
    if (!stage->fp) {
        set_error(error_message, error_size, "Nao foi possivel criar o arquivo temporario");
        stage->staging_path[0] = '\0';
        return -1;
    }

    return 0;
}

int upload_stage_write(UploadStage *stage,
                       const char *data,
                       size_t size,
                       size_t max_file_size,
                       char *error_message,
                       size_t error_size) {
    if (!stage || !stage->fp) {
        set_error(error_message, error_size, "Upload temporario nao foi inicializado");
        return -1;
    }

    if (stage->bytes_received + size > max_file_size) {
        set_error(error_message, error_size, "Arquivo excede o limite permitido");
        return -1;
    }

    if (fwrite(data, 1, size, stage->fp) != size) {
        set_error(error_message, error_size, "Falha ao gravar o arquivo temporario");
        return -1;
    }

    stage->bytes_received += size;
    return 0;
}

int upload_stage_seal(UploadStage *stage, char *error_message, size_t error_size) {
    if (!stage) {
        set_error(error_message, error_size, "Upload temporario invalido");
        return -1;
    }

    if (stage->sealed) {
        return 0;
    }

    if (!stage->fp) {
        set_error(error_message, error_size, "Arquivo temporario ausente");
        return -1;
    }

    if (fflush(stage->fp) != 0) {
        set_error(error_message, error_size, "Falha ao persistir o arquivo temporario");
        return -1;
    }

    if (fclose(stage->fp) != 0) {
        stage->fp = NULL;
        set_error(error_message, error_size, "Falha ao fechar o arquivo temporario");
        return -1;
    }

    stage->fp = NULL;
    stage->sealed = 1;
    return 0;
}

int upload_stage_commit(UploadStage *stage, char *final_path, size_t final_path_size, char *error_message, size_t error_size) {
    char committed_path[512];

    if (!stage) {
        set_error(error_message, error_size, "Upload temporario invalido");
        return -1;
    }

    if (upload_stage_seal(stage, error_message, error_size) != 0) {
        return -1;
    }

    snprintf(committed_path,
             sizeof(committed_path),
             "%s/%ld_%s",
             DOWNLOADS_DIR,
             (long)time(NULL),
             stage->original_name[0] != '\0' ? stage->original_name : "upload.bin");

    if (rename(stage->staging_path, committed_path) != 0) {
        set_error(error_message, error_size, "Falha ao mover o arquivo para downloads");
        return -1;
    }

    if (final_path && final_path_size > 0) {
        copy_string(final_path, final_path_size, committed_path);
    }

    stage->staging_path[0] = '\0';
    return 0;
}

void upload_stage_abort(UploadStage *stage) {
    if (!stage) {
        return;
    }

    if (stage->fp) {
        fclose(stage->fp);
        stage->fp = NULL;
    }

    if (stage->staging_path[0] != '\0') {
        unlink(stage->staging_path);
    }

    memset(stage, 0, sizeof(*stage));
}

int storage_delete_file(const char *path) {
    if (!path || path[0] == '\0') {
        return -1;
    }

    return unlink(path);
}

void cleanup_expired_staging_files(time_t max_age_seconds) {
    DIR *directory;
    struct dirent *entry;
    time_t now = time(NULL);

    if (max_age_seconds <= 0) {
        return;
    }

    directory = opendir(STAGING_DIR);
    if (!directory) {
        return;
    }

    while ((entry = readdir(directory)) != NULL) {
        char path[512];
        struct stat st = {0};

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", STAGING_DIR, entry->d_name);
        if (stat(path, &st) != 0) {
            continue;
        }

        if (!S_ISREG(st.st_mode)) {
            continue;
        }

        if (st.st_mtime + max_age_seconds <= now) {
            unlink(path);
        }
    }

    closedir(directory);
}
