#include "tiger.h"

#ifdef _WIN32
#include <windows.h>
#ifndef CP_UTF8
#define CP_UTF8 65001
#endif
int WINAPI WideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCSTR lpDefaultChar, LPBOOL lpUsedDefaultChar);
#endif

char* tiger_read_file_resilient(const char* filepath, size_t* out_size) {
    if (!filepath) return NULL;

    FILE* f = fopen(filepath, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        char* empty = (char*)calloc(1, 1);
        if (out_size) *out_size = 0;
        return empty;
    }

    unsigned char* raw = (unsigned char*)malloc(size + 4);
    if (!raw) {
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(raw, 1, size, f);
    fclose(f);
    raw[read_bytes] = 0;
    raw[read_bytes + 1] = 0;
    raw[read_bytes + 2] = 0;

    /* 1. UTF-8 con BOM (0xEF, 0xBB, 0xBF) */
    if (read_bytes >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
        size_t len = read_bytes - 3;
        char* res = (char*)malloc(len + 1);
        memcpy(res, raw + 3, len);
        res[len] = '\0';
        free(raw);
        if (out_size) *out_size = len;
        return res;
    }

#ifdef _WIN32
    /* 2. UTF-16 Big Endian con BOM (0xFE, 0xFF) */
    if (read_bytes >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
        size_t wide_count = (read_bytes - 2) / 2;
        wchar_t* wide_buf = (wchar_t*)malloc((wide_count + 1) * sizeof(wchar_t));
        for (size_t i = 0; i < wide_count; i++) {
            unsigned char b1 = raw[2 + i * 2];
            unsigned char b2 = raw[2 + i * 2 + 1];
            wide_buf[i] = (wchar_t)((b1 << 8) | b2);
        }
        wide_buf[wide_count] = 0;

        int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide_buf, (int)wide_count, NULL, 0, NULL, NULL);
        char* res = (char*)malloc(utf8_len + 1);
        WideCharToMultiByte(CP_UTF8, 0, wide_buf, (int)wide_count, res, utf8_len, NULL, NULL);
        res[utf8_len] = '\0';

        free(wide_buf);
        free(raw);
        if (out_size) *out_size = (size_t)utf8_len;
        return res;
    }

    /* 3. UTF-16 Little Endian con BOM (0xFF, 0xFE) */
    if (read_bytes >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        size_t wide_count = (read_bytes - 2) / 2;
        wchar_t* wide_buf = (wchar_t*)(raw + 2);

        int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide_buf, (int)wide_count, NULL, 0, NULL, NULL);
        char* res = (char*)malloc(utf8_len + 1);
        WideCharToMultiByte(CP_UTF8, 0, wide_buf, (int)wide_count, res, utf8_len, NULL, NULL);
        res[utf8_len] = '\0';

        free(raw);
        if (out_size) *out_size = (size_t)utf8_len;
        return res;
    }
#endif

    /* 4. Si es UTF-8 estándar directo */
    char* res = (char*)malloc(read_bytes + 1);
    memcpy(res, raw, read_bytes);
    res[read_bytes] = '\0';
    free(raw);
    if (out_size) *out_size = read_bytes;
    return res;
}

#pragma pack(push, 1)
typedef struct {
    uint64_t payload_len;
    uint64_t uncompressed_len;
    uint32_t flags;
    uint32_t reserved;
    uint64_t magic;
} TigerPackFooter;
#pragma pack(pop)

char* tiger_read_self_payload(const char* self_path, size_t* out_size) {
    if (!self_path) return NULL;
    FILE* f = fopen(self_path, "rb");
    if (!f) return NULL;

    if (fseek(f, -(long)sizeof(TigerPackFooter), SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }

    TigerPackFooter footer;
    if (fread(&footer, sizeof(TigerPackFooter), 1, f) != 1) {
        fclose(f);
        return NULL;
    }

    if (footer.magic != TIGER_PACK_MAGIC || footer.payload_len == 0 || footer.payload_len > 150 * 1024 * 1024) {
        fclose(f);
        return NULL;
    }

    long total_offset = (long)(sizeof(TigerPackFooter) + footer.payload_len);
    if (fseek(f, -total_offset, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }

    char* buffer = (char*)malloc(footer.payload_len + 1);
    if (!buffer) {
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(buffer, 1, footer.payload_len, f);
    fclose(f);
    buffer[read_bytes] = '\0';

    if (out_size) *out_size = read_bytes;
    return buffer;
}
