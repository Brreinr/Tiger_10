#ifndef TIGER_MODULES_H
#define TIGER_MODULES_H

#include "tiger.h"

/* ========================================================================= */
/* TIGER 10.2: GESTOR DE MODULOS DINAMICOS Y TELEMETRIA DE MEMORIA RAM       */
/* Carga perezosa (on-demand) de subsistemas para mantener el nucleo ligero. */
/* ========================================================================= */

typedef enum {
    TIGER_MOD_UIX = 0,   /* Interfaz Grafica, ClearType, Ventanas, Canvas, Multimedia */
    TIGER_MOD_AI  = 1,   /* Copiloto IA, Modelos Externos, Servidor MCP JSON-RPC */
    TIGER_MOD_CPU = 2,   /* Hilos de Paralelismo Masivo, Topologia de CPU */
    TIGER_MOD_GPU = 3,   /* Aceleracion por GPU y Shaders */
    TIGER_MOD_NPU = 4,   /* Tensor Engine y Aceleracion NPU / SIMD profunda */
    TIGER_MOD_BCI = 5,   /* Interfaz Cerebro-Computador y Telemetria Neuro */
    TIGER_MOD_COUNT
} TigerModuleType;

/* Informacion descriptiva del modulo */
typedef struct {
    TigerModuleType type;
    const char* name;
    const char* dll_filename;
    const char* description;
    bool is_loaded;
    void* handle;
    size_t loaded_ram_bytes;
} TigerModuleInfo;

/* Inicializacion y consulta del gestor de modulos */
void tiger_modules_init(void);
void tiger_modules_shutdown(void);
bool tiger_module_load(TigerModuleType type);
void tiger_module_unload(TigerModuleType type);
bool tiger_module_is_loaded(TigerModuleType type);
void* tiger_module_get_proc(TigerModuleType type, const char* symbol_name);
const TigerModuleInfo* tiger_module_get_info(TigerModuleType type);
void tiger_modules_print_status(void);

/* ========================================================================= */
/* TELEMETRIA DE MEMORIA RAM DE SILICIO                                      */
/* ========================================================================= */
typedef struct {
    size_t working_set_bytes;       /* RAM fisica en uso real */
    size_t peak_working_set_bytes;  /* Pico maximo de RAM alcanzado */
    size_t private_bytes;           /* Memoria comprometida privada */
    size_t arena_allocated_bytes;   /* Memoria reservada por el Arena Core */
    int active_modules_count;       /* Modulos opcionales activos en RAM */
} TigerMemStats;

TigerMemStats tiger_mem_get_stats(void);
void tiger_mem_print_telemetry(void);

/* Despachador de hardware desacoplado con carga bajo demanda */
bool tiger_modules_dispatch_call(TigerEnv* env, const char* fn_name, size_t argc, TigerValue* argv, TigerValue* out_result);

#endif /* TIGER_MODULES_H */

