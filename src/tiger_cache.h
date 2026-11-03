#ifndef TIGER_CACHE_H
#define TIGER_CACHE_H

#include "tiger.h"

/* ========================================================================= */
/* TIGER 10.2: SISTEMA DE COMPILACION INCREMENTAL Y CACHE (.tiger_cache/)   */
/* Evita recompilar codigos que no han cambiado (< 0.05 ms en Cache HIT)     */
/* ========================================================================= */

typedef struct {
    uint64_t full_hash;
    char meta_path[MAX_PATH];
    bool is_hit;
} TigerCacheEntry;

/* Inicializa el directorio de cache (.tiger_cache/) */
void tiger_cache_init(void);

/* Calcula el hash de contenido y opciones de compilacion */
uint64_t tiger_cache_compute_hash(const char* source_code, size_t code_len, const char* options_str);

/* Verifica si existe cache valido para el archivo y binario destino */
bool tiger_cache_check_hit(const char* input_path, const char* output_path, uint64_t hash, TigerCacheEntry* out_entry);

/* Registra una compilacion exitosa en el cache incremental */
void tiger_cache_store(const char* input_path, const char* output_path, uint64_t hash);

/* Limpia completamente el cache incremental */
void tiger_cache_clean(void);

#endif /* TIGER_CACHE_H */

