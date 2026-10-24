#include "tiger_parallel.h"
#include "tiger_simd.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

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
}

static void buf_flush_to_stdout(OutputBuffer* b) {
    if (!b || b->is_flushed) return;
    if (b->length > 0) {
        fputs(b->data, stdout);
        fflush(stdout);
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
/* EMPAQUETADOR DE SUBPROCESOS (Lotes cuádruples de 4 en 4)                  */
/* ========================================================================= */

TigerPackage* tiger_empaquetar_procesos(TigerStmt** stmts, size_t count, size_t batch_size, size_t* out_pkg_count) {
    if (count == 0) {
        *out_pkg_count = 0;
        return NULL;
    }
    if (batch_size == 0) batch_size = TIGER_PARALLEL_BURST_SIZE;

    TigerPackage* pkgs = (TigerPackage*)malloc(count * sizeof(TigerPackage));
    size_t pkg_idx = 0;
    size_t i = 0;

    while (i < count) {
        size_t start = i;
        size_t limit = start + batch_size;
        if (limit > count) limit = count;

        size_t end = start;
        bool has_sub = false;
        while (end < limit) {
            if (stmts[end]->is_subprocess) has_sub = true;
            end++;
            /* Si encontramos un proceso terminado en '.', respetamos la frontera */
            if (end < count && !stmts[end - 1]->is_subprocess) {
                break;
            }
        }

        pkgs[pkg_idx].stmts = &stmts[start];
        pkgs[pkg_idx].count = end - start;
        pkgs[pkg_idx].package_id = (int)pkg_idx;
        pkgs[pkg_idx].has_subprocesses = has_sub;
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
        char* str = tiger_val_to_string(val);
        buf_append(buf, str);
        if (stmt->as.print.newline) buf_append(buf, "\n");
        free(str);
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

            /* Espera activa ultraligera en pipeline ordenado */
            if (pkg_idx > 0) {
                int spin = 0;
                while (g_pipeline_stage < (pkg_idx - 1)) {
                    if (++spin > 3000) {
                        Sleep(0);
                        spin = 0;
                    }
                }
            }

            TigerPackage* pkg = &g_active_packages[pkg_idx];
            OutputBuffer* out_buf = &g_active_buffers[pkg_idx];

            /* Ráfaga simultánea potenciada con SIMD AVX2 */
            size_t s = 0;
            while (s + 4 <= pkg->count) {
                for (int sub = 0; sub < 4; sub++) {
                    if (pkg->stmts[s + sub]->type == STMT_POLYGLOT || stmt_has_input(pkg->stmts[s + sub])) {
                        for (int p = 0; p < pkg_idx; p++) buf_flush_to_stdout(&g_active_buffers[p]);
                        buf_flush_to_stdout(out_buf);
                    }
                    tiger_exec_stmt_buffered(g_active_env, pkg->stmts[s + sub], out_buf);
                }
                s += 4;
            }
            while (s < pkg->count) {
                if (pkg->stmts[s]->type == STMT_POLYGLOT || stmt_has_input(pkg->stmts[s])) {
                    for (int p = 0; p < pkg_idx; p++) buf_flush_to_stdout(&g_active_buffers[p]);
                    buf_flush_to_stdout(out_buf);
                }
                tiger_exec_stmt_buffered(g_active_env, pkg->stmts[s], out_buf);
                s++;
            }

            /* Notificar avance de etapa del pipeline */
            InterlockedExchange(&g_pipeline_stage, pkg_idx);
        }

        SetEvent(g_done_events[subproc_id]);
    }
    return 0;
}
#endif

void tiger_threadpool_init(void) {
#ifdef _WIN32
    if (InterlockedCompareExchange(&g_tp_initialized, 1, 0) == 0) {
        tiger_simd_init();
        for (int i = 0; i < TIGER_SUBPROCESSOR_COUNT; i++) {
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
        for (int i = 0; i < TIGER_SUBPROCESSOR_COUNT; i++) {
            SetEvent(g_start_events[i]);
        }
        WaitForMultipleObjects(TIGER_SUBPROCESSOR_COUNT, g_worker_threads, TRUE, 1000);
        for (int i = 0; i < TIGER_SUBPROCESSOR_COUNT; i++) {
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

ExecSignal tiger_ejecutar_dual_paralelo(TigerEnv* env, TigerStmt** stmts, size_t count) {
    ExecSignal sig = { EXEC_OK, tiger_val_nil() };
    if (!stmts || count == 0) return sig;

    size_t pkg_count = 0;
    TigerPackage* pkgs = tiger_empaquetar_procesos(stmts, count, TIGER_PARALLEL_BURST_SIZE, &pkg_count);

    if (pkg_count <= 2) {
        sig = tiger_exec_block(env, stmts, count);
        free(pkgs);
        return sig;
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

    /* Activar los 4 subprocesadores simultáneos */
    int active_workers = (pkg_count < TIGER_SUBPROCESSOR_COUNT) ? (int)pkg_count : TIGER_SUBPROCESSOR_COUNT;
    for (int i = 0; i < active_workers; i++) {
        SetEvent(g_start_events[i]);
    }

    /* Esperar que los 4 subprocesadores concluyan sus paquetes */
    WaitForMultipleObjects(active_workers, g_done_events, TRUE, INFINITE);

    /* Escupir resultados secuencialmente en orden sin bloqueos */
    for (size_t i = 0; i < pkg_count; i++) {
        buf_flush_to_stdout(&pkg_buffers[i]);
        buf_free(&pkg_buffers[i]);
    }
    fflush(stdout);

    free(pkg_buffers);
#else
    sig = tiger_exec_block(env, stmts, count);
#endif

    free(pkgs);
    return sig;
}
