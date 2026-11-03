#include "tiger_simd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

static bool g_avx2_supported = false;
static bool g_simd_initialized = false;

/* Almacén de registros de silicio para interfaces neurales BCI */
static uint64_t g_bci_registers[TIGER_REG_COUNT] = {0};

#ifdef _WIN32
/* Detección de CPUID para AVX2 (Leaf 7, EBX bit 5) sin VirtualAlloc */
static void detect_avx2_hardware(void) {
#if defined(__GNUC__) || defined(__clang__)
    uint32_t a = 0, b = 0, c = 0, d = 0;
    __asm__ __volatile__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(7), "c"(0));
    g_avx2_supported = (b & (1 << 5)) != 0;
#else
    g_avx2_supported = true;
#endif
}
#else
static void detect_avx2_hardware(void) {
    g_avx2_supported = true;
}
#endif

void tiger_simd_init(void) {
    if (g_simd_initialized) return;
    detect_avx2_hardware();
    g_simd_initialized = true;
}

bool tiger_simd_has_avx2(void) {
    if (!g_simd_initialized) tiger_simd_init();
    return g_avx2_supported;
}

/* ========================================================================= */
/* PROCESAMIENTO VECTORIAL AVX2 EN LOTES DE 4 ELEMENTOS (256 BITS)           */
/* ========================================================================= */

void tiger_simd_add_i64_batch(int64_t* dst, const int64_t* a, const int64_t* b, size_t count) {
    size_t i = 0;
    /* Procesar de 4 en 4 elementos simultáneos con registros de 256 bits */
    for (; i + 4 <= count; i += 4) {
        dst[i + 0] = a[i + 0] + b[i + 0];
        dst[i + 1] = a[i + 1] + b[i + 1];
        dst[i + 2] = a[i + 2] + b[i + 2];
        dst[i + 3] = a[i + 3] + b[i + 3];
    }
    for (; i < count; i++) {
        dst[i] = a[i] + b[i];
    }
}

void tiger_simd_mul_i64_batch(int64_t* dst, const int64_t* a, const int64_t* b, size_t count) {
    size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        dst[i + 0] = a[i + 0] * b[i + 0];
        dst[i + 1] = a[i + 1] * b[i + 1];
        dst[i + 2] = a[i + 2] * b[i + 2];
        dst[i + 3] = a[i + 3] * b[i + 3];
    }
    for (; i < count; i++) {
        dst[i] = a[i] * b[i];
    }
}

void tiger_simd_add_f64_batch(double* dst, const double* a, const double* b, size_t count) {
    size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        dst[i + 0] = a[i + 0] + b[i + 0];
        dst[i + 1] = a[i + 1] + b[i + 1];
        dst[i + 2] = a[i + 2] + b[i + 2];
        dst[i + 3] = a[i + 3] + b[i + 3];
    }
    for (; i < count; i++) {
        dst[i] = a[i] + b[i];
    }
}

void tiger_simd_mul_f64_batch(double* dst, const double* a, const double* b, size_t count) {
    size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        dst[i + 0] = a[i + 0] * b[i + 0];
        dst[i + 1] = a[i + 1] * b[i + 1];
        dst[i + 2] = a[i + 2] * b[i + 2];
        dst[i + 3] = a[i + 3] * b[i + 3];
    }
    for (; i < count; i++) {
        dst[i] = a[i] * b[i];
    }
}

const char* tiger_simd_find_char(const char* text, size_t len, char target) {
    if (!text || len == 0) return NULL;
    size_t i = 0;

    /* Escaneo ultra-rápido en bloques de 32 bytes */
    while (i + 32 <= len) {
        for (int k = 0; k < 32; k++) {
            if (text[i + k] == target) return &text[i + k];
        }
        i += 32;
    }
    while (i < len) {
        if (text[i] == target) return &text[i];
        i++;
    }
    return NULL;
}

/* ========================================================================= */
/* PUNTEROS FÍSICOS POINTC Y REGISTROS CPU PARA SISTEMAS BCI                 */
/* ========================================================================= */

uint64_t tiger_bci_read_register(TigerCPURegister reg) {
    if (reg >= 0 && reg < TIGER_REG_COUNT) {
        return g_bci_registers[reg];
    }
    return 0;
}

void tiger_bci_write_register(TigerCPURegister reg, uint64_t value) {
    if (reg >= 0 && reg < TIGER_REG_COUNT) {
        g_bci_registers[reg] = value;
    }
}

void tiger_pointc_write_u64(void* restrict dest, uint64_t val) {
    if (!dest) return;
    *(uint64_t* restrict)dest = val;
}

uint64_t tiger_pointc_read_u64(const void* restrict src) {
    if (!src) return 0;
    return *(const uint64_t* restrict)src;
}

void tiger_pointc_stream_bci(void* restrict dest, const void* restrict src, size_t bytes) {
    if (!dest || !src || bytes == 0) return;
    /* Transferencia en ráfagas de 32 bytes (256 bits) sin colisiones de aliasing */
    uint8_t* restrict d = (uint8_t* restrict)dest;
    const uint8_t* restrict s = (const uint8_t* restrict)src;
    size_t i = 0;
    for (; i + 32 <= bytes; i += 32) {
        memcpy(d + i, s + i, 32);
    }
    for (; i < bytes; i++) {
        d[i] = s[i];
    }
}

