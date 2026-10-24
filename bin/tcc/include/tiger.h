/**
 * ============================================================================
 * TIGER ENGINE — NATIVE C/C++ INTEROPERABILITY RUNTIME (tiger.h)
 * ============================================================================
 * Permite la interoperabilidad bidireccional entre el lenguaje Tiger y código C/C++.
 * 
 * Sintaxis soportada:
 *   - TIGER->metodo(...)      / TIGRE->metodo(...)
 *   - TIGER->variable         / TIGRE->variable
 *   - TIGER_IMPRIMIR(...)     / TIGER_LOG(...)
 *   - Tiger.metodo(...)       / Tigre.metodo(...)
 * ============================================================================
 */

#ifndef TIGER_H
#define TIGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <math.h>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

/* ----------------------------------------------------------------------------
 * CONSTANTES DE COLOR ANSI (ESTILO TIGER ENGINE)
 * ---------------------------------------------------------------------------- */
#define TIGER_COLOR_ORANGE  "\033[38;2;255;107;0m"
#define TIGER_COLOR_YELLOW  "\033[38;2;255;230;0m"
#define TIGER_COLOR_GREEN   "\033[38;2;45;210;110m"
#define TIGER_COLOR_RED     "\033[38;2;255;65;54m"
#define TIGER_COLOR_CYAN    "\033[38;2;0;200;255m"
#define TIGER_COLOR_BOLD    "\033[1m"
#define TIGER_COLOR_RESET   "\033[0m"

/* ----------------------------------------------------------------------------
 * TIPOS Y ESTRUCTURAS DE DATOS DE TIGER
 * ---------------------------------------------------------------------------- */
typedef long long       tiger_int_t;
typedef double          tiger_float_t;
typedef const char*     tiger_str_t;
typedef int             tiger_bool_t;

#define TIGER_VERDADERO 1
#define TIGER_FALSO     0

typedef enum {
    TIGER_VAL_NULO = 0,
    TIGER_VAL_ENTERO,
    TIGER_VAL_FLOTANTE,
    TIGER_VAL_CADENA,
    TIGER_VAL_BOOLEANO
} TigerTipoDato;

typedef struct {
    char clave[64];
    TigerTipoDato tipo;
    union {
        tiger_int_t   entero;
        tiger_float_t flotante;
        char          cadena[256];
        tiger_bool_t  booleano;
    } valor;
} TigerVariable;

#define TIGER_MAX_VARS 128

/* Tabla estática de variables compartidas */
static TigerVariable _tiger_tabla_vars[TIGER_MAX_VARS];
static int           _tiger_total_vars = 0;

#if defined(_WIN32) || defined(_WIN64)
static LARGE_INTEGER _tiger_timer_freq;
static LARGE_INTEGER _tiger_timer_start;
static int           _tiger_timer_init = 0;
#else
static clock_t       _tiger_timer_start;
static int           _tiger_timer_init = 0;
#endif

/* ----------------------------------------------------------------------------
 * FUNCIONES INTERNAS DEL RUNTIME
 * ---------------------------------------------------------------------------- */

/* Imprimir con formato y etiqueta Tiger */
static void _tiger_imprimir(const char* fmt, ...) {
    va_list args;
    printf("%s%s[Tiger C]:%s ", TIGER_COLOR_ORANGE, TIGER_COLOR_BOLD, TIGER_COLOR_RESET);
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

/* Imprimir en bruto sin prefijo */
static void _tiger_print_raw(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
}

/* Alerta amarilla */
static void _tiger_alerta(const char* msg) {
    printf("%s%s[Alerta Tiger / Warning]:%s %s\n", 
           TIGER_COLOR_YELLOW, TIGER_COLOR_BOLD, TIGER_COLOR_RESET, msg);
    fflush(stdout);
}

/* Error rojo */
static void _tiger_error(const char* msg) {
    printf("%s%s[Error Tiger / Native Error]:%s %s\n", 
           TIGER_COLOR_RED, TIGER_COLOR_BOLD, TIGER_COLOR_RESET, msg);
    fflush(stdout);
}

/* Éxito verde */
static void _tiger_exito(const char* msg) {
    printf("%s%s[Éxito Tiger / Success]:%s %s\n", 
           TIGER_COLOR_GREEN, TIGER_COLOR_BOLD, TIGER_COLOR_RESET, msg);
    fflush(stdout);
}

/* Log categorizado con Tag */
static void _tiger_log(const char* tag, const char* msg) {
    printf("%s[%s]:%s %s\n", TIGER_COLOR_CYAN, tag ? tag : "TIGER", TIGER_COLOR_RESET, msg);
    fflush(stdout);
}

/* ----------------------------------------------------------------------------
 * GESTIÓN DE VARIABLES (SET / GET)
 * ---------------------------------------------------------------------------- */
static int _tiger_buscar_var(const char* clave) {
    if (!clave) return -1;
    for (int i = 0; i < _tiger_total_vars; i++) {
        if (strcmp(_tiger_tabla_vars[i].clave, clave) == 0) {
            return i;
        }
    }
    return -1;
}

static void _tiger_set_int(const char* clave, tiger_int_t val) {
    int idx = _tiger_buscar_var(clave);
    if (idx < 0) {
        if (_tiger_total_vars >= TIGER_MAX_VARS) return;
        idx = _tiger_total_vars++;
        strncpy(_tiger_tabla_vars[idx].clave, clave, 63);
        _tiger_tabla_vars[idx].clave[63] = '\0';
    }
    _tiger_tabla_vars[idx].tipo = TIGER_VAL_ENTERO;
    _tiger_tabla_vars[idx].valor.entero = val;
}

static tiger_int_t _tiger_get_int(const char* clave) {
    int idx = _tiger_buscar_var(clave);
    if (idx >= 0 && _tiger_tabla_vars[idx].tipo == TIGER_VAL_ENTERO) {
        return _tiger_tabla_vars[idx].valor.entero;
    }
    return 0;
}

static void _tiger_set_float(const char* clave, tiger_float_t val) {
    int idx = _tiger_buscar_var(clave);
    if (idx < 0) {
        if (_tiger_total_vars >= TIGER_MAX_VARS) return;
        idx = _tiger_total_vars++;
        strncpy(_tiger_tabla_vars[idx].clave, clave, 63);
        _tiger_tabla_vars[idx].clave[63] = '\0';
    }
    _tiger_tabla_vars[idx].tipo = TIGER_VAL_FLOTANTE;
    _tiger_tabla_vars[idx].valor.flotante = val;
}

static tiger_float_t _tiger_get_float(const char* clave) {
    int idx = _tiger_buscar_var(clave);
    if (idx >= 0 && _tiger_tabla_vars[idx].tipo == TIGER_VAL_FLOTANTE) {
        return _tiger_tabla_vars[idx].valor.flotante;
    }
    return 0.0;
}

static void _tiger_set_str(const char* clave, const char* val) {
    int idx = _tiger_buscar_var(clave);
    if (idx < 0) {
        if (_tiger_total_vars >= TIGER_MAX_VARS) return;
        idx = _tiger_total_vars++;
        strncpy(_tiger_tabla_vars[idx].clave, clave, 63);
        _tiger_tabla_vars[idx].clave[63] = '\0';
    }
    _tiger_tabla_vars[idx].tipo = TIGER_VAL_CADENA;
    strncpy(_tiger_tabla_vars[idx].valor.cadena, val ? val : "", 255);
    _tiger_tabla_vars[idx].valor.cadena[255] = '\0';
}

static const char* _tiger_get_str(const char* clave) {
    int idx = _tiger_buscar_var(clave);
    if (idx >= 0 && _tiger_tabla_vars[idx].tipo == TIGER_VAL_CADENA) {
        return _tiger_tabla_vars[idx].valor.cadena;
    }
    return "";
}

static void _tiger_set_bool(const char* clave, tiger_bool_t val) {
    int idx = _tiger_buscar_var(clave);
    if (idx < 0) {
        if (_tiger_total_vars >= TIGER_MAX_VARS) return;
        idx = _tiger_total_vars++;
        strncpy(_tiger_tabla_vars[idx].clave, clave, 63);
        _tiger_tabla_vars[idx].clave[63] = '\0';
    }
    _tiger_tabla_vars[idx].tipo = TIGER_VAL_BOOLEANO;
    _tiger_tabla_vars[idx].valor.booleano = val ? 1 : 0;
}

static tiger_bool_t _tiger_get_bool(const char* clave) {
    int idx = _tiger_buscar_var(clave);
    if (idx >= 0 && _tiger_tabla_vars[idx].tipo == TIGER_VAL_BOOLEANO) {
        return _tiger_tabla_vars[idx].valor.booleano;
    }
    return 0;
}

static int _tiger_existe(const char* clave) {
    return (_tiger_buscar_var(clave) >= 0) ? 1 : 0;
}

static void _tiger_listar_variables(void) {
    printf("%s--- [Variables Activas en Runtime Tiger C (%d)] ---%s\n", 
           TIGER_COLOR_ORANGE, _tiger_total_vars, TIGER_COLOR_RESET);
    for (int i = 0; i < _tiger_total_vars; i++) {
        TigerVariable* v = &_tiger_tabla_vars[i];
        switch (v->tipo) {
            case TIGER_VAL_ENTERO:
                printf("  * %-20s (ENTERO)   : %lld\n", v->clave, v->valor.entero);
                break;
            case TIGER_VAL_FLOTANTE:
                printf("  * %-20s (FLOTANTE) : %.6f\n", v->clave, v->valor.flotante);
                break;
            case TIGER_VAL_CADENA:
                printf("  * %-20s (CADENA)   : \"%s\"\n", v->clave, v->valor.cadena);
                break;
            case TIGER_VAL_BOOLEANO:
                printf("  * %-20s (BOOLEANO) : %s\n", v->clave, v->valor.booleano ? "VERDADERO" : "FALSO");
                break;
            default:
                printf("  * %-20s (DESCONOCIDO)\n", v->clave);
                break;
        }
    }
    printf("%s---------------------------------------------------%s\n", 
           TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
}

/* ----------------------------------------------------------------------------
 * CRONÓMETRO DE ALTA PRECISIÓN (MICROSEGUNDOS)
 * ---------------------------------------------------------------------------- */
static void _tiger_cronometro_iniciar(void) {
#if defined(_WIN32) || defined(_WIN64)
    QueryPerformanceFrequency(&_tiger_timer_freq);
    QueryPerformanceCounter(&_tiger_timer_start);
#else
    _tiger_timer_start = clock();
#endif
    _tiger_timer_init = 1;
}

static double _tiger_cronometro_ms(void) {
    if (!_tiger_timer_init) return 0.0;
#if defined(_WIN32) || defined(_WIN64)
    LARGE_INTEGER end;
    QueryPerformanceCounter(&end);
    double elapsed = (double)(end.QuadPart - _tiger_timer_start.QuadPart) * 1000.0 / (double)_tiger_timer_freq.QuadPart;
    return elapsed;
#else
    clock_t end = clock();
    return ((double)(end - _tiger_timer_start) * 1000.0) / CLOCKS_PER_SEC;
#endif
}

/* ----------------------------------------------------------------------------
 * FUNCIONES MATEMÁTICAS Y ALGORITMOS NATIVOS
 * ---------------------------------------------------------------------------- */
static double _tiger_clamp(double val, double min, double max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

static double _tiger_sigmoid(double x) {
    return 1.0 / (1.0 + exp(-x));
}

/* Hash rápido DJB2 para cadenas y claves */
static unsigned long _tiger_hash(const char* str) {
    if (!str) return 0;
    unsigned long hash = 5381;
    int c;
    while ((c = (unsigned char)*str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}

static int _tiger_random_range(int min, int max) {
    if (min >= max) return min;
    return min + rand() % (max - min + 1);
}

/* Dump de memoria en hexadecimal */
static void _tiger_memoria_dump(const void* ptr, int bytes) {
    const unsigned char* p = (const unsigned char*)ptr;
    printf("%s[Memoria 0x%p (%d bytes)]:%s\n", TIGER_COLOR_CYAN, ptr, bytes, TIGER_COLOR_RESET);
    for (int i = 0; i < bytes; i++) {
        if (i % 16 == 0) printf("  %04X: ", i);
        printf("%02X ", p[i]);
        if ((i + 1) % 16 == 0 || i == bytes - 1) {
            int espacios = (15 - (i % 16)) * 3;
            for (int s = 0; s < espacios; s++) printf(" ");
            printf(" | ");
            int start = i - (i % 16);
            for (int j = start; j <= i; j++) {
                char c = (p[j] >= 32 && p[j] <= 126) ? p[j] : '.';
                printf("%c", c);
            }
            printf("\n");
        }
    }
}

/* ----------------------------------------------------------------------------
 * ESTRUCTURA DE CONTEXTO TIGER (OBJETO Y MÉTODOS)
 * ---------------------------------------------------------------------------- */
typedef struct TigerContext {
    /* Métodos de visualización y consola */
    void (*imprimir)(const char* fmt, ...);
    void (*print)(const char* fmt, ...);
    void (*alerta)(const char* msg);
    void (*error)(const char* msg);
    void (*exito)(const char* msg);
    void (*log)(const char* tag, const char* msg);

    /* Métodos de variables */
    void (*set_int)(const char* clave, tiger_int_t val);
    tiger_int_t (*get_int)(const char* clave);
    void (*set_float)(const char* clave, tiger_float_t val);
    tiger_float_t (*get_float)(const char* clave);
    void (*set_str)(const char* clave, const char* val);
    const char* (*get_str)(const char* clave);
    void (*set_bool)(const char* clave, tiger_bool_t val);
    tiger_bool_t (*get_bool)(const char* clave);
    int (*existe)(const char* clave);
    void (*listar_variables)(void);

    /* Cronómetro y rendimiento */
    void (*cronometro_iniciar)(void);
    double (*cronometro_ms)(void);

    /* Matemáticas y algoritmos */
    double (*clamp)(double val, double min, double max);
    double (*sigmoid)(double x);
    unsigned long (*hash)(const char* str);
    int (*random_range)(int min, int max);
    void (*memoria_dump)(const void* ptr, int bytes);

    /* Metadatos */
    const char* version;
} TigerContext;

/* Instancia singleton del motor Tiger en C */
static TigerContext _tiger_ctx_singleton = {
    _tiger_imprimir,
    _tiger_print_raw,
    _tiger_alerta,
    _tiger_error,
    _tiger_exito,
    _tiger_log,
    _tiger_set_int,
    _tiger_get_int,
    _tiger_set_float,
    _tiger_get_float,
    _tiger_set_str,
    _tiger_get_str,
    _tiger_set_bool,
    _tiger_get_bool,
    _tiger_existe,
    _tiger_listar_variables,
    _tiger_cronometro_iniciar,
    _tiger_cronometro_ms,
    _tiger_clamp,
    _tiger_sigmoid,
    _tiger_hash,
    _tiger_random_range,
    _tiger_memoria_dump,
    "10.0.0"
};

/* Punteros globales para llamadas de sintaxis TIGER->metodo o TIGRE->metodo */
static TigerContext* const TIGER = &_tiger_ctx_singleton;
static TigerContext* const TIGRE = &_tiger_ctx_singleton;

/* Acceso alternativo por punto: Tiger.metodo(...) */
#define Tiger _tiger_ctx_singleton
#define Tigre _tiger_ctx_singleton

/* Macros de conveniencia directa */
#define TIGER_IMPRIMIR(...)     TIGER->imprimir(__VA_ARGS__)
#define TIGER_PRINT(...)        TIGER->print(__VA_ARGS__)
#define TIGER_ALERTA(msg)       TIGER->alerta(msg)
#define TIGER_ERROR(msg)        TIGER->error(msg)
#define TIGER_EXITO(msg)        TIGER->exito(msg)
#define TIGER_LOG(tag, msg)     TIGER->log(tag, msg)
#define TIGER_BENCH_START()     TIGER->cronometro_iniciar()
#define TIGER_BENCH_END()       TIGER->cronometro_ms()

#ifdef __cplusplus
}
#endif

#endif /* TIGER_H */
