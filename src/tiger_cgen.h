#ifndef TIGER_CGEN_H
#define TIGER_CGEN_H

#include "tiger.h"
#include "tiger_ast.h"

typedef enum {
    TARGET_WINDOWS = 0,
    TARGET_MAC,
    TARGET_LINUX,
    TARGET_ANDROID,
    TARGET_IOS,
    TARGET_WASM
} TigerTargetOS;

const char* tiger_target_name(TigerTargetOS target);
TigerTargetOS tiger_parse_target(const char* name);

/* Opciones avanzadas de compilacion al silicio */
typedef struct {
    TigerTargetOS target;
    const char* domain;
    int opt_level;        /* 0 (-O0), 1 (-O1), 2 (-O2), 3 (-O3) */
    bool is_dev;          /* Modo desarrollo: compilacion rapida, simbolos de depuracion, sin LTO */
    bool is_release;      /* Modo produccion: -O3, strip de simbolos, optimizaciones agresivas */
    bool enable_lto;      /* Link-Time Optimization (-flto) */
    bool strip_symbols;   /* Elimina simbolos para distribucion (-Wl,-s) */
    bool use_cache;       /* Compilacion incremental por cache (.tiger_cache/) */
    bool force_rebuild;   /* Fuerza recompilacion ignorando cache */
    bool run_after;       /* Ejecuta inmediatamente tras compilar */
} TigerCompileOptions;

void tiger_compile_options_default(TigerCompileOptions* opts);

/* Genera código C autónomo portable a partir del AST de Tiger */
char* tiger_ast_to_c(TigerStmt** stmts, size_t count, TigerTargetOS target, const char* app_name, const char* domain);

/* Compila un archivo Tiger directamente a un ejecutable nativo con opciones configurables */
bool tiger_compile_with_options(const char* input_path, const char* output_path, const TigerCompileOptions* opts);

/* Compila un archivo Tiger directamente a un ejecutable nativo o paquete de SO con dominios (compatibilidad retroactiva) */
bool tiger_compile_to_binary(const char* input_path, const char* output_path, TigerTargetOS target, const char* domain, bool run_after);

#endif /* TIGER_CGEN_H */


