#ifndef TIGER_ARENA_H
#define TIGER_ARENA_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* ========================================================================= */
/* ARENA ALLOCATOR LINEAL CERO (Zero-Overhead Linear Arena Allocator)        */
/* Asignaciones en 1 ciclo CPU con bump-pointer atómico sin bloqueos.        */
/* Liberación instantánea O(1) reseteando el puntero en 0 ns.                */
/* Alineado a 32 bytes para compatibilidad nativa con instrucciones AVX2.   */
/* ========================================================================= */

#define TIGER_ARENA_DEFAULT_SIZE (16 * 1024 * 1024) /* 16 MB pre-asignados */
#define TIGER_ARENA_ALIGNMENT 32                    /* Alineación de 256 bits para AVX2 */

typedef struct TigerArena {
    uint8_t* buffer;
    size_t capacity;
    volatile int64_t offset; /* Puntero bump atómico libre de bloqueos */
    struct TigerArena* next;
} TigerArena;

/* Inicializa una nueva arena lineal contigua */
TigerArena* tiger_arena_new(size_t capacity);

/* Asignación ultra-rápida (1 ciclo CPU, alineada a 32 bytes para AVX2) */
void* tiger_arena_alloc(TigerArena* arena, size_t size);

/* Duplica una cadena dentro de la arena */
char* tiger_arena_strdup(TigerArena* arena, const char* s);

/* Resetea la arena a cero sin devolver la memoria al SO (coste cero O(1)) */
void tiger_arena_reset(TigerArena* arena);

/* Libera completamente la memoria física de la arena */
void tiger_arena_free(TigerArena* arena);

/* Arena global por defecto del motor Tiger */
TigerArena* tiger_arena_global(void);

#endif /* TIGER_ARENA_H */
