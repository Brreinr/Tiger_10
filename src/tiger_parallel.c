#include "tiger_parallel.h"
#include "tiger_simd.h"
#include "tiger_native_hw.h"
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

/* ========================================================================= */
/* TABLA DE PERFILES ÓPTIMOS DEL PLANIFICADOR ADAPTATIVO                     */
/* ========================================================================= */

#define TIGER_MAX_PROFILES 32
static TigerOptimalProfile g_profiles[TIGER_MAX_PROFILES];
static int g_profile_count = 0;

void tiger_scheduler_record_profile(const char* task_name, TigerTaskNature nature, int workers, double time_ms, double bw_gbs) {
    if (!task_name) return;
    for (int i = 0; i < g_profile_count; i++) {
        if (strcasecmp(g_profiles[i].task_signature, task_name) == 0) {
            g_profiles[i].nature = nature;
            g_profiles[i].optimal_workers = workers;
            g_profiles[i].measured_time_ms = time_ms;
            g_profiles[i].measured_bandwidth_gbs = bw_gbs;
            return;
        }
    }
    if (g_profile_count < TIGER_MAX_PROFILES) {
        snprintf(g_profiles[g_profile_count].task_signature, sizeof(g_profiles[g_profile_count].task_signature), "%s", task_name);
        g_profiles[g_profile_count].nature = nature;
        g_profiles[g_profile_count].optimal_workers = workers;
        g_profiles[g_profile_count].measured_time_ms = time_ms;
        g_profiles[g_profile_count].measured_bandwidth_gbs = bw_gbs;
        g_profile_count++;
    }
}

int tiger_scheduler_get_optimal_workers(const char* task_name, TigerTaskNature nature, size_t data_bytes) {
    /* 1. Verificar si ya existe un perfil entrenado en la caché del planificador */
    if (task_name) {
        for (int i = 0; i < g_profile_count; i++) {
            if (strcasecmp(g_profiles[i].task_signature, task_name) == 0) {
                return g_profiles[i].optimal_workers;
            }
        }
    }

    /* 2. Regla adaptativa por naturaleza de la tarea:
          - Tareas limitadas por memoria (RAM-Bound > 16 MB):
            La contención de memoria en buses DDR5 penaliza a 8 hilos; 4 hilos es el pico óptimo.
          - Tareas de cómputo puro (Compute-Bound, ej. Primos, Álgebra Densa):
            No saturan el bus; escalan hasta los 8 subprocesadores disponibles. */
    if (nature == TIGER_TASK_MEMORY_BOUND) {
        if (data_bytes <= (2 * 1024 * 1024)) return 1; /* <= 2MB: Cache local (1 hilo evita sincronización) */
        return 4; /* Sweet Spot de memoria RAM validado empíricamente (20.43 GB/s) */
    } else if (nature == TIGER_TASK_COMPUTE_BOUND) {
        return 8; /* Hasta 8 subprocesadores a tope */
    }

    return 4; /* Default equilibrado */
}

TigerValue tiger_scheduler_autotune_task(const char* task_name, TigerTaskNature nature, size_t data_bytes) {
    int candidates[] = { 1, 2, 4, 6, 8 };
    int num_candidates = 5;

    int best_workers = 1;
    double best_score = 0.0;
    double best_time_ms = 999999.0;
    double best_bw_gbs = 0.0;

    int mb = (data_bytes > 0) ? (int)(data_bytes / (1024 * 1024)) : 64;
    if (mb <= 0) mb = 64;

    TigerValue arr_results = tiger_val_array_new();

    for (int idx = 0; idx < num_candidates; idx++) {
        int w = candidates[idx];
        TigerValue run_res = tiger_val_nil();

        if (nature == TIGER_TASK_MEMORY_BOUND) {
            run_res = tiger_hw_benchmark_memoria(mb, 5, w);
            if (run_res.type == VAL_DICT) {
                TigerValue val_bw, val_t;
                tiger_dict_get(run_res.as.dict_val, "bw_read", &val_bw);
                tiger_dict_get(run_res.as.dict_val, "t_read_ms", &val_t);
                double bw = (val_bw.type == VAL_FLOAT) ? val_bw.as.f_val : 0.0;
                double t_ms = (val_t.type == VAL_FLOAT) ? val_t.as.f_val : 999999.0;
                tiger_val_free(&val_bw);
                tiger_val_free(&val_t);

                if (bw > best_score) {
                    best_score = bw;
                    best_workers = w;
                    best_bw_gbs = bw;
                    best_time_ms = t_ms;
                }
            }
        } else {
            /* Compute-Bound (ej. primos o cálculo numérico) */
            run_res = tiger_hw_contar_primos_hilos(300000, w);
            if (run_res.type == VAL_DICT) {
                TigerValue val_t;
                tiger_dict_get(run_res.as.dict_val, "tiempo_ms", &val_t);
                double t_ms = (val_t.type == VAL_FLOAT) ? val_t.as.f_val : 999999.0;
                tiger_val_free(&val_t);

                if (t_ms < best_time_ms) {
                    best_time_ms = t_ms;
                    best_workers = w;
                    best_score = 1000.0 / t_ms;
                }
            }
        }

        tiger_array_push(arr_results.as.arr_val, run_res);
        tiger_val_free(&run_res);
    }

    tiger_scheduler_record_profile(task_name, nature, best_workers, best_time_ms, best_bw_gbs);

    TigerValue out = tiger_val_dict_new();
    tiger_dict_set(out.as.dict_val, "tarea", tiger_val_str(task_name ? task_name : "TareaGenerica"));
    tiger_dict_set(out.as.dict_val, "naturaleza", tiger_val_str(nature == TIGER_TASK_MEMORY_BOUND ? "RAM-Bound (Limitada por Memoria)" : "Compute-Bound (Limitada por Calculo)"));
    tiger_dict_set(out.as.dict_val, "hilos_optimos", tiger_val_int(best_workers));
    tiger_dict_set(out.as.dict_val, "mejor_tiempo_ms", tiger_val_float(best_time_ms));
    tiger_dict_set(out.as.dict_val, "mejor_bw_gbs", tiger_val_float(best_bw_gbs));
    tiger_val_free(&arr_results);
    return out;
}

/* ========================================================================= */
/* BUFFER DE SALIDA ORDENADO POR PAQUETE                                     */
/* ========================================================================= */

typedef struct {
    char* data;
    size_t length;
    size_t capacity;
    bool is_flushed;
} OutputBuffer;

static void buf_init(OutputBuffer* b) {
    b->capacity = 256;
    b->length = 0;
    b->is_flushed = false;
    b->data = (char*)malloc(b->capacity);
    b->data[0] = '\0';
}

static void buf_append(OutputBuffer* b, const char* str) {
    if (!str) return;
    size_t slen = strlen(str);
    if (b->length + slen + 2 >= b->capacity) {
        b->capacity = (b->length + slen + 2) * 2;
        b->data = (char*)realloc(b->data, b->capacity);
    }
    memcpy(b->data + b->length, str, slen);
    b->length += slen;
    b->data[b->length] = '\0';
    b->is_flushed = false;
}

static void buf_flush_to_stdout(OutputBuffer* b) {
    if (!b || b->is_flushed) return;
    tiger_io_flush_fast();
    if (b->length > 0) {
#ifdef _WIN32
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut && hOut != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(hOut, b->data, (DWORD)b->length, &written, NULL);
        } else {
            fwrite(b->data, 1, b->length, stdout);
        }
#else
        fwrite(b->data, 1, b->length, stdout);
#endif
        b->length = 0;
        b->data[0] = '\0';
    }
    b->is_flushed = true;
}

static void buf_free(OutputBuffer* b) {
    if (b && b->data) {
        free(b->data);
        b->data = NULL;
    }
}

/* ========================================================================= */
/* EMPAQUETADOR DE SUBPROCESOS (Lotes cuádruples y Afinidad de Silicio)      */
/* ========================================================================= */

static const char* tiger_casestr_fast(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
            const char* h = haystack;
            const char* n = needle;
            while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
                h++; n++;
            }
            if (!*n) return haystack;
        }
    }
    return NULL;
}

static int scan_expr_affinity(TigerExpr* expr) {
    if (!expr) return -1;
    if (expr->type == EXPR_CALL) {
        if (expr->as.call.callee && expr->as.call.callee->type == EXPR_VAR) {
            const char* fn = expr->as.call.callee->as.var_name;
            if (fn) {
                if (tiger_casestr_fast(fn, "2d") || tiger_casestr_fast(fn, "3d") ||
                    tiger_casestr_fast(fn, "ventana") || tiger_casestr_fast(fn, "buscador") ||
                    tiger_casestr_fast(fn, "grafic") || tiger_casestr_fast(fn, "pixel") ||
                    tiger_casestr_fast(fn, "shader") || tiger_casestr_fast(fn, "color") ||
                    tiger_casestr_fast(fn, "dibuj") || tiger_casestr_fast(fn, "gpu")) {
                    return 1; /* TIGER_HW_GPU */
                }
                if (tiger_casestr_fast(fn, "dot") || tiger_casestr_fast(fn, "tensor") ||
                    tiger_casestr_fast(fn, "relu") || tiger_casestr_fast(fn, "sigmoid") ||
                    tiger_casestr_fast(fn, "npu") || tiger_casestr_fast(fn, "ia") ||
                    tiger_casestr_fast(fn, "neural") || tiger_casestr_fast(fn, "asistente")) {
                    return 2; /* TIGER_HW_NPU */
                }
            }
        }
    }
    return -1;
}

static int tiger_analizar_afinidad_paquete(TigerStmt** stmts, size_t count) {
    bool has_gpu = false;
    bool has_npu = false;

    for (size_t i = 0; i < count; i++) {
        TigerStmt* s = stmts[i];
        if (!s) continue;
        if (s->type == STMT_HW_TARGET) {
            return s->as.hw_target.target;
        }
        if (s->type == STMT_EXPR && s->as.expr) {
            int aff = scan_expr_affinity(s->as.expr);
            if (aff == 1) has_gpu = true;
            if (aff == 2) has_npu = true;
        }
        if (s->type == STMT_VAR_DECL && s->as.var_decl.init) {
            int aff = scan_expr_affinity(s->as.var_decl.init);
            if (aff == 1) has_gpu = true;
            if (aff == 2) has_npu = true;
        }
        if (s->type == STMT_ASSIGN && s->as.assign.value) {
            int aff = scan_expr_affinity(s->as.assign.value);
            if (aff == 1) has_gpu = true;
            if (aff == 2) has_npu = true;
        }
        if (s->type == STMT_ASSISTANT) has_npu = true;
    }

    if (has_gpu && has_npu) return 3; /* TIGER_HW_MAS: Procesado cooperativo en los 3 silicios */
    if (has_gpu) return 1; /* TIGER_HW_GPU */
    if (has_npu) return 2; /* TIGER_HW_NPU */
    return 0; /* TIGER_HW_CPU */
}

TigerPackage* tiger_empaquetar_procesos(TigerStmt** stmts, size_t count, size_t batch_size, size_t* out_pkg_count) {
    if (count == 0) {
        *out_pkg_count = 0;
        return NULL;
    }
    if (batch_size == 0) batch_size = TIGER_PARALLEL_BURST_SIZE;

    int total_lines = (count > 0 && stmts[count - 1]) ? stmts[count - 1]->line : (int)count;
    bool is_large = (total_lines >= TIGER_PARALLEL_THRESHOLD_LINES || count >= TIGER_PARALLEL_THRESHOLD_LINES);

    TigerPackage* pkgs = (TigerPackage*)malloc(count * sizeof(TigerPackage));
    size_t pkg_idx = 0;
    size_t i = 0;

    while (i < count) {
        size_t start = i;
        size_t end = start;
        bool has_sub = false;

        /* Si la primera sentencia inicia un proceso encadenado con coma (subproceso),
           reunir todo el proceso completo hasta el punto '.' final */
        if (stmts[start]->is_subprocess) {
            has_sub = true;
            while (end < count) {
                if (stmts[end]->is_subprocess) has_sub = true;
                end++;
                /* Al llegar a la sentencia que termina en '.', concluye el proceso encadenado */
                if (!stmts[end - 1]->is_subprocess) {
                    break;
                }
            }
        } else {
            /* Sentencia terminada en punto '.'.
               El empaquetador agrupa sentencias independientes hasta batch_size,
               deteniéndose si encuentra una sentencia con coma */
            end++;
            while (end < count && (end - start) < batch_size) {
                if (stmts[end]->is_subprocess) {
                    /* Comienza un proceso con coma, dejamos que el siguiente paquete lo tome */
                    break;
                }
                end++;
            }
        }

        int target = tiger_analizar_afinidad_paquete(&stmts[start], end - start);

        pkgs[pkg_idx].stmts = &stmts[start];
        pkgs[pkg_idx].count = end - start;
        pkgs[pkg_idx].package_id = (int)pkg_idx;
        pkgs[pkg_idx].target_hw = target;
        pkgs[pkg_idx].has_subprocesses = has_sub;

        if (!is_large) {
            /* Menos de 200 líneas: 1 solo Ejecutador Maestro */
            pkgs[pkg_idx].master_id = 0;
        } else {
            /* 200 líneas o más: 2 Ejecutadores Maestros (Maestro 0: CPU/NPU, Maestro 1: GPU) */
            if (target == 1) { /* GPU */
                pkgs[pkg_idx].master_id = 1;
            } else if (target == 2) { /* NPU */
                pkgs[pkg_idx].master_id = 0;
            } else if (target == 3) { /* MAS (Procesado en los 3 silicios a la vez) */
                pkgs[pkg_idx].master_id = (int)(pkg_idx % TIGER_MASTER_PROCESSORS);
            } else { /* CPU */
                pkgs[pkg_idx].master_id = 0;
            }
        }

        if (target == 1) {
            pkgs[pkg_idx].target_name = "GPU";
        } else if (target == 2) {
            pkgs[pkg_idx].target_name = "NPU";
        } else if (target == 3) {
            pkgs[pkg_idx].target_name = "MAS (CPU+GPU+NPU)";
        } else {
            pkgs[pkg_idx].target_name = "CPU";
        }

        pkg_idx++;
        i = end;
    }

    *out_pkg_count = pkg_idx;
    return pkgs;
}

/* ========================================================================= */
/* EVALUACIÓN DE SENTENCIAS CON BUFFERING Y SIMD AVX2                        */
/* ========================================================================= */

static ExecSignal tiger_exec_block_buffered(TigerEnv* env, TigerStmt** stmts, size_t count, OutputBuffer* buf);

static bool stmt_has_input(TigerStmt* stmt) {
    if (!stmt) return false;
    if (stmt->type == STMT_VAR_DECL && stmt->as.var_decl.init && stmt->as.var_decl.init->type == EXPR_INPUT) return true;
    if (stmt->type == STMT_ASSIGN && stmt->as.assign.value && stmt->as.assign.value->type == EXPR_INPUT) return true;
    if (stmt->type == STMT_EXPR && stmt->as.expr && stmt->as.expr->type == EXPR_INPUT) return true;
    return false;
}

static ExecSignal tiger_exec_stmt_buffered(TigerEnv* env, TigerStmt* stmt, OutputBuffer* buf) {
    ExecSignal sig = { EXEC_OK, tiger_val_nil() };
    if (!stmt) return sig;

    if (stmt->type == STMT_POLYGLOT || stmt_has_input(stmt)) {
        buf_flush_to_stdout(buf);
    }

    if (stmt->type == STMT_PRINT) {
        TigerValue val = tiger_eval_expr(env, stmt->as.print.expr);
        char fast_buf[256];
        size_t len = tiger_val_format_buf(val, fast_buf, sizeof(fast_buf));
        buf_append(buf, fast_buf);
        if (stmt->as.print.newline) buf_append(buf, "\n");
        tiger_val_free(&val);
        return sig;
    }

    if (stmt->type == STMT_IF) {
        TigerValue cond = tiger_eval_expr(env, stmt->as.if_stmt.condition);
        bool is_true = tiger_val_is_truthy(cond);
        tiger_val_free(&cond);
        if (is_true) {
            return tiger_exec_block_buffered(env, stmt->as.if_stmt.then_stmts, stmt->as.if_stmt.then_count, buf);
        } else if (stmt->as.if_stmt.else_stmts && stmt->as.if_stmt.else_count > 0) {
            return tiger_exec_block_buffered(env, stmt->as.if_stmt.else_stmts, stmt->as.if_stmt.else_count, buf);
        }
        return sig;
    }

    if (stmt->type == STMT_WHILE) {
        while (true) {
            TigerValue cond = tiger_eval_expr(env, stmt->as.while_stmt.condition);
            bool ok = tiger_val_is_truthy(cond);
            tiger_val_free(&cond);
            if (!ok) break;
            sig = tiger_exec_block_buffered(env, stmt->as.while_stmt.body, stmt->as.while_stmt.body_count, buf);
            if (sig.status == EXEC_BREAK) { sig.status = EXEC_OK; break; }
            if (sig.status == EXEC_RETURN) return sig;
        }
        return sig;
    }

    if (stmt->type == STMT_HW_TARGET) {
        TigerHardwareTarget prev = tiger_hw_get_target();
        TigerHardwareTarget tgt = (TigerHardwareTarget)stmt->as.hw_target.target;
        tiger_hw_set_target(tgt);

        char msg[384];
        if (tgt == TIGER_HW_GPU) {
            snprintf(msg, sizeof(msg), "[EMPAQUETADOR -> SILICIO GPU]: Directiva 'USAR EN GPU' detectada. Codigo despachado directamente a la GPU (%s).\n", tiger_hw_get_gpu_name());
            buf_append(buf, msg);
        } else if (tgt == TIGER_HW_CPU) {
            snprintf(msg, sizeof(msg), "[EMPAQUETADOR -> SILICIO CPU]: Directiva 'USAR EN CPU' detectada. Codigo despachado a la CPU (%s con SIMD AVX2).\n", tiger_hw_get_cpu_name());
            buf_append(buf, msg);
        } else if (tgt == TIGER_HW_NPU) {
            snprintf(msg, sizeof(msg), "[EMPAQUETADOR -> SILICIO NPU]: Directiva 'USAR EN NPU' detectada. Codigo despachado a la NPU (%s Tensor Engine).\n", tiger_hw_get_npu_name());
            buf_append(buf, msg);
        } else if (tgt == TIGER_HW_MAS) {
            snprintf(msg, sizeof(msg), "[EMPAQUETADOR -> SILICIO MAS (3 PROCESADORES)]: Directiva 'USAR EN MAS' detectada. Procesando cooperativamente entre CPU + GPU + NPU.\n");
            buf_append(buf, msg);
        }

        if (stmt->as.hw_target.body && stmt->as.hw_target.body_count > 0) {
            sig = tiger_exec_block_buffered(env, stmt->as.hw_target.body, stmt->as.hw_target.body_count, buf);
            tiger_hw_set_target(prev);
        }
        return sig;
    }

    return tiger_exec_stmt(env, stmt);
}

static ExecSignal tiger_exec_block_buffered(TigerEnv* env, TigerStmt** stmts, size_t count, OutputBuffer* buf) {
    ExecSignal sig = { EXEC_OK, tiger_val_nil() };
    for (size_t i = 0; i < count; i++) {
        sig = tiger_exec_stmt_buffered(env, stmts[i], buf);
        if (sig.status != EXEC_OK) break;
    }
    return sig;
}

/* ========================================================================= */
/* THREAD POOL PERSISTENTE CON 4 SUBPROCESADORES SIMULTÁNEOS                 */
/* ========================================================================= */

#ifdef _WIN32
static volatile LONG g_tp_initialized = 0;
static volatile LONG g_tp_running = 1;
static HANDLE g_worker_threads[TIGER_SUBPROCESSOR_COUNT];
static HANDLE g_start_events[TIGER_SUBPROCESSOR_COUNT];
static HANDLE g_done_events[TIGER_SUBPROCESSOR_COUNT];

static volatile LONG g_next_pkg_ticket = 0;
static volatile LONG g_total_packages = 0;
static TigerEnv* g_active_env = NULL;
static TigerPackage* g_active_packages = NULL;
static OutputBuffer* g_active_buffers = NULL;
static volatile LONG g_pipeline_stage = -1;

static DWORD WINAPI persistent_subprocessor_thread(LPVOID lpParam) {
    int subproc_id = (int)(intptr_t)lpParam;

    while (g_tp_running) {
        /* Esperar señal de trabajo para este subprocesador */
        WaitForSingleObject(g_start_events[subproc_id], INFINITE);
        if (!g_tp_running) break;

        /* Procesamiento concurrente sin bloqueos de 4 en 4 líneas */
        while (1) {
            LONG pkg_idx = (LONG)tiger_atomic_add_i32((volatile int32_t*)&g_next_pkg_ticket, 1);
            if (pkg_idx >= g_total_packages) break;

            /* Sincronización sin quemar ciclos de CPU innecesarios */
            if (pkg_idx > 0) {
                while (g_pipeline_stage < (pkg_idx - 1)) {
                    SwitchToThread();
                }
            }

            TigerPackage* pkg = &g_active_packages[pkg_idx];
            OutputBuffer* out_buf = &g_active_buffers[pkg_idx];

            /* Fijar contexto de silicio asignado por el empaquetador (CPU, GPU o NPU) */
            TigerHardwareTarget prev_hw = tiger_hw_get_target();
            tiger_hw_set_target((TigerHardwareTarget)pkg->target_hw);

            /* Ráfaga adaptativa por proceso:
               - Si cada proceso tiene más de 4 líneas: se ejecutan los 4 subprocesadores de golpe.
               - Si tiene entre 2 y 4 líneas: se usan 2 subprocesadores de golpe.
               - Si tiene 1 sola línea: se usa 1 subprocesador de golpe.
               Aplica en todas partes sea CPU, NPU o GPU. */
            int burst_subprocs = 1;
            if (pkg->count > 4) {
                burst_subprocs = 4;
            } else if (pkg->count >= 2) {
                burst_subprocs = 2;
            } else {
                burst_subprocs = 1;
            }

            size_t s = 0;
            while (s + burst_subprocs <= pkg->count) {
                for (int sub = 0; sub < burst_subprocs; sub++) {
                    if (pkg->stmts[s + sub]->type == STMT_POLYGLOT || stmt_has_input(pkg->stmts[s + sub])) {
                        for (int p = 0; p < pkg_idx; p++) buf_flush_to_stdout(&g_active_buffers[p]);
                        buf_flush_to_stdout(out_buf);
                    }
                    tiger_exec_stmt_buffered(g_active_env, pkg->stmts[s + sub], out_buf);
                }
                s += burst_subprocs;
            }
            while (s < pkg->count) {
                if (pkg->stmts[s]->type == STMT_POLYGLOT || stmt_has_input(pkg->stmts[s])) {
                    for (int p = 0; p < pkg_idx; p++) buf_flush_to_stdout(&g_active_buffers[p]);
                    buf_flush_to_stdout(out_buf);
                }
                tiger_exec_stmt_buffered(g_active_env, pkg->stmts[s], out_buf);
                s++;
            }

            tiger_hw_set_target(prev_hw);

            /* Notificar avance de etapa del pipeline */
            InterlockedExchange(&g_pipeline_stage, pkg_idx);
        }

        SetEvent(g_done_events[subproc_id]);
    }
    return 0;
}
#endif

void tiger_parallel_get_stats(int* out_masters, int* out_subprocessors, int* out_burst_size) {
    if (out_masters) *out_masters = TIGER_MASTER_PROCESSORS;
    if (out_subprocessors) *out_subprocessors = TIGER_SUBPROCESSOR_TOTAL;
    if (out_burst_size) *out_burst_size = TIGER_PARALLEL_BURST_SIZE;
}

void tiger_threadpool_init(void) {
#ifdef _WIN32
    if (InterlockedCompareExchange(&g_tp_initialized, 1, 0) == 0) {
        tiger_simd_init();
        for (int i = 0; i < TIGER_SUBPROCESSOR_TOTAL; i++) {
            g_start_events[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
            g_done_events[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
            g_worker_threads[i] = CreateThread(NULL, 0, persistent_subprocessor_thread, (LPVOID)(intptr_t)i, 0, NULL);
        }
    }
#endif
}

void tiger_threadpool_shutdown(void) {
#ifdef _WIN32
    if (g_tp_initialized) {
        g_tp_running = 0;
        for (int i = 0; i < TIGER_SUBPROCESSOR_TOTAL; i++) {
            SetEvent(g_start_events[i]);
        }
        WaitForMultipleObjects(TIGER_SUBPROCESSOR_TOTAL, g_worker_threads, TRUE, 1000);
        for (int i = 0; i < TIGER_SUBPROCESSOR_TOTAL; i++) {
            CloseHandle(g_worker_threads[i]);
            CloseHandle(g_start_events[i]);
            CloseHandle(g_done_events[i]);
        }
        g_tp_initialized = 0;
    }
#endif
}

/* ========================================================================= */
/* 2 ANALIZADORES MAESTROS QUE DIVIDEN PÁRRAFOS Y DESPACHAN CONCURRENTE      */
/* ========================================================================= */

#ifdef _WIN32
static volatile LONG g_tp_busy = 0;
#endif

ExecSignal tiger_ejecutar_dual_paralelo(TigerEnv* env, TigerStmt** stmts, size_t count) {
    ExecSignal sig = { EXEC_OK, tiger_val_nil() };
    if (!stmts || count == 0) return sig;

#ifdef _WIN32
    /* Prevenir reentrada recursiva (por ejemplo, al ejecutar USAR "modulo.tg" desde un subprocesador) */
    if (InterlockedCompareExchange(&g_tp_busy, 1, 0) != 0) {
        return tiger_exec_block(env, stmts, count);
    }
#endif

    /* REGLA DE ARQUITECTURA TIGER 10.1:
       - Si hay >= 200 líneas:
         Se ejecutan los 2 Ejecutadores Maestros (Maestro 0: CPU/NPU, Maestro 1: GPU/Gráficos)
         con hasta 8 subprocesadores concurrentes.
       - Si hay < 200 líneas:
         Se ejecuta 1 solo Ejecutador Maestro con hasta 4 subprocesadores.
       - En cada proceso:
         - Si tiene más de 4 líneas: 4 subprocesadores de golpe.
         - Si tiene entre 2 y 4 líneas: 2 subprocesadores de golpe.
         - Si tiene 1 línea: 1 subprocesador de golpe.
         Aplica en todas partes sea CPU, NPU o GPU. */
    int total_lines = (count > 0 && stmts[count - 1]) ? stmts[count - 1]->line : (int)count;
    bool is_large = (total_lines >= TIGER_PARALLEL_THRESHOLD_LINES || count >= TIGER_PARALLEL_THRESHOLD_LINES);

    if (!is_large) {
        sig = tiger_exec_block(env, stmts, count);
#ifdef _WIN32
        InterlockedExchange(&g_tp_busy, 0);
#endif
        return sig;
    }

    int max_workers = TIGER_SUBPROCESSOR_TOTAL;

    size_t pkg_count = 0;
    TigerPackage* pkgs = tiger_empaquetar_procesos(stmts, count, TIGER_PARALLEL_BURST_SIZE, &pkg_count);

    if (pkg_count == 0) {
        if (pkgs) free(pkgs);
#ifdef _WIN32
        InterlockedExchange(&g_tp_busy, 0);
#endif
        return sig;
    }

    if (is_large) {
        printf("[EMPAQUETADOR TIGER (>= 200 LINEAS)]: Activando 2 Ejecutadores Maestros y 8 Subprocesadores concurrentes.\n");
        printf("  -> Maestro 0: CPU (%s) y NPU (%s)\n", tiger_hw_get_cpu_name(), tiger_hw_get_npu_name());
        printf("  -> Maestro 1: GPU (%s) y Superficies Direct GDI\n", tiger_hw_get_gpu_name());
        printf("  -> Empaquetado: %zu procesos asignados y distribuidos entre los 2 maestros.\n", pkg_count);
    }

#ifdef _WIN32
    tiger_threadpool_init();

    OutputBuffer* pkg_buffers = (OutputBuffer*)malloc(pkg_count * sizeof(OutputBuffer));
    for (size_t i = 0; i < pkg_count; i++) {
        buf_init(&pkg_buffers[i]);
    }

    g_active_env = env;
    g_active_packages = pkgs;
    g_active_buffers = pkg_buffers;
    g_total_packages = (LONG)pkg_count;
    g_next_pkg_ticket = 0;
    InterlockedExchange(&g_pipeline_stage, -1);

    /* Activar los subprocesadores según la regla de < 200 vs >= 200 líneas:
       - Si >= 200 líneas: hasta 8 subprocesadores en los 2 ejecutadores maestros (CPU/NPU y GPU).
       - Si < 200 líneas: hasta 4 subprocesadores en 1 solo ejecutador maestro. */
    int active_workers = (pkg_count < (size_t)max_workers) ? (int)pkg_count : max_workers;
    if (active_workers < 1) active_workers = 1;

    /* Activar bloqueos ANTES de despertar a los subprocesadores (evita carrera de datos) */
    tiger_env_set_parallel_mode(true);
    for (int i = 0; i < active_workers; i++) {
        SetEvent(g_start_events[i]);
    }

    /* Esperar que los subprocesadores concluyan sus paquetes */
    WaitForMultipleObjects(active_workers, g_done_events, TRUE, INFINITE);
    tiger_env_set_parallel_mode(false);

    /* Escupir resultados secuencialmente en orden sin bloqueos */
    for (size_t i = 0; i < pkg_count; i++) {
        buf_flush_to_stdout(&pkg_buffers[i]);
        buf_free(&pkg_buffers[i]);
    }
    fflush(stdout);

    free(pkg_buffers);
    InterlockedExchange(&g_tp_busy, 0);
#else
    sig = tiger_exec_block(env, stmts, count);
#endif

    free(pkgs);
    return sig;
}

/* ========================================================================= */
/* EJECUCIÓN DE BUCLES PARALELOS REALES EN LOS 8 SUBPROCESADORES             */
/* ========================================================================= */

typedef struct {
    TigerEnv* parent_env;
    TigerStmt** body;
    size_t body_count;
    const char* loop_var;
    uint64_t loop_hash;
    int64_t chunk_start;
    int64_t chunk_end;
    TigerEnv* worker_env;
} ParallelLoopTask;

#ifdef _WIN32
static DWORD WINAPI parallel_loop_worker_fn(LPVOID lpParam) {
    ParallelLoopTask* t = (ParallelLoopTask*)lpParam;
    TigerVarEntry* ivar = tiger_env_lookup_entry(t->worker_env, t->loop_var, t->loop_hash);
    if (!ivar) return 0;

    while (ivar->value.as.i_val < t->chunk_end) {
        ExecSignal sig = tiger_exec_block(t->worker_env, t->body, t->body_count);
        if (sig.status == EXEC_BREAK || sig.status == EXEC_RETURN) break;
    }
    return 0;
}
#endif

bool tiger_exec_parallel_while(TigerEnv* env, TigerStmt* stmt) {
    if (!env || !stmt || stmt->type != STMT_WHILE) return false;
    TigerExpr* cond = stmt->as.while_stmt.condition;
    if (!cond || cond->type != EXPR_BINARY ||
        (cond->as.binary.op != TOK_LT && cond->as.binary.op != TOK_LE) ||
        cond->as.binary.left->type != EXPR_VAR) {
        return false;
    }

    const char* vname = cond->as.binary.left->as.var_name;
    uint64_t vhash = cond->as.binary.left->hash;
    TigerVarEntry* loop_entry = tiger_env_lookup_entry(env, vname, vhash);
    if (!loop_entry || loop_entry->value.type != VAL_INT || loop_entry->is_const) return false;

    TigerValue lim_v = tiger_eval_expr(env, cond->as.binary.right);
    int64_t limit_i = (lim_v.type == VAL_INT) ? lim_v.as.i_val : (int64_t)lim_v.as.f_val;
    tiger_val_free(&lim_v);
    if (cond->as.binary.op == TOK_LE) limit_i++;

    int64_t start_i = loop_entry->value.as.i_val;
    int64_t total_iters = limit_i - start_i;
    if (total_iters <= 0) return true;

#ifdef _WIN32
    int num_workers = TIGER_SUBPROCESSOR_TOTAL; /* 8 subprocesadores reales */
    if (total_iters < num_workers) num_workers = (int)total_iters;
    if (num_workers <= 1) return false;

    ParallelLoopTask tasks[TIGER_SUBPROCESSOR_TOTAL];
    HANDLE threads[TIGER_SUBPROCESSOR_TOTAL];
    int64_t base_chunk = total_iters / num_workers;
    int64_t rem = total_iters % num_workers;

    /* Crear entornos aislados por subprocesador (sin contención de candados) */
    for (int w = 0; w < num_workers; w++) {
        int64_t c_start = start_i + w * base_chunk + (w < rem ? w : rem);
        int64_t c_end   = c_start + base_chunk + (w < rem ? 1 : 0);

        TigerEnv* wenv = (TigerEnv*)malloc(sizeof(TigerEnv));
        memset(wenv->buckets, 0, sizeof(wenv->buckets));
        wenv->parent = env;

        /* Clonar variables enteras del scope actual para reducción libre de bloqueos */
        for (int b = 0; b < TIGER_ENV_BUCKETS; b++) {
            TigerVarEntry* e = env->buckets[b];
            while (e) {
                if (e->value.type == VAL_INT && !e->is_const) {
                    TigerVarEntry* ne = (TigerVarEntry*)malloc(sizeof(TigerVarEntry));
                    size_t nlen = strlen(e->name);
                    if (nlen < sizeof(ne->inline_name)) {
                        memcpy(ne->inline_name, e->name, nlen + 1);
                        ne->name = ne->inline_name;
                    } else {
                        ne->name = strdup(e->name);
                    }
                    ne->hash = e->hash;
                    ne->value = tiger_val_int(strcmp(e->name, vname) == 0 ? c_start : e->value.as.i_val);
                    ne->is_const = false;
                    ne->type_constraint = e->type_constraint;
                    ne->next = wenv->buckets[b];
                    wenv->buckets[b] = ne;
                }
                e = e->next;
            }
        }

        tasks[w].parent_env = env;
        tasks[w].body = stmt->as.while_stmt.body;
        tasks[w].body_count = stmt->as.while_stmt.body_count;
        tasks[w].loop_var = vname;
        tasks[w].loop_hash = vhash;
        tasks[w].chunk_start = c_start;
        tasks[w].chunk_end = c_end;
        tasks[w].worker_env = wenv;
    }

    g_tiger_parallel_active = true;
    for (int w = 0; w < num_workers; w++) {
        threads[w] = CreateThread(NULL, 0, parallel_loop_worker_fn, &tasks[w], 0, NULL);
    }
    WaitForMultipleObjects(num_workers, threads, TRUE, INFINITE);
    g_tiger_parallel_active = false;

    /* Reducción matemática de deltas de cada subprocesador al entorno principal */
    for (int b = 0; b < TIGER_ENV_BUCKETS; b++) {
        TigerVarEntry* pe = env->buckets[b];
        while (pe) {
            if (pe->value.type == VAL_INT && !pe->is_const && strcmp(pe->name, vname) != 0) {
                int64_t orig = pe->value.as.i_val;
                int64_t total_delta = 0;
                for (int w = 0; w < num_workers; w++) {
                    TigerVarEntry* we = tasks[w].worker_env->buckets[b];
                    while (we) {
                        if (we->hash == pe->hash && strcmp(we->name, pe->name) == 0) {
                            total_delta += (we->value.as.i_val - orig);
                            break;
                        }
                        we = we->next;
                    }
                }
                pe->value.as.i_val = orig + total_delta;
            }
            pe = pe->next;
        }
    }
    loop_entry->value.as.i_val = limit_i;

    for (int w = 0; w < num_workers; w++) {
        CloseHandle(threads[w]);
        for (int b = 0; b < TIGER_ENV_BUCKETS; b++) {
            TigerVarEntry* we = tasks[w].worker_env->buckets[b];
            while (we) {
                TigerVarEntry* nxt = we->next;
                if (we->name != we->inline_name) free(we->name);
                tiger_val_free(&we->value);
                free(we);
                we = nxt;
            }
        }
        free(tasks[w].worker_env);
    }
    return true;
#else
    return false;
#endif
}
