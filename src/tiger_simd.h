#ifndef TIGER_SIMD_H
#define TIGER_SIMD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ========================================================================= */
/* MÓDULO SIMD AVX2 & REGISTROS DE SILICIO / INTERFAZ BCI                    */
/* ========================================================================= */

/* Inicializa el subsistema SIMD y detecta AVX2 en la CPU */
void tiger_simd_init(void);
bool tiger_simd_has_avx2(void);

/* Operaciones vectoriales AVX2 simultáneas de 4 en 4 elementos (256 bits) */
void tiger_simd_add_i64_batch(int64_t* dst, const int64_t* a, const int64_t* b, size_t count);
void tiger_simd_mul_i64_batch(int64_t* dst, const int64_t* a, const int64_t* b, size_t count);
void tiger_simd_add_f64_batch(double* dst, const double* a, const double* b, size_t count);
void tiger_simd_mul_f64_batch(double* dst, const double* a, const double* b, size_t count);

/* Búsqueda vectorial SIMD de caracteres / delimitadores (32 bytes por ciclo) */
const char* tiger_simd_find_char(const char* text, size_t len, char target);

/* ========================================================================= */
/* PUNTEROS FÍSICOS POINTC LIBRES DE ALIASING Y REGISTROS CPU PARA BCI       */
/* ========================================================================= */

/* Identificadores de Registros de CPU x86_64 */
typedef enum {
    TIGER_REG_RAX = 0,
    TIGER_REG_RBX,
    TIGER_REG_RCX,
    TIGER_REG_RDX,
    TIGER_REG_RSI,
    TIGER_REG_RDI,
    TIGER_REG_R8,
    TIGER_REG_R9,
    TIGER_REG_R10,
    TIGER_REG_R11,
    TIGER_REG_R12,
    TIGER_REG_R13,
    TIGER_REG_R14,
    TIGER_REG_R15,
    TIGER_REG_COUNT
} TigerCPURegister;

/* Asignación y lectura directa de registros de CPU para control neural BCI */
uint64_t tiger_bci_read_register(TigerCPURegister reg);
void tiger_bci_write_register(TigerCPURegister reg, uint64_t value);

/* Operaciones de puntero físico libres de aliasing (pointc restrict) */
void tiger_pointc_write_u64(void* restrict dest, uint64_t val);
uint64_t tiger_pointc_read_u64(const void* restrict src);
void tiger_pointc_stream_bci(void* restrict dest, const void* restrict src, size_t bytes);

#endif /* TIGER_SIMD_H */

