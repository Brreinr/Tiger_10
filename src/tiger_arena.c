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
