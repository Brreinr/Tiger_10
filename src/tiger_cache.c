#include "tiger_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define tiger_mkdir(p) _mkdir(p)
#else
#include <unistd.h>
#define tiger_mkdir(p) mkdir(p, 0755)
#endif

#define TIGER_CACHE_DIR ".tiger_cache"

static void ensure_cache_dir(void) {
    tiger_mkdir(TIGER_CACHE_DIR);
}

void tiger_cache_init(void) {
    ensure_cache_dir();
}

uint64_t tiger_cache_compute_hash(const char* source_code, size_t code_len, const char* options_str) {
    uint64_t h = 14695981039346656037ULL;
    if (source_code) {
        for (size_t i = 0; i < code_len; i++) {
            h ^= (unsigned char)source_code[i];
            h *= 1099511628211ULL;
        }
    }
    if (options_str) {
        while (*options_str) {
            h ^= (unsigned char)(*options_str++);
            h *= 1099511628211ULL;
        }
    }
    return h;
}

static uint64_t get_file_mtime(const char* path) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &data)) {
        ULARGE_INTEGER u;
        u.LowPart = data.ftLastWriteTime.dwLowDateTime;
        u.HighPart = data.ftLastWriteTime.dwHighDateTime;
        return (uint64_t)u.QuadPart;
    }
    return 0;
#else
    struct stat st;
    if (stat(path, &st) == 0) {
        return (uint64_t)st.st_mtime;
    }
    return 0;
#endif
}

bool tiger_cache_check_hit(const char* input_path, const char* output_path, uint64_t hash, TigerCacheEntry* out_entry) {
    if (!out_entry) return false;
    out_entry->full_hash = hash;
    out_entry->is_hit = false;

    snprintf(out_entry->meta_path, sizeof(out_entry->meta_path), "%s/%016llx.meta", TIGER_CACHE_DIR, (unsigned long long)hash);

    /* Si el binario de salida no existe en disco, no hay cache hit */
    if (GetFileAttributesA(output_path) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    FILE* f = fopen(out_entry->meta_path, "r");
    if (!f) return false;

    char line[512];
    uint64_t cached_src_mtime = 0;
    uint64_t cached_out_mtime = 0;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "src_mtime=", 10) == 0) {
            cached_src_mtime = (uint64_t)strtoull(line + 10, NULL, 10);
        } else if (strncmp(line, "out_mtime=", 10) == 0) {
            cached_out_mtime = (uint64_t)strtoull(line + 10, NULL, 10);
        }
    }
    fclose(f);

    uint64_t current_src_mtime = get_file_mtime(input_path);
    uint64_t current_out_mtime = get_file_mtime(output_path);

    /* Cache HIT: Si el archivo fuente no fue modificado desde el cacheo y el binario sigue intacto */
    if (current_src_mtime != 0 && current_src_mtime <= cached_src_mtime &&
        current_out_mtime != 0 && current_out_mtime >= cached_out_mtime) {
        out_entry->is_hit = true;
        return true;
    }

    return false;
}

void tiger_cache_store(const char* input_path, const char* output_path, uint64_t hash) {
    ensure_cache_dir();
    char meta_path[MAX_PATH];
    snprintf(meta_path, sizeof(meta_path), "%s/%016llx.meta", TIGER_CACHE_DIR, (unsigned long long)hash);

    FILE* f = fopen(meta_path, "w");
    if (!f) return;

    uint64_t src_mtime = get_file_mtime(input_path);
    uint64_t out_mtime = get_file_mtime(output_path);

    fprintf(f, "hash=%016llx\n", (unsigned long long)hash);
    fprintf(f, "src_path=%s\n", input_path ? input_path : "");
    fprintf(f, "src_mtime=%llu\n", (unsigned long long)src_mtime);
    fprintf(f, "out_path=%s\n", output_path ? output_path : "");
    fprintf(f, "out_mtime=%llu\n", (unsigned long long)out_mtime);
    fclose(f);
}

void tiger_cache_clean(void) {
    printf("[Tiger Cache]: Vaciando cache incremental en '%s'...\n", TIGER_CACHE_DIR);
#ifdef _WIN32
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "cmd /c \"if exist %s rmdir /s /q %s\"", TIGER_CACHE_DIR, TIGER_CACHE_DIR);
    system(cmd);
#else
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TIGER_CACHE_DIR);
    system(cmd);
#endif
    printf("[Tiger Cache]: Cache limpiado con exito.\n");
}

