#ifndef TIGER_PARALLEL_H
#define TIGER_PARALLEL_H

#include "tiger.h"
#include "tiger_ast.h"
#include "tiger_interp.h"

/* ========================================================================= */
/* ARQUITECTURA CONCURRENTE SIN BLOQUEOS (LOCK-FREE DUAL MASTER & 4 CORES)   */
/* 2 Analizadores Maestros que dividen párrafos en memoria con detector      */
/* inteligente que protege decimales y archivos.                            */
/* Reparto de lógica nativa a Thread Pool persistente con 4 subprocesadores   */
/* simultáneos procesando de 4 en 4 líneas potenciados con SIMD AVX2.       */
/* ========================================================================= */

#define TIGER_PARALLEL_BURST_SIZE 4   /* Procesar de 4 en 4 líneas / subprocesos */
#define TIGER_SUBPROCESSOR_COUNT 4    /* 4 subprocesadores persistentes */

/* Paquete de procesos / subprocesos para el analizador concurrente */
typedef struct {
    TigerStmt** stmts;
    size_t count;
    int package_id;
    bool has_subprocesses; /* true si contiene subprocesos por coma ',' */
} TigerPackage;

/* Inicializa el Thread Pool persistente de 4 subprocesadores simultáneos */
void tiger_threadpool_init(void);

/* Apaga el Thread Pool persistente al cerrar el motor Tiger */
void tiger_threadpool_shutdown(void);

/* Empaqueta sentencias en lotes cuádruples respetando fronteras de procesos */
TigerPackage* tiger_empaquetar_procesos(TigerStmt** stmts, size_t count, size_t batch_size, size_t* out_pkg_count);

/* Ejecuta los lotes con la arquitectura concurrente lock-free:
   - 2 Analizadores Maestros que dividen párrafos en memoria.
   - 4 Subprocesadores persistentes simultáneos en ráfagas de 4 en 4 líneas.
   - Aceleración vectorial SIMD AVX2 para evaluación numérica y lógica.
   - Ensamblado y escupido ordenado sin bloqueos ni contención de mutexes.
*/
ExecSignal tiger_ejecutar_dual_paralelo(TigerEnv* env, TigerStmt** stmts, size_t count);

#endif /* TIGER_PARALLEL_H */
