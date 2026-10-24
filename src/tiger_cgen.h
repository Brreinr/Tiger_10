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

/* Genera código C autónomo portable a partir del AST de Tiger */
char* tiger_ast_to_c(TigerStmt** stmts, size_t count, TigerTargetOS target, const char* app_name, const char* domain);

/* Compila un archivo Tiger directamente a un ejecutable nativo o paquete de SO con dominios */
bool tiger_compile_to_binary(const char* input_path, const char* output_path, TigerTargetOS target, const char* domain, bool run_after);

#endif /* TIGER_CGEN_H */


