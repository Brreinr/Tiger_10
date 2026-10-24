#include "tiger.h"

#ifdef _WIN32
static CRITICAL_SECTION g_env_cs;
static bool g_cs_init = false;

static inline void env_lock(void) {
    if (!g_cs_init) {
        InitializeCriticalSection(&g_env_cs);
        g_cs_init = true;
    }
    EnterCriticalSection(&g_env_cs);
}

static inline void env_unlock(void) {
    LeaveCriticalSection(&g_env_cs);
}
#else
static inline void env_lock(void) {}
static inline void env_unlock(void) {}
#endif

TigerEnv* tiger_env_new(TigerEnv* parent) {
    TigerEnv* env = (TigerEnv*)malloc(sizeof(TigerEnv));
    for (int i = 0; i < TIGER_ENV_BUCKETS; i++) env->buckets[i] = NULL;
    env->parent = parent;
    return env;
}

void tiger_env_free(TigerEnv* env) {
    if (!env) return;
    for (int i = 0; i < TIGER_ENV_BUCKETS; i++) {
        TigerVarEntry* e = env->buckets[i];
        while (e) {
            TigerVarEntry* next = e->next;
            free(e->name);
            tiger_val_free(&e->value);
            free(e);
            e = next;
        }
    }
    free(env);
}

bool tiger_env_set_fast(TigerEnv* env, const char* name, uint64_t hash, TigerValue val, bool is_const, TigerValType type_req) {
    if (!env || !name) return false;
    if (hash == 0) hash = tiger_hash64(name);

    /* Validar restricción de tipo si existe */
    if (type_req != VAL_NIL && val.type != type_req) {
        if (type_req == VAL_FLOAT && val.type == VAL_INT) {
            val = tiger_val_float((double)val.as.i_val);
        } else if (type_req == VAL_INT && val.type == VAL_FLOAT) {
            val = tiger_val_int((int64_t)val.as.f_val);
        } else if (type_req == VAL_STR) {
            char* s = tiger_val_to_string(val);
            tiger_val_free(&val);
            val = tiger_val_str_take(s);
        }
    }

    env_lock();
    /* Máscara a nivel de bits de 1 ciclo CPU en lugar de división IDIV lenta */
    unsigned int idx = (unsigned int)(hash & TIGER_ENV_MASK);
    TigerVarEntry* e = env->buckets[idx];
    while (e) {
        /* Comparación de registros de 64 bits ultra-rápida antes de strcmp */
        if (e->hash == hash && strcmp(e->name, name) == 0) {
            if (e->is_const) {
                env_unlock();
                return false; /* No se puede reasignar una constante */
            }
            tiger_val_free(&e->value);
            e->value = tiger_val_copy(val);
            e->type_constraint = type_req;
            env_unlock();
            return true;
        }
        e = e->next;
    }

    TigerVarEntry* new_entry = (TigerVarEntry*)malloc(sizeof(TigerVarEntry));
    size_t len = strlen(name);
    new_entry->name = (char*)malloc(len + 1);
    memcpy(new_entry->name, name, len + 1);
    new_entry->hash = hash;
    new_entry->value = tiger_val_copy(val);
    new_entry->is_const = is_const;
    new_entry->type_constraint = type_req;
    new_entry->next = env->buckets[idx];
    env->buckets[idx] = new_entry;
    env_unlock();
    return true;
}

bool tiger_env_set(TigerEnv* env, const char* name, TigerValue val, bool is_const, TigerValType type_req) {
    return tiger_env_set_fast(env, name, tiger_hash64(name), val, is_const, type_req);
}

bool tiger_env_assign_fast(TigerEnv* env, const char* name, uint64_t hash, TigerValue val) {
    if (!env || !name) return false;
    if (hash == 0) hash = tiger_hash64(name);
    unsigned int idx = (unsigned int)(hash & TIGER_ENV_MASK);

    env_lock();
    TigerEnv* curr = env;
    while (curr) {
        TigerVarEntry* e = curr->buckets[idx];
        while (e) {
            if (e->hash == hash && strcmp(e->name, name) == 0) {
                if (e->is_const) {
                    env_unlock();
                    return false;
                }
                if (e->type_constraint != VAL_NIL && val.type != e->type_constraint) {
                    if (e->type_constraint == VAL_FLOAT && val.type == VAL_INT) {
                        val = tiger_val_float((double)val.as.i_val);
                    } else if (e->type_constraint == VAL_INT && val.type == VAL_FLOAT) {
                        val = tiger_val_int((int64_t)val.as.f_val);
                    } else if (e->type_constraint == VAL_STR) {
                        char* s = tiger_val_to_string(val);
                        tiger_val_free(&val);
                        val = tiger_val_str_take(s);
                    } else {
                        env_unlock();
                        return false;
                    }
                }
                tiger_val_free(&e->value);
                e->value = tiger_val_copy(val);
                env_unlock();
                return true;
            }
            e = e->next;
        }
        curr = curr->parent;
    }
    env_unlock();
    /* Si no existe en ningún scope previo, definir en el scope actual */
    return tiger_env_set_fast(env, name, hash, val, false, VAL_NIL);
}

bool tiger_env_assign(TigerEnv* env, const char* name, TigerValue val) {
    return tiger_env_assign_fast(env, name, tiger_hash64(name), val);
}

bool tiger_env_get_fast(TigerEnv* env, const char* name, uint64_t hash, TigerValue* out_val) {
    if (!env || !name) return false;
    if (hash == 0) hash = tiger_hash64(name);
    unsigned int idx = (unsigned int)(hash & TIGER_ENV_MASK);

    env_lock();
    TigerEnv* curr = env;
    while (curr) {
        TigerVarEntry* e = curr->buckets[idx];
        while (e) {
            if (e->hash == hash && strcmp(e->name, name) == 0) {
                if (out_val) *out_val = tiger_val_copy(e->value);
                env_unlock();
                return true;
            }
            e = e->next;
        }
        curr = curr->parent;
    }
    env_unlock();
    return false;
}

bool tiger_env_get(TigerEnv* env, const char* name, TigerValue* out_val) {
    return tiger_env_get_fast(env, name, tiger_hash64(name), out_val);
}

/* ========================================================================= */
/* API C BILINGÜE Y BIDIRECCIONAL (C <-> TIGER BRIDGE)                       */
/* ========================================================================= */

int64_t tiger_c_get_int(TigerEnv* env, const char* name) {
    TigerValue v;
    if (tiger_env_get(env, name, &v)) {
        int64_t res = (v.type == VAL_INT) ? v.as.i_val : (v.type == VAL_FLOAT ? (int64_t)v.as.f_val : 0);
        tiger_val_free(&v);
        return res;
    }
    return 0;
}

void tiger_c_set_int(TigerEnv* env, const char* name, int64_t val) {
    TigerValue v = tiger_val_int(val);
    tiger_env_assign(env, name, v);
    tiger_val_free(&v);
}

const char* tiger_c_get_str(TigerEnv* env, const char* name) {
    TigerValue v;
    if (tiger_env_get(env, name, &v) && v.type == VAL_STR) {
        /* Nota: Para uso inmediato en C */
        return v.as.s_val;
    }
    return "";
}

void tiger_c_set_str(TigerEnv* env, const char* name, const char* str) {
    TigerValue v = tiger_val_str(str);
    tiger_env_assign(env, name, v);
    tiger_val_free(&v);
}

void tiger_c_register_func(TigerEnv* env, const char* name, TigerCFuncPtr fn) {
    TigerValue v = tiger_val_cfunc(fn);
    tiger_env_set(env, name, v, true, VAL_CFUNC);
    tiger_val_free(&v);
}

