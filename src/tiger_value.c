#include "tiger.h"

TigerValue tiger_val_str(const char* s) {
    TigerValue v;
    v.type = VAL_STR;
    if (!s) {
        v.as.s_val = (char*)calloc(1, 1);
    } else {
        size_t len = strlen(s);
        v.as.s_val = (char*)malloc(len + 1);
        memcpy(v.as.s_val, s, len + 1);
    }
    return v;
}

TigerValue tiger_val_str_take(char* s) {
    TigerValue v;
    v.type = VAL_STR;
    v.as.s_val = s ? s : (char*)calloc(1, 1);
    return v;
}

TigerValue tiger_val_array_new(void) {
    TigerValue v;
    v.type = VAL_ARRAY;
    TigerArray* arr = (TigerArray*)malloc(sizeof(TigerArray));
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
    arr->ref_count = 1;
    v.as.arr_val = arr;
    return v;
}

void tiger_array_reserve(TigerArray* arr, size_t capacity) {
    if (!arr || capacity <= arr->capacity) return;
    arr->items = (TigerValue*)realloc(arr->items, capacity * sizeof(TigerValue));
    arr->capacity = capacity;
}

void tiger_array_push(TigerArray* arr, TigerValue val) {
    if (arr->count >= arr->capacity) {
        size_t new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
        arr->items = (TigerValue*)realloc(arr->items, new_cap * sizeof(TigerValue));
        arr->capacity = new_cap;
    }
    arr->items[arr->count++] = tiger_val_copy(val);
}

TigerValue tiger_val_dict_new(void) {
    TigerValue v;
    v.type = VAL_DICT;
    TigerDict* dict = (TigerDict*)malloc(sizeof(TigerDict));
    for (int i = 0; i < TIGER_DICT_BUCKETS; i++) dict->buckets[i] = NULL;
    dict->count = 0;
    dict->ref_count = 1;
    v.as.dict_val = dict;
    return v;
}

void tiger_dict_set(TigerDict* dict, const char* key, TigerValue val) {
    uint64_t h = tiger_hash64(key);
    unsigned int idx = (unsigned int)(h & (TIGER_DICT_BUCKETS - 1));
    TigerDictEntry* entry = dict->buckets[idx];
    while (entry) {
        if (entry->hash == h && strcmp(entry->key, key) == 0) {
            tiger_val_free(&entry->value);
            entry->value = tiger_val_copy(val);
            return;
        }
        entry = entry->next;
    }
    TigerDictEntry* new_entry = (TigerDictEntry*)malloc(sizeof(TigerDictEntry));
    size_t klen = strlen(key);
    new_entry->key = (char*)malloc(klen + 1);
    memcpy(new_entry->key, key, klen + 1);
    new_entry->hash = h;
    new_entry->value = tiger_val_copy(val);
    new_entry->next = dict->buckets[idx];
    dict->buckets[idx] = new_entry;
    dict->count++;
}

bool tiger_dict_get(TigerDict* dict, const char* key, TigerValue* out_val) {
    uint64_t h = tiger_hash64(key);
    unsigned int idx = (unsigned int)(h & (TIGER_DICT_BUCKETS - 1));
    TigerDictEntry* entry = dict->buckets[idx];
    while (entry) {
        if (entry->hash == h && strcmp(entry->key, key) == 0) {
            if (out_val) *out_val = tiger_val_copy(entry->value);
            return true;
        }
        entry = entry->next;
    }
    return false;
}

TigerValue tiger_val_instance_new(const char* class_name) {
    TigerValue v;
    v.type = VAL_INSTANCE;
    TigerInstance* inst = (TigerInstance*)malloc(sizeof(TigerInstance));
    size_t len = strlen(class_name);
    inst->class_name = (char*)malloc(len + 1);
    memcpy(inst->class_name, class_name, len + 1);

    TigerValue d = tiger_val_dict_new();
    inst->fields = d.as.dict_val;
    inst->ref_count = 1;
    v.as.inst_val = inst;
    return v;
}

void tiger_val_free(TigerValue* v) {
    if (!v) return;
    switch (v->type) {
        case VAL_STR:
            if (v->as.s_val) {
                free(v->as.s_val);
                v->as.s_val = NULL;
            }
            break;
        case VAL_ARRAY:
            if (v->as.arr_val && tiger_rc_dec(&v->as.arr_val->ref_count) <= 0) {
                for (size_t i = 0; i < v->as.arr_val->count; i++) {
                    tiger_val_free(&v->as.arr_val->items[i]);
                }
                free(v->as.arr_val->items);
                free(v->as.arr_val);
                v->as.arr_val = NULL;
            }
            break;
        case VAL_DICT:
            if (v->as.dict_val && tiger_rc_dec(&v->as.dict_val->ref_count) <= 0) {
                for (int i = 0; i < TIGER_DICT_BUCKETS; i++) {
                    TigerDictEntry* e = v->as.dict_val->buckets[i];
                    while (e) {
                        TigerDictEntry* next = e->next;
                        free(e->key);
                        tiger_val_free(&e->value);
                        free(e);
                        e = next;
                    }
                }
                free(v->as.dict_val);
                v->as.dict_val = NULL;
            }
            break;
        case VAL_INSTANCE:
            if (v->as.inst_val && tiger_rc_dec(&v->as.inst_val->ref_count) <= 0) {
                free(v->as.inst_val->class_name);
                TigerValue d; d.type = VAL_DICT; d.as.dict_val = v->as.inst_val->fields;
                tiger_val_free(&d);
                free(v->as.inst_val);
                v->as.inst_val = NULL;
            }
            break;
        default:
            break;
    }
    v->type = VAL_NIL;
}

TigerValue tiger_val_copy(TigerValue v) {
    TigerValue copy = v;
    switch (v.type) {
        case VAL_STR:
            copy = tiger_val_str(v.as.s_val);
            break;
        case VAL_ARRAY:
            if (copy.as.arr_val) tiger_rc_inc(&copy.as.arr_val->ref_count);
            break;
        case VAL_DICT:
            if (copy.as.dict_val) tiger_rc_inc(&copy.as.dict_val->ref_count);
            break;
        case VAL_INSTANCE:
            if (copy.as.inst_val) tiger_rc_inc(&copy.as.inst_val->ref_count);
            break;
        default:
            break;
    }
    return copy;
}

bool tiger_val_is_truthy(TigerValue v) {
    switch (v.type) {
        case VAL_NIL: return false;
        case VAL_BOOL: return v.as.b_val;
        case VAL_INT: return v.as.i_val != 0;
        case VAL_FLOAT: return v.as.f_val != 0.0;
        case VAL_STR: return v.as.s_val && v.as.s_val[0] != '\0';
        case VAL_PTR: return v.as.ptr_val != NULL;
        case VAL_ARRAY: return v.as.arr_val && v.as.arr_val->count > 0;
        case VAL_DICT: return v.as.dict_val && v.as.dict_val->count > 0;
        default: return true;
    }
}

/* ========================================================================= */
/* BUFFER DE SALIDA ULTRA-RÁPIDO EN SILICIO (ZERO-ALLOC I/O)                 */
/* ========================================================================= */

#define TIGER_STDOUT_BUF_SIZE (64 * 1024)
static char g_fast_stdout_buf[TIGER_STDOUT_BUF_SIZE];
static size_t g_fast_stdout_len = 0;

void tiger_io_flush_fast(void) {
    if (g_fast_stdout_len > 0) {
#ifdef _WIN32
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut && hOut != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(hOut, g_fast_stdout_buf, (DWORD)g_fast_stdout_len, &written, NULL);
        } else {
            fwrite(g_fast_stdout_buf, 1, g_fast_stdout_len, stdout);
        }
#else
        fwrite(g_fast_stdout_buf, 1, g_fast_stdout_len, stdout);
#endif
        g_fast_stdout_len = 0;
    }
}

static inline void tiger_io_write_fast(const char* s, size_t len) {
    if (!s || len == 0) return;
    if (g_fast_stdout_len + len >= TIGER_STDOUT_BUF_SIZE) {
        tiger_io_flush_fast();
        if (len >= TIGER_STDOUT_BUF_SIZE) {
#ifdef _WIN32
            HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
            if (hOut && hOut != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                WriteFile(hOut, s, (DWORD)len, &written, NULL);
                return;
            }
#endif
            fwrite(s, 1, len, stdout);
            return;
        }
    }
    memcpy(g_fast_stdout_buf + g_fast_stdout_len, s, len);
    g_fast_stdout_len += len;
}

size_t tiger_val_format_buf(TigerValue v, char* dst, size_t max_size) {
    if (!dst || max_size == 0) return 0;
    switch (v.type) {
        case VAL_NIL:
            return snprintf(dst, max_size, "NULO");
        case VAL_BOOL:
            return snprintf(dst, max_size, "%s", v.as.b_val ? "VERDADERO" : "FALSO");
        case VAL_INT:
            return snprintf(dst, max_size, "%lld", (long long)v.as.i_val);
        case VAL_FLOAT:
            return snprintf(dst, max_size, "%g", v.as.f_val);
        case VAL_STR:
            if (!v.as.s_val) return 0;
            return snprintf(dst, max_size, "%s", v.as.s_val);
        case VAL_PTR:
            return snprintf(dst, max_size, "<puntero %p>", v.as.ptr_val);
        case VAL_ARRAY:
            return snprintf(dst, max_size, "<arreglo>");
        case VAL_DICT:
            return snprintf(dst, max_size, "<diccionario>");
        case VAL_INSTANCE:
            return snprintf(dst, max_size, "<instancia %s>", v.as.inst_val ? v.as.inst_val->class_name : "");
        case VAL_CFUNC:
            return snprintf(dst, max_size, "<funcion_c_nativa>");
        default:
            dst[0] = '\0';
            return 0;
    }
}

char* tiger_val_to_string(TigerValue v) {
    char buf[128];
    switch (v.type) {
        case VAL_NIL: return strdup("NULO");
        case VAL_BOOL: return strdup(v.as.b_val ? "VERDADERO" : "FALSO");
        case VAL_INT:
            snprintf(buf, sizeof(buf), "%lld", (long long)v.as.i_val);
            return strdup(buf);
        case VAL_FLOAT:
            snprintf(buf, sizeof(buf), "%g", v.as.f_val);
            return strdup(buf);
        case VAL_STR:
            return strdup(v.as.s_val ? v.as.s_val : "");
        case VAL_PTR:
            snprintf(buf, sizeof(buf), "<puntero %p>", v.as.ptr_val);
            return strdup(buf);
        case VAL_ARRAY:
            return strdup("<arreglo>");
        case VAL_DICT:
            return strdup("<diccionario>");
        case VAL_INSTANCE: {
            snprintf(buf, sizeof(buf), "<instancia %s>", v.as.inst_val->class_name);
            return strdup(buf);
        }
        case VAL_CFUNC:
            return strdup("<funcion_c_nativa>");
        default:
            return strdup("");
    }
}

static volatile long g_print_lock = 0;

void tiger_val_print(TigerValue v, bool newline) {
    bool locked = g_tiger_parallel_active;
    if (locked) {
        while (__atomic_exchange_n(&g_print_lock, 1, __ATOMIC_ACQUIRE)) { }
    }
    if (v.type == VAL_STR) {
        if (v.as.s_val && v.as.s_val[0]) {
            tiger_io_write_fast(v.as.s_val, strlen(v.as.s_val));
        }
    } else {
        char fast_buf[256];
        size_t len = tiger_val_format_buf(v, fast_buf, sizeof(fast_buf));
        if (len >= sizeof(fast_buf)) len = sizeof(fast_buf) - 1;
        tiger_io_write_fast(fast_buf, len);
    }
    if (newline) {
        tiger_io_write_fast("\n", 1);
    }
    if (locked) {
        __atomic_store_n(&g_print_lock, 0, __ATOMIC_RELEASE);
    }
}

bool tiger_val_equals(TigerValue a, TigerValue b) {
    if (a.type != b.type) {
        if (a.type == VAL_INT && b.type == VAL_FLOAT) return (double)a.as.i_val == b.as.f_val;
        if (a.type == VAL_FLOAT && b.type == VAL_INT) return a.as.f_val == (double)b.as.i_val;
        return false;
    }
    switch (a.type) {
        case VAL_NIL: return true;
        case VAL_BOOL: return a.as.b_val == b.as.b_val;
        case VAL_INT: return a.as.i_val == b.as.i_val;
        case VAL_FLOAT: return a.as.f_val == b.as.f_val;
        case VAL_STR: return strcmp(a.as.s_val, b.as.s_val) == 0;
        case VAL_PTR: return a.as.ptr_val == b.as.ptr_val;
        default: return false;
    }
}
