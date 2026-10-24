#ifndef TIGER_C_INTEROP_H
#define TIGER_C_INTEROP_H

#include "tiger.h"
#include "tiger_ast.h"

/* ========================================================================= */
/* CAJAS NEGRAS OPACAS Y DEMONIOS EN MEMORIA COMPARTIDA                      */
/* Bloques políglotas enviados a demonios persistentes en memoria            */
/* compartida sin tocar el disco duro.                                       */
/* ========================================================================= */

typedef struct {
    char* lang;       /* "C", "CPP", "PY", "JS", "HTML", "JSON", "NOSQL", "BI" */
    char* code;       /* Código fuente en memoria */
    char* target_var; /* Variable destino opcional */
} TigerPolyglotBox;

/* Estructura expuesta a los bloques de C para acceso directo y bidireccional */
typedef struct {
    int64_t     (*get_int)(const char* name);
    void        (*set_int)(const char* name, int64_t val);
    double      (*get_float)(const char* name);
    void        (*set_float)(const char* name, double val);
    const char* (*get_str)(const char* name);
    void        (*set_str)(const char* name, const char* val);
} TigerBridgeAPI;

/* Modos de ejecución políglota para máxima velocidad */
typedef enum {
    TIGER_POLYGLOT_BASELINE    = 0, /* CreateProcess estándar en frío (Cold Start) */
    TIGER_POLYGLOT_FAST_FLAGS  = 1, /* Nivel 1: Flags de aislamiento rápido (-S -B -u) */
    TIGER_POLYGLOT_WARM_DAEMON = 2, /* Nivel 2: Demonio persistente en RAM (0.3 ms) */
    TIGER_POLYGLOT_INPROC      = 3  /* Nivel 3: Motor In-Process en silicio (microsegundos) */
} TigerPolyglotMode;

void tiger_set_polyglot_mode(TigerPolyglotMode mode);
TigerPolyglotMode tiger_get_polyglot_mode(void);
void tiger_run_polyglot_benchmark(TigerEnv* env);

/* Inicialización y apagado de demonios persistentes en memoria compartida */
void tiger_polyglot_daemons_init(void);
void tiger_polyglot_daemons_shutdown(void);

/* Despachador de Cajas Negras Opacas a demonios persistentes (Sin tocar disco) */
bool tiger_execute_polyglot_box(TigerEnv* env, const char* lang, const char* code, const char* target_var);

/* Ejecutores individuales en memoria compartida sin disco */
bool tiger_execute_c_block(TigerEnv* env, const char* c_code);
bool tiger_import_c_module(TigerEnv* env, const char* c_path);
bool tiger_execute_py_block(TigerEnv* env, const char* py_code);
bool tiger_execute_js_block(TigerEnv* env, const char* js_code);
bool tiger_execute_html_block(TigerEnv* env, const char* html_code, const char* target_var);
bool tiger_execute_json_block(TigerEnv* env, const char* json_code, const char* target_var);
bool tiger_execute_nosql_block(TigerEnv* env, const char* nosql_code, const char* target_var);
bool tiger_execute_bi_block(TigerEnv* env, const char* hex_code, const char* target_var);

/* Asistente IA / Copiloto MCP integrado */
bool tiger_assistant_call(TigerEnv* env, const char* api_key, const char* model, const char* prompt, const char* target_var);

#endif /* TIGER_C_INTEROP_H */
