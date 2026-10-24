#ifndef TIGER_H
#define TIGER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "tiger_arena.h"
#include "tiger_simd.h"

#ifdef _WIN32
#include <windows.h>
#ifndef strtoll
#define strtoll _strtoi64
#endif
#else
#include <unistd.h>
#endif

/* Operaciones atómicas nativas x86_64 de 1 ciclo CPU sin bloqueos */
static inline int64_t tiger_atomic_add_i64(volatile int64_t* target, int64_t val) {
    __asm__ __volatile__ (
        "lock xaddq %0, %1"
        : "=r" (val), "+m" (*target)
        : "0" (val)
        : "memory"
    );
    return val;
}

static inline int32_t tiger_atomic_add_i32(volatile int32_t* target, int32_t val) {
    __asm__ __volatile__ (
        "lock xaddl %0, %1"
        : "=r" (val), "+m" (*target)
        : "0" (val)
        : "memory"
    );
    return val;
}

#define TIGER_VERSION "10.1.0-NATIVE-C"

/* ========================================================================= */
/* 1. TIPOS DE VALORES TIGER (Tagged Union)                                   */
/* ========================================================================= */

typedef enum {
    VAL_NIL = 0,
    VAL_INT,        /* ENT / INT (64-bit integer) */
    VAL_FLOAT,      /* FLOT / FLOAT / DEC / REAL (double) */
    VAL_STR,        /* CAD / STR / TEXTO / STRING */
    VAL_BOOL,       /* BOOL / BOOLEANO */
    VAL_PTR,        /* PUNT / PUNTC / POINTC (raw C pointer) */
    VAL_ARRAY,      /* [ elem1, elem2 ] */
    VAL_DICT,       /* { "clave": valor } */
    VAL_INSTANCE,   /* Instancia de CLASE Tiger */
    VAL_CFUNC       /* Puntero a función C nativa */
} TigerValType;

struct TigerValue;
struct TigerArray;
struct TigerDict;
struct TigerInstance;

typedef struct TigerValue (*TigerCFuncPtr)(int argc, struct TigerValue* argv);

typedef struct TigerValue {
    TigerValType type;
    union {
        int64_t i_val;
        double  f_val;
        char*   s_val;
        bool    b_val;
        void*   ptr_val;
        struct TigerArray*    arr_val;
        struct TigerDict*     dict_val;
        struct TigerInstance* inst_val;
        TigerCFuncPtr         cfunc_val;
    } as;
} TigerValue;

/* Arreglos dinámicos */
typedef struct TigerArray {
    TigerValue* items;
    size_t count;
    size_t capacity;
    int ref_count;
} TigerArray;

/* Pares clave-valor para diccionarios */
typedef struct TigerDictEntry {
    char* key;
    uint64_t hash;
    TigerValue value;
    struct TigerDictEntry* next;
} TigerDictEntry;

/* Diccionarios (Hash Table simple y rápida) */
#define TIGER_DICT_BUCKETS 64
typedef struct TigerDict {
    TigerDictEntry* buckets[TIGER_DICT_BUCKETS];
    size_t count;
    int ref_count;
} TigerDict;

/* Instancias de clase Tiger */
typedef struct TigerInstance {
    char* class_name;
    TigerDict* fields;
    int ref_count;
} TigerInstance;

/* Constructores de valores */
static inline TigerValue tiger_val_nil(void) {
    TigerValue v; v.type = VAL_NIL; v.as.ptr_val = NULL; return v;
}
static inline TigerValue tiger_val_int(int64_t val) {
    TigerValue v; v.type = VAL_INT; v.as.i_val = val; return v;
}
static inline TigerValue tiger_val_float(double val) {
    TigerValue v; v.type = VAL_FLOAT; v.as.f_val = val; return v;
}
static inline TigerValue tiger_val_bool(bool val) {
    TigerValue v; v.type = VAL_BOOL; v.as.b_val = val; return v;
}
static inline TigerValue tiger_val_ptr(void* ptr) {
    TigerValue v; v.type = VAL_PTR; v.as.ptr_val = ptr; return v;
}
static inline TigerValue tiger_val_cfunc(TigerCFuncPtr fn) {
    TigerValue v; v.type = VAL_CFUNC; v.as.cfunc_val = fn; return v;
}

TigerValue tiger_val_str(const char* s);
TigerValue tiger_val_str_take(char* s);
TigerValue tiger_val_array_new(void);
TigerValue tiger_val_dict_new(void);
TigerValue tiger_val_instance_new(const char* class_name);
void tiger_array_push(TigerArray* arr, TigerValue val);
void tiger_dict_set(TigerDict* dict, const char* key, TigerValue val);
bool tiger_dict_get(TigerDict* dict, const char* key, TigerValue* out_val);

void tiger_val_free(TigerValue* v);
TigerValue tiger_val_copy(TigerValue v);
bool tiger_val_is_truthy(TigerValue v);
void tiger_val_print(TigerValue v, bool newline);
char* tiger_val_to_string(TigerValue v);
bool tiger_val_equals(TigerValue a, TigerValue b);

/* ========================================================================= */
/* 2. TABLA DE SÍMBOLOS Y ENTORNO (Scope de Silicio con Hash Interning)      */
/* ========================================================================= */

/* Función de Hash FNV-1a de 64 bits ultra-rápida (1 ciclo CPU por byte) */
static inline uint64_t tiger_hash64(const char* s) {
    if (!s) return 0;
    uint64_t h = 14695981039346656037ULL;
    while (*s) {
        h ^= (unsigned char)(*s++);
        h *= 1099511628211ULL;
    }
    return h;
}

typedef struct TigerVarEntry {
    char* name;
    uint64_t hash;
    TigerValue value;
    bool is_const;
    TigerValType type_constraint; /* VAL_NIL si es dinámico */
    struct TigerVarEntry* next;
} TigerVarEntry;

#define TIGER_ENV_BUCKETS 256
#define TIGER_ENV_MASK (TIGER_ENV_BUCKETS - 1)

typedef struct TigerEnv {
    TigerVarEntry* buckets[TIGER_ENV_BUCKETS];
    struct TigerEnv* parent;
} TigerEnv;

TigerEnv* tiger_env_new(TigerEnv* parent);
void tiger_env_free(TigerEnv* env);
bool tiger_env_set(TigerEnv* env, const char* name, TigerValue val, bool is_const, TigerValType type_req);
bool tiger_env_set_fast(TigerEnv* env, const char* name, uint64_t hash, TigerValue val, bool is_const, TigerValType type_req);
bool tiger_env_assign(TigerEnv* env, const char* name, TigerValue val);
bool tiger_env_assign_fast(TigerEnv* env, const char* name, uint64_t hash, TigerValue val);
bool tiger_env_get(TigerEnv* env, const char* name, TigerValue* out_val);
bool tiger_env_get_fast(TigerEnv* env, const char* name, uint64_t hash, TigerValue* out_val);

/* ========================================================================= */
/* 3. TOKENS Y LEXER BILINGÜE                                                */
/* ========================================================================= */

typedef enum {
    TOK_EOF = 0,
    TOK_ERROR,

    /* Literales */
    TOK_INT,
    TOK_FLOAT,
    TOK_STRING,
    TOK_IDENT,
    TOK_BOOL_TRUE,
    TOK_BOOL_FALSE,

    /* Palabras Clave Bilingües (Español / English) */
    TOK_VAR,        /* VAR, VARIABLE, LET */
    TOK_CONST,      /* CONST, CONSTANTE, CONSTANT */
    TOK_FN,         /* FN, FUNCTION, FUNC, DEF, FUNCION, FUNCIÓN */
    TOK_COMANDO,    /* COMANDO, COMMAND */
    TOK_SI,         /* SI, IF */
    TOK_SINO,       /* SINO, ELSE */
    TOK_MIENTRAS,   /* MIENTRAS, WHILE */
    TOK_CICLO,      /* CICLO, LOOP, FOR, PARA */
    TOK_ROMPER,     /* ROMPER, BREAK */
    TOK_CONTINUAR,  /* CONTINUAR, CONTINUE */
    TOK_RETORNAR,   /* RETORNAR, RETURN, REGRESAR */
    TOK_IMPRIMIR,   /* IMPRIMIR, PRINT, MOSTRAR, ECHO */
    TOK_ENTRADA,    /* ENTRADA, INPUT, LEER */
    TOK_USAR,       /* USAR, USE, IMPORT, IMPORTAR */
    TOK_CLASE,      /* CLASE, CLASS, OBJETO, OBJECT */
    TOK_MOLDE,      /* MOLDE, MOLD */
    TOK_CRE,        /* CRE, CAMPO, FIELD, PROP, PROPERTY, PROPIEDAD, ATTR */
    TOK_METODO,     /* METODO, METHOD */
    TOK_TIPO,       /* TIPO, TYPE */
    TOK_MACRO,      /* MACRO */
    TOK_BCI,        /* BCI, BRAIN, INTERFAZ */
    TOK_ASISTENTE,  /* ASISTENTE, ASSISTANT, ASISSTANT, IA, AI */
    TOK_INI,        /* INI, INICIO, BEGIN, START */
    TOK_FIN,        /* FIN, END */
    TOK_NIL,        /* NULO, NIL, NULL */

    /* Tipos bilingües */
    TOK_KW_ENT,     /* ENT, INT */
    TOK_KW_FLOT,    /* FLOT, FLOAT, DEC, REAL */
    TOK_KW_CAD,     /* CAD, STR, TEXTO, STRING */
    TOK_KW_BOOL,    /* BOOL, BOOLEANO */
    TOK_KW_PUNT,    /* PUNT, PUNTC, POINTC */

    /* Operadores y Puntuación */
    TOK_DOT,        /* . (Terminador de sentencia) */
    TOK_PIPE,       /* | (Separador de argumentos fluido) */
    TOK_ARROW_L,    /* <- (Asignación) */
    TOK_ARROW_R,    /* -> */
    TOK_ASSIGN,     /* = */
    TOK_PLUS_ASSIGN,/* += */
    TOK_MINUS_ASSIGN,/* -= */
    TOK_STAR_ASSIGN,/* *= */
    TOK_SLASH_ASSIGN,/* /= */

    TOK_PLUS,       /* + */
    TOK_MINUS,      /* - */
    TOK_STAR,       /* * */
    TOK_SLASH,      /* / */
    TOK_PERCENT,    /* % */

    TOK_EQ,         /* == o === */
    TOK_NEQ,        /* != */
    TOK_LT,         /* < */
    TOK_LE,         /* <= */
    TOK_GT,         /* > */
    TOK_GE,         /* >= */

    TOK_AND,        /* && o Y o AND */
    TOK_OR,         /* || o O o OR */
    TOK_NOT,        /* ! o NO o NOT */

    TOK_LPAREN,     /* ( */
    TOK_RPAREN,     /* ) */
    TOK_LBRACE,     /* { */
    TOK_RBRACE,     /* } */
    TOK_LBRACKET,   /* [ */
    TOK_RBRACKET,   /* ] */
    TOK_COMMA,      /* , */
    TOK_COLON,      /* : */
    TOK_SEMICOLON,  /* ; */

    /* Bloques de Metalenguaje Políglota */
    TOK_POLYGLOT_BLOCK /* Contenido de TIPO PY / TIPO C / TIPO JS { ... } */
} TigerTokenType;

typedef struct {
    TigerTokenType type;
    char* text;
    size_t length;
    size_t offset;
    int line;
    int column;
    union {
        int64_t i_val;
        double  f_val;
    };
} TigerToken;

/* ========================================================================= */
/* 4. SOPORTE DE CODIFICACIONES RESILIENTE (UTF-8, UTF-16, ANSI)              */
/* ========================================================================= */

char* tiger_read_file_resilient(const char* filepath, size_t* out_size);

#endif /* TIGER_H */
