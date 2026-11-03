#ifndef TIGER_PARALLEL_H
#define TIGER_PARALLEL_H

#include "tiger.h"
#include "tiger_ast.h"
#include "tiger_interp.h"

/* ========================================================================= */
/* ARQUITECTURA TIGER: 2 PROCESADORES MAESTROS Y 8 SUBPROCESADORES (4 C/U)   */
/* - Procesador Maestro 0: Lógica, CPU, NPU Tensor, BCI y SIMD AVX2          */
/*   -> Controla 4 Subprocesadores (0, 1, 2, 3) en ráfagas de 4 en 4 líneas  */
/* - Procesador Maestro 1: Gráficos, GPU, Ventanas Nativas y Entornos 2D/3D  */
/*   -> Controla 4 Subprocesadores (4, 5, 6, 7) para renderizado y eventos   */
/* Total: 2 Procesadores Maestros y 8 Subprocesadores concurrentes lock-free */
/* ========================================================================= */

#define TIGER_PARALLEL_BURST_SIZE 4       /* Procesar de 4 en 4 líneas / subprocesos */
#define TIGER_PARALLEL_THRESHOLD_LINES 200 /* Umbral adaptativo: < 200 líneas usa 1 analizador rápido; >= 200 líneas activa los 2 maestros y 8 subprocesadores */
#define TIGER_MASTER_PROCESSORS 2         /* 2 Procesadores Maestros (0: Lógica/CPU/NPU, 1: Gráficos/GPU/Ventanas) */
#define TIGER_SUBPROCESSORS_PER_MASTER 4  /* 4 Subprocesadores por cada procesador maestro */
#define TIGER_SUBPROCESSOR_TOTAL (TIGER_MASTER_PROCESSORS * TIGER_SUBPROCESSORS_PER_MASTER) /* 8 subprocesadores en total */
#define TIGER_SUBPROCESSOR_COUNT TIGER_SUBPROCESSOR_TOTAL

/* Paquete de procesos / subprocesos para el analizador concurrente */
typedef struct {
    TigerStmt** stmts;
    size_t count;
    int package_id;
    int master_id;         /* 0: Maestro Lógica/CPU/NPU, 1: Maestro Gráfico/GPU */
    int target_hw;         /* 0: CPU, 1: GPU, 2: NPU, 3: MAS (CPU+GPU+NPU), 4: AUTO */
    const char* target_name;
    bool has_subprocesses; /* true si contiene subprocesos por coma ',' */
} TigerPackage;

/* Inicializa el Thread Pool persistente con los 8 subprocesadores simultáneos */
void tiger_threadpool_init(void);

/* Apaga el Thread Pool persistente al cerrar el motor Tiger */
void tiger_threadpool_shutdown(void);

/* Obtiene estadísticas y telemetría de los 2 maestros y 8 subprocesadores */
void tiger_parallel_get_stats(int* out_masters, int* out_subprocessors, int* out_burst_size);

/* ========================================================================= */
/* PLANIFICADOR ADAPTATIVO INTELIGENTE (RAM-Bound vs Compute-Bound Engine)    */
/* ========================================================================= */

typedef enum {
    TIGER_TASK_UNKNOWN = 0,
    TIGER_TASK_COMPUTE_BOUND, /* Cálculo aritmético puro: aprovecha hasta 8 hilos */
    TIGER_TASK_MEMORY_BOUND    /* Movimiento masivo RAM: auto-limitado a sweet spot (4 hilos) */
} TigerTaskNature;

typedef struct {
    char task_signature[64];
    TigerTaskNature nature;
    int optimal_workers;
    double measured_bandwidth_gbs;
    double measured_time_ms;
} TigerOptimalProfile;

/* Consulta o entrena el perfil óptimo en caché para la tarea */
int tiger_scheduler_get_optimal_workers(const char* task_name, TigerTaskNature nature, size_t data_bytes);
void tiger_scheduler_record_profile(const char* task_name, TigerTaskNature nature, int workers, double time_ms, double bw_gbs);
TigerValue tiger_scheduler_autotune_task(const char* task_name, TigerTaskNature nature, size_t data_bytes);

/* Empaqueta sentencias en lotes cuádruples respetando fronteras de procesos */
TigerPackage* tiger_empaquetar_procesos(TigerStmt** stmts, size_t count, size_t batch_size, size_t* out_pkg_count);

/* Ejecuta los lotes con la arquitectura concurrente lock-free:
   - 2 Analizadores Maestros que dividen párrafos en memoria.
   - 4 Subprocesadores persistentes simultáneos en ráfagas de 4 en 4 líneas.
   - Aceleración vectorial SIMD AVX2 para evaluación numérica y lógica.
   - Ensamblado y escupido ordenado sin bloqueos ni contención de mutexes.
*/
ExecSignal tiger_ejecutar_dual_paralelo(TigerEnv* env, TigerStmt** stmts, size_t count);
bool tiger_exec_parallel_while(TigerEnv* env, TigerStmt* stmt);

#endif /* TIGER_PARALLEL_H */
