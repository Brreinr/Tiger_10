#ifndef TIGER_NATIVE_HW_H
#define TIGER_NATIVE_HW_H

#include "tiger.h"
#include "tiger_ast.h"

/* ========================================================================= */
/* SUBSISTEMA NATIVO DE HARDWARE (CPU / GPU / NPU) Y ENTORNOS GRAFICOS       */
/* Integrado directamente en bin/tiger.exe (Cero dependencia de otros lengs) */
/* ========================================================================= */

typedef enum {
    TIGER_HW_CPU = 0,
    TIGER_HW_GPU = 1,
    TIGER_HW_NPU = 2,
    TIGER_HW_MAS = 3,   /* Procesado cooperativo en los 3 procesadores: CPU + GPU + NPU */
    TIGER_HW_AUTO = 4   /* Auto-acoplamiento por el empaquetador */
} TigerHardwareTarget;

/* Inicializacion y deteccion real de silicio (CPU, GPU, NPU) */
void tiger_hw_init(void);
void tiger_hw_set_target(TigerHardwareTarget target);
TigerHardwareTarget tiger_hw_get_target(void);
const char* tiger_hw_get_target_name(TigerHardwareTarget target);
const char* tiger_hw_get_cpu_name(void);
const char* tiger_hw_get_gpu_name(void);
const char* tiger_hw_get_npu_name(void);
int tiger_hw_get_cpu_cores(void);

/* Comunicacion directa con CPU, GPU y NPU desde Tiger */
TigerValue tiger_hw_cpu_op(TigerEnv* env, int argc, TigerValue* argv);
TigerValue tiger_hw_gpu_op(TigerEnv* env, int argc, TigerValue* argv);
TigerValue tiger_hw_npu_op(TigerEnv* env, int argc, TigerValue* argv);
TigerValue tiger_hw_bci_dispatch(TigerEnv* env, int argc, TigerValue* argv);

/* Primitivas de dibujo 2D y 3D acumulables desde codigo Tiger puro */
void tiger_hw_clear_primitives(void);
void tiger_hw_add_rect2d(int x, int y, int w, int h, int r, int g, int b);
void tiger_hw_add_circle2d(int x, int y, int radius, int r, int g, int b);
void tiger_hw_add_line2d(int x1, int y1, int x2, int y2, int thickness, int r, int g, int b);
void tiger_hw_add_text2d(int x, int y, const char* text, int font_size, int r, int g, int b);
void tiger_hw_add_cube3d(double x, double y, double z, double size, int r, int g, int b);

/* Creacion nativa de Ventanas GUI ("Hola Mundo", Buscador + Boton), Entornos 2D y Entornos 3D */
TigerValue tiger_hw_open_window(TigerEnv* env, const char* title, const char* message, int width, int height);
TigerValue tiger_hw_open_search_window(TigerEnv* env, const char* title, const char* placeholder, const char* button_text, int width, int height);
TigerValue tiger_hw_open_2d(TigerEnv* env, const char* title, int width, int height, int fps);
TigerValue tiger_hw_open_3d(TigerEnv* env, const char* title, int width, int height, int fps);

TigerValue tiger_hw_benchmark_memoria(int mb, int reps, int num_threads);
TigerValue tiger_hw_contar_primos_hilos(int64_t limite, int num_threads);
TigerValue tiger_hw_contar_primos_paralelo(int64_t limite);
TigerValue tiger_hw_multiplicar_matrices_paralelo(TigerValue matA, TigerValue matB, int n);

/* Despachador unificado de funciones nativas de ventana, 2D, 3D y hardware */
bool tiger_hw_try_builtin_call(TigerEnv* env, const char* fn_name, size_t argc, TigerValue* argv, TigerValue* out_result);

/* Interoperabilidad TigerUIX para Backend, JSON y NoSQL */
bool tiger_uix_has_id(const char* id);
bool tiger_uix_nosql_save(const char* id, const char* collection);
bool tiger_uix_nosql_load(const char* id, const char* collection);
TigerValue tiger_uix_export_json_by_id(const char* id);

#endif /* TIGER_NATIVE_HW_H */

