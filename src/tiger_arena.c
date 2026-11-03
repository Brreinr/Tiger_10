#include "tiger_arena.h"
#include "tiger.h"
#include <stdlib.h>
#include <string.h>

static TigerArena* g_global_arena = NULL;

TigerArena* tiger_arena_new(size_t capacity) {
    if (capacity == 0) capacity = TIGER_ARENA_DEFAULT_SIZE;

    TigerArena* arena = (TigerArena*)malloc(sizeof(TigerArena));
    if (!arena) return NULL;

#ifdef _WIN32
    arena->buffer = (uint8_t*)VirtualAlloc(NULL, capacity, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    arena->buffer = (uint8_t*)malloc(capacity);
#endif

    if (!arena->buffer) {
        free(arena);
        return NULL;
    }

    arena->capacity = capacity;
    arena->offset = 0;
    arena->next = NULL;
    return arena;
}

void* tiger_arena_alloc(TigerArena* arena, size_t size) {
    if (!arena || !arena->buffer || size == 0) return NULL;

    /* Alinear tamaño a TIGER_ARENA_ALIGNMENT (32 bytes para AVX2) */
    size_t aligned_size = (size + (TIGER_ARENA_ALIGNMENT - 1)) & ~(size_t)(TIGER_ARENA_ALIGNMENT - 1);

    /* Incremento atómico bump-pointer de 1 ciclo CPU libre de bloqueos */
    int64_t old_offset = tiger_atomic_add_i64(&arena->offset, (int64_t)aligned_size);

    if ((size_t)old_offset + aligned_size <= arena->capacity) {
        return (void*)(arena->buffer + old_offset);
    }

    return malloc(aligned_size);
}

char* tiger_arena_strdup(TigerArena* arena, const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char* copy = (char*)tiger_arena_alloc(arena, len + 1);
    if (copy) {
        memcpy(copy, s, len);
        copy[len] = '\0';
    }
    return copy;
}

void tiger_arena_reset(TigerArena* arena) {
    if (arena) {
        arena->offset = 0;
    }
}

void tiger_arena_free(TigerArena* arena) {
    if (!arena) return;
    if (arena->buffer) {
#ifdef _WIN32
        VirtualFree(arena->buffer, 0, MEM_RELEASE);
#else
        free(arena->buffer);
#endif
        arena->buffer = NULL;
    }
    free(arena);
}

TigerArena* tiger_arena_global(void) {
    if (!g_global_arena) {
        g_global_arena = tiger_arena_new(TIGER_ARENA_DEFAULT_SIZE);
    }
    return g_global_arena;
}

size_t tiger_arena_get_allocated(void) {
    if (!g_global_arena) return 0;
    int64_t off = g_global_arena->offset;
    return off > 0 ? (size_t)off : 0;
}

/* ========================================================================= */
/* RECOLECTOR DE BASURA CON TEMPORIZADOR EN SEGUNDO PLANO (TIMED GC)         */
/* ========================================================================= */

static TigerGCStats g_gc_stats = {0, 0, 1000, false};
#ifdef _WIN32
static HANDLE g_gc_thread = NULL;
static HANDLE g_gc_stop_event = NULL;
static volatile LONG g_gc_running = 0;

static DWORD WINAPI tiger_gc_worker_proc(LPVOID lpParam) {
    (void)lpParam;
    while (g_gc_running) {
        DWORD wait_res = WaitForSingleObject(g_gc_stop_event, g_gc_stats.interval_ms);
        if (wait_res == WAIT_OBJECT_0) {
            break; /* Señal de detención recibida */
        }
        if (!g_gc_running) break;

        /* Ejecutar ciclo de recolección en segundo plano */
        tiger_gc_collect();
    }
    return 0;
}
#endif

size_t tiger_gc_collect(void) {
    size_t reclaimed = 0;

#ifdef _WIN32
    /* 1. Trim del Working Set en segundo plano: devuelve páginas de RAM física no usadas */
    HANDLE hProcess = GetCurrentProcess();
    if (hProcess) {
        SetProcessWorkingSetSize(hProcess, (SIZE_T)-1, (SIZE_T)-1);
    }
#endif

    /* 2. Compactación de arena si ha alcanzado un umbral significativo */
    if (g_global_arena && g_global_arena->offset > (int64_t)(8 * 1024 * 1024)) {
        size_t old_bytes = (size_t)g_global_arena->offset;
        tiger_arena_reset(g_global_arena);
        reclaimed += old_bytes;
    }

    g_gc_stats.total_collections++;
    g_gc_stats.total_bytes_reclaimed += reclaimed;
    return reclaimed;
}

void tiger_gc_start(uint32_t interval_ms) {
    if (interval_ms < 50) interval_ms = 50; /* Mínimo 50 ms para no saturar CPU */
    g_gc_stats.interval_ms = interval_ms;

#ifdef _WIN32
    if (InterlockedCompareExchange(&g_gc_running, 1, 0) == 0) {
        g_gc_stats.is_running = true;
        g_gc_stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);
        g_gc_thread = CreateThread(NULL, 0, tiger_gc_worker_proc, NULL, 0, NULL);
        if (g_gc_thread) {
            SetThreadPriority(g_gc_thread, THREAD_PRIORITY_BELOW_NORMAL);
        }
    }
#else
    g_gc_stats.is_running = true;
#endif
}

void tiger_gc_stop(void) {
#ifdef _WIN32
    if (InterlockedCompareExchange(&g_gc_running, 0, 1) == 1) {
        g_gc_stats.is_running = false;
        if (g_gc_stop_event) {
            SetEvent(g_gc_stop_event);
        }
        if (g_gc_thread) {
            WaitForSingleObject(g_gc_thread, 1000);
            CloseHandle(g_gc_thread);
            g_gc_thread = NULL;
        }
        if (g_gc_stop_event) {
            CloseHandle(g_gc_stop_event);
            g_gc_stop_event = NULL;
        }
    }
#else
    g_gc_stats.is_running = false;
#endif
}

TigerGCStats tiger_gc_get_stats(void) {
    return g_gc_stats;
}
