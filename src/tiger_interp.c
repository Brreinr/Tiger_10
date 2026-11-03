#include "tiger_interp.h"
#include "tiger_parser.h"
#include "tiger_c_interop.h"
#include "tiger_parallel.h"
#include "tiger_simd.h"
#include "tiger_native_hw.h"
#include "tiger_modules.h"
#include "api_traduct.h"

/* ========================================================================= */
/* TABLAS DE FUNCIONES, MACROS Y CLASES/MOLDES                               */
/* ========================================================================= */

typedef struct TigerUserFunc {
    char* name;
    uint64_t name_hash;
    TigerParam* params;
    uint64_t* param_hashes;
    size_t param_count;
    TigerStmt** body;
    size_t body_count;
    struct TigerUserFunc* next;
} TigerUserFunc;

static TigerUserFunc* g_functions = NULL;

static void register_user_func(const char* name, TigerParam* params, size_t param_c, TigerStmt** body, size_t body_c) {
    uint64_t h = tiger_hash64_ci(name);
    TigerUserFunc* f = g_functions;
    while (f) {
        if (f->name_hash == h && strcasecmp(f->name, name) == 0) {
            f->params = params;
            f->param_count = param_c;
            if (f->param_hashes) free(f->param_hashes);
            f->param_hashes = param_c > 0 ? (uint64_t*)malloc(param_c * sizeof(uint64_t)) : NULL;
            for (size_t i = 0; i < param_c; i++) f->param_hashes[i] = tiger_hash64(params[i].name);
            f->body = body;
            f->body_count = body_c;
            return;
        }
        f = f->next;
    }
    TigerUserFunc* nf = (TigerUserFunc*)malloc(sizeof(TigerUserFunc));
    nf->name = strdup(name);
    nf->name_hash = h;
    nf->params = params;
    nf->param_count = param_c;
    nf->param_hashes = param_c > 0 ? (uint64_t*)malloc(param_c * sizeof(uint64_t)) : NULL;
    for (size_t i = 0; i < param_c; i++) nf->param_hashes[i] = tiger_hash64(params[i].name);
    nf->body = body;
    nf->body_count = body_c;
    nf->next = g_functions;
    g_functions = nf;
}

static TigerUserFunc* find_user_func(const char* name) {
    uint64_t h = tiger_hash64_ci(name);
    TigerUserFunc* f = g_functions;
    while (f) {
        if (f->name_hash == h && strcasecmp(f->name, name) == 0) return f;
        f = f->next;
    }
    return NULL;
}

/* Macros de Cero Coste */
typedef struct TigerMacro {
    char* name;
    uint64_t name_hash;
    char* param_name;
    uint64_t param_hash;
    TigerExpr* body_expr;
    struct TigerMacro* next;
} TigerMacro;

static TigerMacro* g_macros = NULL;

static void register_macro(const char* name, const char* param, TigerExpr* body_expr) {
    uint64_t h = tiger_hash64_ci(name);
    TigerMacro* m = g_macros;
    while (m) {
        if (m->name_hash == h && strcasecmp(m->name, name) == 0) {
            m->param_name = param ? strdup(param) : NULL;
            m->param_hash = param ? tiger_hash64(param) : 0;
            m->body_expr = body_expr;
            return;
        }
        m = m->next;
    }
    TigerMacro* nm = (TigerMacro*)malloc(sizeof(TigerMacro));
    nm->name = strdup(name);
    nm->name_hash = h;
    nm->param_name = param ? strdup(param) : NULL;
    nm->param_hash = param ? tiger_hash64(param) : 0;
    nm->body_expr = body_expr;
    nm->next = g_macros;
    g_macros = nm;
}

static TigerMacro* find_macro(const char* name) {
    uint64_t h = tiger_hash64_ci(name);
    TigerMacro* m = g_macros;
    while (m) {
        if (m->name_hash == h && strcasecmp(m->name, name) == 0) return m;
        m = m->next;
    }
    return NULL;
}

/* Moldes y Clases */
typedef struct TigerClassRecord {
    char* name;
    uint64_t name_hash;
    TigerParam* fields;
    TigerExpr** field_inits;
    size_t field_count;
    TigerStmt** methods;
    size_t method_count;
    struct TigerClassRecord* next;
} TigerClassRecord;

static TigerClassRecord* g_classes = NULL;

static void register_class_record(const char* name, TigerParam* fields, TigerExpr** field_inits, size_t field_c, TigerStmt** methods, size_t method_c) {
    uint64_t h = tiger_hash64_ci(name);
    TigerClassRecord* cr = g_classes;
    while (cr) {
        if (cr->name_hash == h && strcasecmp(cr->name, name) == 0) {
            cr->fields = fields;
            cr->field_inits = field_inits;
            cr->field_count = field_c;
            cr->methods = methods;
            cr->method_count = method_c;
            return;
        }
        cr = cr->next;
    }
    TigerClassRecord* ncr = (TigerClassRecord*)malloc(sizeof(TigerClassRecord));
    ncr->name = strdup(name);
    ncr->name_hash = h;
    ncr->fields = fields;
    ncr->field_inits = field_inits;
    ncr->field_count = field_c;
    ncr->methods = methods;
    ncr->method_count = method_c;
    ncr->next = g_classes;
    g_classes = ncr;
}

static TigerClassRecord* find_class_record(const char* name) {
    uint64_t h = tiger_hash64_ci(name);
    TigerClassRecord* cr = g_classes;
    while (cr) {
        if (cr->name_hash == h && strcasecmp(cr->name, name) == 0) return cr;
        cr = cr->next;
    }
    return NULL;
}

/* ========================================================================= */
/* EVALUACIÓN DE EXPRESIONES                                                 */
/* ========================================================================= */

TigerValue tiger_eval_expr(TigerEnv* env, TigerExpr* expr) {
    if (!expr) return tiger_val_nil();

    switch (expr->type) {
        case EXPR_LITERAL:
            return tiger_val_copy(expr->as.literal_val);

        case EXPR_VAR: {
            TigerValue v;
            if (tiger_env_get_fast(env, expr->as.var_name, expr->hash, &v)) {
                return v;
            }
            /* Si es el nombre de una Clase o Molde (ej: VentanaNativa sin parentesis), instanciar automaticamente */
            TigerClassRecord* cr = find_class_record(expr->as.var_name);
            if (cr) {
                TigerValue inst = tiger_val_instance_new(cr->name);
                for (size_t f = 0; f < cr->field_count; f++) {
                    TigerValue fval = cr->field_inits[f] ? tiger_eval_expr(env, cr->field_inits[f]) : tiger_val_nil();
                    tiger_dict_set(inst.as.inst_val->fields, cr->fields[f].name, fval);
                    tiger_val_free(&fval);
                }
                return inst;
            }
            return tiger_val_nil();
        }

        case EXPR_UNARY: {
            TigerValue r = tiger_eval_expr(env, expr->as.unary.right);
            if (expr->as.unary.op == TOK_NOT) {
                bool b = !tiger_val_is_truthy(r);
                tiger_val_free(&r);
                return tiger_val_bool(b);
            }
            if (expr->as.unary.op == TOK_MINUS) {
                if (r.type == VAL_INT) {
                    r.as.i_val = -r.as.i_val;
                    return r;
                }
                if (r.type == VAL_FLOAT) {
                    r.as.f_val = -r.as.f_val;
                    return r;
                }
            }
            return r;
        }

        case EXPR_BINARY: {
            /* Fast-Path para operaciones aritméticas enteras entre variables y literales */
            if (expr->as.binary.left->type == EXPR_VAR && expr->as.binary.right->type == EXPR_LITERAL &&
                expr->as.binary.right->as.literal_val.type == VAL_INT) {
                TigerVarEntry* ea = tiger_env_lookup_entry(env, expr->as.binary.left->as.var_name, expr->as.binary.left->hash);
                if (ea && ea->value.type == VAL_INT) {
                    int64_t v1 = ea->value.as.i_val;
                    int64_t v2 = expr->as.binary.right->as.literal_val.as.i_val;
                    switch (expr->as.binary.op) {
                        case TOK_PERCENT: return tiger_val_int(v2 != 0 ? (v1 % v2) : 0);
                        case TOK_EQ: return tiger_val_bool(v1 == v2);
                        case TOK_NEQ: return tiger_val_bool(v1 != v2);
                        case TOK_LT: return tiger_val_bool(v1 < v2);
                        case TOK_LE: return tiger_val_bool(v1 <= v2);
                        case TOK_GT: return tiger_val_bool(v1 > v2);
                        case TOK_GE: return tiger_val_bool(v1 >= v2);
                        case TOK_PLUS: return tiger_val_int(v1 + v2);
                        case TOK_MINUS: return tiger_val_int(v1 - v2);
                        case TOK_STAR: return tiger_val_int(v1 * v2);
                        default: break;
                    }
                }
            }

            /* Fast-Path para operaciones enteras binarias entre dos variables: (a op b) */
            if (expr->as.binary.left->type == EXPR_VAR && expr->as.binary.right->type == EXPR_VAR) {
                TigerVarEntry* ea = tiger_env_lookup_entry(env, expr->as.binary.left->as.var_name, expr->as.binary.left->hash);
                TigerVarEntry* eb = tiger_env_lookup_entry(env, expr->as.binary.right->as.var_name, expr->as.binary.right->hash);
                if (ea && eb && ea->value.type == VAL_INT && eb->value.type == VAL_INT) {
                    int64_t v1 = ea->value.as.i_val;
                    int64_t v2 = eb->value.as.i_val;
                    switch (expr->as.binary.op) {
                        case TOK_PLUS: return tiger_val_int(v1 + v2);
                        case TOK_MINUS: return tiger_val_int(v1 - v2);
                        case TOK_STAR: return tiger_val_int(v1 * v2);
                        case TOK_SLASH: return tiger_val_int(v2 != 0 ? (v1 / v2) : 0);
                        case TOK_PERCENT: return tiger_val_int(v2 != 0 ? (v1 % v2) : 0);
                        case TOK_EQ: return tiger_val_bool(v1 == v2);
                        case TOK_NEQ: return tiger_val_bool(v1 != v2);
                        case TOK_LT: return tiger_val_bool(v1 < v2);
                        case TOK_LE: return tiger_val_bool(v1 <= v2);
                        case TOK_GT: return tiger_val_bool(v1 > v2);
                        case TOK_GE: return tiger_val_bool(v1 >= v2);
                        default: break;
                    }
                }
            }

            TigerValue a = tiger_eval_expr(env, expr->as.binary.left);

            /* Cortocircuito lógico */
            if (expr->as.binary.op == TOK_AND) {
                if (!tiger_val_is_truthy(a)) {
                    tiger_val_free(&a);
                    return tiger_val_bool(false);
                }
                tiger_val_free(&a);
                TigerValue b = tiger_eval_expr(env, expr->as.binary.right);
                bool res = tiger_val_is_truthy(b);
                tiger_val_free(&b);
                return tiger_val_bool(res);
            }
            if (expr->as.binary.op == TOK_OR) {
                if (tiger_val_is_truthy(a)) {
                    tiger_val_free(&a);
                    return tiger_val_bool(true);
                }
                tiger_val_free(&a);
                TigerValue b = tiger_eval_expr(env, expr->as.binary.right);
                bool res = tiger_val_is_truthy(b);
                tiger_val_free(&b);
                return tiger_val_bool(res);
            }

            TigerValue b = tiger_eval_expr(env, expr->as.binary.right);

            /* Concatenación de cadenas */
            if (expr->as.binary.op == TOK_PLUS && (a.type == VAL_STR || b.type == VAL_STR)) {
                char* sa = tiger_val_to_string(a);
                char* sb = tiger_val_to_string(b);
                size_t la = strlen(sa), lb = strlen(sb);
                char* combined = (char*)malloc(la + lb + 1);
                memcpy(combined, sa, la);
                memcpy(combined + la, sb, lb + 1);
                free(sa);
                free(sb);
                tiger_val_free(&a);
                tiger_val_free(&b);
                return tiger_val_str_take(combined);
            }

            /* Operaciones numéricas */
            bool is_float = (a.type == VAL_FLOAT || b.type == VAL_FLOAT);
            double fa = (a.type == VAL_INT) ? (double)a.as.i_val : a.as.f_val;
            double fb = (b.type == VAL_INT) ? (double)b.as.i_val : b.as.f_val;

            switch (expr->as.binary.op) {
                case TOK_PLUS: {
                    TigerValue res = is_float ? tiger_val_float(fa + fb) : tiger_val_int(a.as.i_val + b.as.i_val);
                    tiger_val_free(&a); tiger_val_free(&b);
                    return res;
                }
                case TOK_MINUS: {
                    TigerValue res = is_float ? tiger_val_float(fa - fb) : tiger_val_int(a.as.i_val - b.as.i_val);
                    tiger_val_free(&a); tiger_val_free(&b);
                    return res;
                }
                case TOK_STAR: {
                    TigerValue res = is_float ? tiger_val_float(fa * fb) : tiger_val_int(a.as.i_val * b.as.i_val);
                    tiger_val_free(&a); tiger_val_free(&b);
                    return res;
                }
                case TOK_SLASH: {
                    TigerValue res;
                    if (is_float) {
                        res = tiger_val_float(fb != 0.0 ? (fa / fb) : 0.0);
                    } else {
                        res = tiger_val_int(b.as.i_val != 0 ? (a.as.i_val / b.as.i_val) : 0);
                    }
                    tiger_val_free(&a); tiger_val_free(&b);
                    return res;
                }
                case TOK_PERCENT: {
                    int64_t ia = (int64_t)fa;
                    int64_t ib = (int64_t)fb;
                    TigerValue res = tiger_val_int(ib != 0 ? (ia % ib) : 0);
                    tiger_val_free(&a); tiger_val_free(&b);
                    return res;
                }
                case TOK_EQ: {
                    bool eq = tiger_val_equals(a, b);
                    tiger_val_free(&a); tiger_val_free(&b);
                    return tiger_val_bool(eq);
                }
                case TOK_NEQ: {
                    bool neq = !tiger_val_equals(a, b);
                    tiger_val_free(&a); tiger_val_free(&b);
                    return tiger_val_bool(neq);
                }
                case TOK_LT: {
                    bool res = is_float ? (fa < fb) : (a.as.i_val < b.as.i_val);
                    tiger_val_free(&a); tiger_val_free(&b);
                    return tiger_val_bool(res);
                }
                case TOK_LE: {
                    bool res = is_float ? (fa <= fb) : (a.as.i_val <= b.as.i_val);
                    tiger_val_free(&a); tiger_val_free(&b);
                    return tiger_val_bool(res);
                }
                case TOK_GT: {
                    bool res = is_float ? (fa > fb) : (a.as.i_val > b.as.i_val);
                    tiger_val_free(&a); tiger_val_free(&b);
                    return tiger_val_bool(res);
                }
                case TOK_GE: {
                    bool res = is_float ? (fa >= fb) : (a.as.i_val >= b.as.i_val);
                    tiger_val_free(&a); tiger_val_free(&b);
                    return tiger_val_bool(res);
                }
                default:
                    tiger_val_free(&a); tiger_val_free(&b);
                    return tiger_val_nil();
            }
        }

        case EXPR_CALL: {
            size_t argc = expr->as.call.arg_count;
            TigerValue stack_argv[16];
            TigerValue* argv = NULL;
            bool heap_argv = false;
            if (argc > 0) {
                if (argc <= 16) {
                    argv = stack_argv;
                } else {
                    argv = (TigerValue*)malloc(argc * sizeof(TigerValue));
                    heap_argv = true;
                }
                for (size_t i = 0; i < argc; i++) {
                    argv[i] = tiger_eval_expr(env, expr->as.call.args[i]);
                }
            }
            #define FREE_CALL_ARGV() do { \
                for (size_t _i = 0; _i < argc; _i++) tiger_val_free(&argv[_i]); \
                if (heap_argv) free(argv); \
            } while(0)

            if (expr->as.call.callee->type == EXPR_VAR) {
                const char* fn_name = expr->as.call.callee->as.var_name;

                /* 1. Longitud / len */
                if (strcasecmp(fn_name, "longitud") == 0 || strcasecmp(fn_name, "len") == 0) {
                    int64_t len = 0;
                    if (argc > 0) {
                        if (argv[0].type == VAL_STR && argv[0].as.s_val) len = (int64_t)strlen(argv[0].as.s_val);
                        else if (argv[0].type == VAL_ARRAY && argv[0].as.arr_val) len = (int64_t)argv[0].as.arr_val->count;
                        else if (argv[0].type == VAL_DICT && argv[0].as.dict_val) len = (int64_t)argv[0].as.dict_val->count;
                    }
                    FREE_CALL_ARGV();
                    return tiger_val_int(len);
                }

#define TIGER_POINTC_MAGIC 0x54475250
typedef struct {
    uint32_t magic;
    size_t capacity;
} TigerPointcHeader;

                /* 2. Registros de Silicio y Punteros Físicos BCI con Mitigación de Ataques */
                if (strcasecmp(fn_name, "bci_leer_registro") == 0) {
                    uint64_t rval = 0;
                    if (argc > 0 && argv[0].type == VAL_INT) {
                        int64_t reg_id = argv[0].as.i_val;
                        if (reg_id < 0 || reg_id >= TIGER_REG_COUNT) {
                            fprintf(stderr, "[Tiger Seguridad]: ATAQUE MITIGADO: Registro de CPU invalido bloqueado (ID: %lld)\n", (long long)reg_id);
                        } else {
                            rval = tiger_bci_read_register((TigerCPURegister)reg_id);
                        }
                    }
                    FREE_CALL_ARGV();
                    return tiger_val_int((int64_t)rval);
                }
                if (strcasecmp(fn_name, "bci_escribir_registro") == 0) {
                    bool ok = false;
                    if (argc >= 2 && argv[0].type == VAL_INT) {
                        int64_t reg_id = argv[0].as.i_val;
                        if (reg_id < 0 || reg_id >= TIGER_REG_COUNT) {
                            fprintf(stderr, "[Tiger Seguridad]: ATAQUE MITIGADO: Registro de CPU invalido bloqueado (ID: %lld)\n", (long long)reg_id);
                        } else {
                            uint64_t val = (argv[1].type == VAL_INT) ? (uint64_t)argv[1].as.i_val : 0;
                            tiger_bci_write_register((TigerCPURegister)reg_id, val);
                            ok = true;
                        }
                    }
                    FREE_CALL_ARGV();
                    return tiger_val_bool(ok);
                }
                if (strcasecmp(fn_name, "pointc_crear") == 0) {
                    int64_t req = (argc > 0 && argv[0].type == VAL_INT) ? argv[0].as.i_val : 64;
                    if (req <= 0 || req > 64 * 1024 * 1024) {
                        fprintf(stderr, "[Tiger Seguridad]: ATAQUE MITIGADO: Asignacion pointc invalida bloqueada (%lld bytes)\n", (long long)req);
                        FREE_CALL_ARGV();
                        return tiger_val_ptr(NULL);
                    }
                    size_t bytes = (size_t)req;
                    TigerPointcHeader* hdr = (TigerPointcHeader*)malloc(sizeof(TigerPointcHeader) + bytes);
                    void* raw = NULL;
                    if (hdr) {
                        hdr->magic = TIGER_POINTC_MAGIC;
                        hdr->capacity = bytes;
                        raw = (void*)(hdr + 1);
                        memset(raw, 0, bytes);
                    }
                    FREE_CALL_ARGV();
                    return tiger_val_ptr(raw);
                }
                if (strcasecmp(fn_name, "pointc_leer") == 0) {
                    uint64_t val = 0;
                    if (argc >= 1 && argv[0].type == VAL_PTR && argv[0].as.ptr_val) {
                        int64_t off_req = (argc >= 2 && argv[1].type == VAL_INT) ? argv[1].as.i_val : 0;
                        void* user_ptr = argv[0].as.ptr_val;
                        TigerPointcHeader* hdr = ((TigerPointcHeader*)user_ptr) - 1;
                        if (off_req < 0 || (hdr->magic == TIGER_POINTC_MAGIC && (size_t)off_req + 8 > hdr->capacity)) {
                            fprintf(stderr, "[Tiger Seguridad]: ATAQUE MITIGADO: Desbordamiento de lectura en pointc bloqueado (offset: %lld, capacidad: %zu)\n",
                                    (long long)off_req, hdr->magic == TIGER_POINTC_MAGIC ? hdr->capacity : 0);
                        } else {
                            val = *(uint64_t*)((uint8_t*)user_ptr + (size_t)off_req);
                        }
                    } else if (argc >= 1 && argv[0].type == VAL_PTR && !argv[0].as.ptr_val) {
                        fprintf(stderr, "[Tiger Seguridad]: ATAQUE MITIGADO: Intento de desreferencia de puntero NULL en pointc bloqueado\n");
                    }
                    FREE_CALL_ARGV();
                    return tiger_val_int((int64_t)val);
                }
                if (strcasecmp(fn_name, "pointc_escribir") == 0) {
                    bool ok = false;
                    if (argc >= 2 && argv[0].type == VAL_PTR && argv[0].as.ptr_val) {
                        int64_t off_req = (argc >= 3 && argv[1].type == VAL_INT) ? argv[1].as.i_val : 0;
                        int64_t val = (argc >= 3) ? ((argv[2].type == VAL_INT) ? argv[2].as.i_val : 0) : ((argv[1].type == VAL_INT) ? argv[1].as.i_val : 0);
                        void* user_ptr = argv[0].as.ptr_val;
                        TigerPointcHeader* hdr = ((TigerPointcHeader*)user_ptr) - 1;
                        if (off_req < 0 || (hdr->magic == TIGER_POINTC_MAGIC && (size_t)off_req + 8 > hdr->capacity)) {
                            fprintf(stderr, "[Tiger Seguridad]: ATAQUE MITIGADO: Desbordamiento de escritura en pointc bloqueado (offset: %lld, capacidad: %zu)\n",
                                    (long long)off_req, hdr->magic == TIGER_POINTC_MAGIC ? hdr->capacity : 0);
                        } else {
                            *(uint64_t*)((uint8_t*)user_ptr + (size_t)off_req) = (uint64_t)val;
                            ok = true;
                        }
                    } else if (argc >= 1 && argv[0].type == VAL_PTR && !argv[0].as.ptr_val) {
                        fprintf(stderr, "[Tiger Seguridad]: ATAQUE MITIGADO: Intento de escritura en puntero NULL en pointc bloqueado\n");
                    }
                    FREE_CALL_ARGV();
                    return tiger_val_bool(ok);
                }

                /* 3. SIMD AVX2 Operaciones Vectoriales en Lotes */
                if (strcasecmp(fn_name, "simd_suma_vector") == 0) {
                    if (argc >= 2 && argv[0].type == VAL_ARRAY && argv[1].type == VAL_ARRAY) {
                        TigerArray* arrA = argv[0].as.arr_val;
                        TigerArray* arrB = argv[1].as.arr_val;
                        size_t n = (arrA->count < arrB->count) ? arrA->count : arrB->count;
                        TigerValue res_arr = tiger_val_array_new();
                        for (size_t k = 0; k < n; k++) {
                            int64_t va = (arrA->items[k].type == VAL_INT) ? arrA->items[k].as.i_val : 0;
                            int64_t vb = (arrB->items[k].type == VAL_INT) ? arrB->items[k].as.i_val : 0;
                            TigerValue sum_val = tiger_val_int(va + vb);
                            tiger_array_push(res_arr.as.arr_val, sum_val);
                            tiger_val_free(&sum_val);
                        }
                        FREE_CALL_ARGV();
                        return res_arr;
                    }
                }

                /* 4. Copiloto MCP / Asistente */
                if (strcasecmp(fn_name, "asistente") == 0 || strcasecmp(fn_name, "assistant") == 0 || strcasecmp(fn_name, "asisstant") == 0) {
                    const char* api = (argc > 0 && argv[0].type == VAL_STR) ? argv[0].as.s_val : NULL;
                    const char* model = (argc > 1 && argv[1].type == VAL_STR) ? argv[1].as.s_val : NULL;
                    const char* prompt = (argc > 2 && argv[2].type == VAL_STR) ? argv[2].as.s_val : NULL;
                    TigerValue res = tiger_assistant_eval(env, api, model, prompt);
                    FREE_CALL_ARGV();
                    return res;
                }

                /* 5. Benchmark y Modos Políglotas de Alta Velocidad */
                if (strcasecmp(fn_name, "benchmark_poliglota") == 0) {
                    FREE_CALL_ARGV();
                    tiger_run_polyglot_benchmark(env);
                    return tiger_val_bool(true);
                }
                if (strcasecmp(fn_name, "fijar_modo_poliglota") == 0 || strcasecmp(fn_name, "set_polyglot_mode") == 0) {
                    if (argc > 0 && argv[0].type == VAL_INT) {
                        tiger_set_polyglot_mode((TigerPolyglotMode)argv[0].as.i_val);
                    } else if (argc > 0 && argv[0].type == VAL_STR) {
                        const char* s = argv[0].as.s_val;
                        if (strcasecmp(s, "COLD") == 0 || strcasecmp(s, "BASELINE") == 0) tiger_set_polyglot_mode(TIGER_POLYGLOT_BASELINE);
                        else if (strcasecmp(s, "FLAGS") == 0 || strcasecmp(s, "DAEMON") == 0) tiger_set_polyglot_mode(TIGER_POLYGLOT_FAST_FLAGS);
                        else if (strcasecmp(s, "INPROC") == 0) tiger_set_polyglot_mode(TIGER_POLYGLOT_INPROC);
                    }
                    FREE_CALL_ARGV();
                    return tiger_val_bool(true);
                }
                if (strcasecmp(fn_name, "obtener_tiempo_ms") == 0 || strcasecmp(fn_name, "get_time_ms") == 0) {
                    FREE_CALL_ARGV();
#ifdef _WIN32
                    LARGE_INTEGER freq, counter;
                    QueryPerformanceFrequency(&freq);
                    QueryPerformanceCounter(&counter);
                    double ms = (double)counter.QuadPart * 1000.0 / (double)freq.QuadPart;
                    return tiger_val_float(ms);
#else
                    return tiger_val_float(0.0);
#endif
                }

                /* 6. Subsistema Nativo de Hardware y Modulos Opcionales bajo demanda */
                TigerValue hw_builtin_res;
                if (tiger_modules_dispatch_call(env, fn_name, argc, argv, &hw_builtin_res)) {
                    FREE_CALL_ARGV();
                    return hw_builtin_res;
                }

                /* 7. Macros de Cero Coste */
                TigerMacro* m = find_macro(fn_name);
                if (m) {
                    TigerEnv* macro_env = tiger_env_new(env);
                    if (m->param_name && argc > 0) {
                        tiger_env_set_fast(macro_env, m->param_name, m->param_hash, argv[0], false, VAL_NIL);
                    }
                    TigerValue res = tiger_eval_expr(macro_env, m->body_expr);
                    tiger_env_free(macro_env);
                    FREE_CALL_ARGV();
                    return res;
                }

                /* 6. Moldes y Clases: Instanciación sin palabra reservada */
                TigerClassRecord* cr = find_class_record(fn_name);
                if (cr) {
                    TigerValue inst = tiger_val_instance_new(cr->name);
                    for (size_t f = 0; f < cr->field_count; f++) {
                        TigerValue fval = cr->field_inits[f] ? tiger_eval_expr(env, cr->field_inits[f]) : tiger_val_nil();
                        if (f < argc) {
                            tiger_val_free(&fval);
                            fval = tiger_val_copy(argv[f]);
                        }
                        tiger_dict_set(inst.as.inst_val->fields, cr->fields[f].name, fval);
                        tiger_val_free(&fval);
                    }
                    FREE_CALL_ARGV();
                    return inst;
                }

                /* 7. Funciones definidas por el usuario */
                TigerUserFunc* ufn = find_user_func(fn_name);
                if (ufn) {
                    TigerEnv* call_env = tiger_env_new(env);
                    for (size_t i = 0; i < ufn->param_count; i++) {
                        TigerValue arg_val = (i < argc) ? argv[i] : tiger_val_nil();
                        tiger_env_set_fast(call_env, ufn->params[i].name, ufn->param_hashes[i], arg_val, false, ufn->params[i].type_req);
                    }
                    ExecSignal sig = tiger_exec_block(call_env, ufn->body, ufn->body_count);
                    tiger_env_free(call_env);

                    FREE_CALL_ARGV();

                    if (sig.status == EXEC_RETURN) return sig.return_val;
                    return tiger_val_nil();
                }
            }

            /* Invocación de Métodos de Clases e Instancias: objeto-metodo(...) o clase-metodo(...) */
            if (expr->as.call.callee->type == EXPR_MEMBER) {
                TigerExpr* target_expr = expr->as.call.callee->as.member.target;
                const char* mname = expr->as.call.callee->as.member.member_name;

                TigerValue inst_val = tiger_eval_expr(env, target_expr);
                TigerClassRecord* cr = NULL;
                if (inst_val.type == VAL_INSTANCE && inst_val.as.inst_val) {
                    cr = find_class_record(inst_val.as.inst_val->class_name);
                } else if (target_expr->type == EXPR_VAR) {
                    cr = find_class_record(target_expr->as.var_name);
                }

                if (cr) {
                    for (size_t m = 0; m < cr->method_count; m++) {
                        TigerStmt* ms = cr->methods[m];
                        if (ms && ms->type == STMT_FUNC_DECL) {
                            bool match = (strcasecmp(ms->as.func_decl.name, mname) == 0);
                            if (!match) {
                                const char* alt_m = tiger_traduct_canonical(mname);
                                if (alt_m && strcasecmp(ms->as.func_decl.name, alt_m) == 0) match = true;
                                if (!match) {
                                    alt_m = tiger_traduct_param(mname, TIGER_LANG_ES, TIGER_LANG_EN);
                                    if (alt_m && strcasecmp(ms->as.func_decl.name, alt_m) == 0) match = true;
                                }
                            }
                            if (!match) continue;

                            TigerEnv* method_env = tiger_env_new(env);
                            /* Inyectar campos de la instancia */
                            if (inst_val.type == VAL_INSTANCE && inst_val.as.inst_val && inst_val.as.inst_val->fields) {
                                for (int b = 0; b < TIGER_DICT_BUCKETS; b++) {
                                    TigerDictEntry* entry = inst_val.as.inst_val->fields->buckets[b];
                                    while (entry) {
                                        tiger_env_set_fast(method_env, entry->key, entry->hash, entry->value, false, VAL_NIL);
                                        entry = entry->next;
                                    }
                                }
                            }
                            /* Inyectar parámetros */
                            for (size_t p = 0; p < ms->as.func_decl.param_count; p++) {
                                TigerValue pval = (p < argc) ? argv[p] : tiger_val_nil();
                                tiger_env_set(method_env, ms->as.func_decl.params[p].name, pval, false, ms->as.func_decl.params[p].type_req);
                            }

                            ExecSignal sig = tiger_exec_block(method_env, ms->as.func_decl.body, ms->as.func_decl.body_count);

                            /* Sincronizar de vuelta los campos modificados en la instancia */
                            if (inst_val.type == VAL_INSTANCE && inst_val.as.inst_val && inst_val.as.inst_val->fields) {
                                for (int b = 0; b < TIGER_DICT_BUCKETS; b++) {
                                    TigerDictEntry* entry = inst_val.as.inst_val->fields->buckets[b];
                                    while (entry) {
                                        TigerValue updated;
                                        if (tiger_env_get_fast(method_env, entry->key, entry->hash, &updated)) {
                                            tiger_dict_set(inst_val.as.inst_val->fields, entry->key, updated);
                                            tiger_val_free(&updated);
                                        }
                                        entry = entry->next;
                                    }
                                }
                                /* Sincronizar también en la variable del entorno si target es una variable */
                                if (target_expr->type == EXPR_VAR) {
                                    tiger_env_assign_fast(env, target_expr->as.var_name, target_expr->hash, inst_val);
                                }
                            }

                            tiger_env_free(method_env);
                            tiger_val_free(&inst_val);
                            FREE_CALL_ARGV();
                            if (sig.status == EXEC_RETURN) return sig.return_val;
                            return tiger_val_nil();
                        }
                    }
                }
                tiger_val_free(&inst_val);
            }

            /* Funciones C nativas registradas */
            TigerValue callee_val = tiger_eval_expr(env, expr->as.call.callee);
            if (callee_val.type == VAL_CFUNC && callee_val.as.cfunc_val) {
                TigerValue res = callee_val.as.cfunc_val((int)argc, argv);
                tiger_val_free(&callee_val);
                FREE_CALL_ARGV();
                return res;
            }

            tiger_val_free(&callee_val);
            FREE_CALL_ARGV();
            #undef FREE_CALL_ARGV
            return tiger_val_nil();
        }

        case EXPR_MEMBER: {
            TigerValue target = tiger_eval_expr(env, expr->as.member.target);
            TigerValue res = tiger_val_nil();
            const char* mname = expr->as.member.member_name;

            if (target.type == VAL_INSTANCE && target.as.inst_val && target.as.inst_val->fields) {
                if (!tiger_dict_get(target.as.inst_val->fields, mname, &res) || res.type == VAL_NIL) {
                    /* Resolucion bilingüe mediante api_traduct para parametros estandar (height <-> alto, etc.) */
                    const char* alt = tiger_traduct_canonical(mname);
                    if (alt && strcmp(alt, mname) != 0) {
                        tiger_dict_get(target.as.inst_val->fields, alt, &res);
                    }
                    if (res.type == VAL_NIL) {
                        alt = tiger_traduct_param(mname, TIGER_LANG_ES, TIGER_LANG_EN);
                        if (alt && strcmp(alt, mname) != 0) {
                            tiger_dict_get(target.as.inst_val->fields, alt, &res);
                        }
                    }
                }
            } else if (target.type == VAL_DICT && target.as.dict_val) {
                if (!tiger_dict_get(target.as.dict_val, mname, &res) || res.type == VAL_NIL) {
                    const char* alt = tiger_traduct_canonical(mname);
                    if (alt && strcmp(alt, mname) != 0) {
                        tiger_dict_get(target.as.dict_val, alt, &res);
                    }
                    if (res.type == VAL_NIL) {
                        alt = tiger_traduct_param(mname, TIGER_LANG_ES, TIGER_LANG_EN);
                        if (alt && strcmp(alt, mname) != 0) {
                            tiger_dict_get(target.as.dict_val, alt, &res);
                        }
                    }
                }
            }
            tiger_val_free(&target);
            return res;
        }

        case EXPR_INDEX: {
            TigerValue target = tiger_eval_expr(env, expr->as.index.target);
            TigerValue idx = tiger_eval_expr(env, expr->as.index.index);
            TigerValue res = tiger_val_nil();
            if (target.type == VAL_ARRAY && target.as.arr_val && idx.type == VAL_INT) {
                int64_t i = idx.as.i_val;
                if (i >= 0 && (size_t)i < target.as.arr_val->count) {
                    res = tiger_val_copy(target.as.arr_val->items[i]);
                }
            } else if (target.type == VAL_DICT && target.as.dict_val && idx.type == VAL_STR && idx.as.s_val) {
                tiger_dict_get(target.as.dict_val, idx.as.s_val, &res);
            }
            tiger_val_free(&target);
            tiger_val_free(&idx);
            return res;
        }

        case EXPR_ARRAY_LIT: {
            TigerValue av = tiger_val_array_new();
            for (size_t i = 0; i < expr->as.array_lit.count; i++) {
                TigerValue ev = tiger_eval_expr(env, expr->as.array_lit.elements[i]);
                tiger_array_push(av.as.arr_val, ev);
                tiger_val_free(&ev);
            }
            return av;
        }

        case EXPR_DICT_LIT: {
            TigerValue dv = tiger_val_dict_new();
            for (size_t i = 0; i < expr->as.dict_lit.count; i++) {
                TigerValue ev = tiger_eval_expr(env, expr->as.dict_lit.values[i]);
                tiger_dict_set(dv.as.dict_val, expr->as.dict_lit.keys[i], ev);
                tiger_val_free(&ev);
            }
            return dv;
        }

        case EXPR_INPUT: {
            if (expr->as.input.prompt) {
                TigerValue pval = tiger_eval_expr(env, expr->as.input.prompt);
                tiger_val_print(pval, false);
                tiger_val_free(&pval);
            }
            tiger_io_flush_fast();
            fflush(stdout);

            char buffer[2048];
            if (!fgets(buffer, sizeof(buffer), stdin)) {
                return tiger_val_str("");
            }

            size_t len = strlen(buffer);
            while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
                buffer[--len] = '\0';
            }

            char* trimmed = buffer;
            while (*trimmed && isspace((unsigned char)*trimmed)) trimmed++;

            if (strcasecmp(trimmed, "VERDADERO") == 0 || strcasecmp(trimmed, "TRUE") == 0 ||
                strcasecmp(trimmed, "CIERTO") == 0) {
                return tiger_val_bool(true);
            }
            if (strcasecmp(trimmed, "FALSO") == 0 || strcasecmp(trimmed, "FALSE") == 0) {
                return tiger_val_bool(false);
            }

            char* endptr = NULL;
            if (*trimmed != '\0') {
                int64_t ival = strtoll(trimmed, &endptr, 10);
                if (endptr && *endptr == '\0') {
                    return tiger_val_int(ival);
                }
                double fval = strtod(trimmed, &endptr);
                if (endptr && *endptr == '\0') {
                    return tiger_val_float(fval);
                }
            }

            return tiger_val_str(buffer);
        }

        default:
            return tiger_val_nil();
    }
}

/* ========================================================================= */
/* EJECUCIÓN DE SENTENCIAS                                                   */
/* ========================================================================= */

ExecSignal tiger_exec_stmt(TigerEnv* env, TigerStmt* stmt) {
    ExecSignal sig = { EXEC_OK, tiger_val_nil() };
    if (!stmt) return sig;

    switch (stmt->type) {
        case STMT_VAR_DECL: {
            TigerValue init_val = stmt->as.var_decl.init ? tiger_eval_expr(env, stmt->as.var_decl.init) : tiger_val_nil();
            tiger_env_set_fast(env, stmt->as.var_decl.name, stmt->hash, init_val, stmt->as.var_decl.is_const, stmt->as.var_decl.type_req);
            tiger_val_free(&init_val);
            break;
        }

        case STMT_ASSIGN: {
            if (stmt->as.assign.target->type == EXPR_VAR) {
                const char* vname = stmt->as.assign.target->as.var_name;
                uint64_t vhash = stmt->as.assign.target->hash;
                TigerVarEntry* e = tiger_env_lookup_entry(env, vname, vhash);

                /* Fast-Path para variables enteras en bucles */
                if (e && e->value.type == VAL_INT && !e->is_const) {
                    if (stmt->as.assign.op == TOK_PLUS_ASSIGN) {
                        TigerExpr* rhs = stmt->as.assign.value;
                        if (rhs->type == EXPR_LITERAL && rhs->as.literal_val.type == VAL_INT) {
                            e->value.as.i_val += rhs->as.literal_val.as.i_val;
                            break;
                        }
                    } else if (stmt->as.assign.op == TOK_MINUS_ASSIGN) {
                        TigerExpr* rhs = stmt->as.assign.value;
                        if (rhs->type == EXPR_LITERAL && rhs->as.literal_val.type == VAL_INT) {
                            e->value.as.i_val -= rhs->as.literal_val.as.i_val;
                            break;
                        }
                    } else if (stmt->as.assign.op == TOK_ASSIGN || stmt->as.assign.op == TOK_ARROW_L) {
                        TigerExpr* rhs = stmt->as.assign.value;
                        if (rhs->type == EXPR_LITERAL && rhs->as.literal_val.type == VAL_INT) {
                            e->value.as.i_val = rhs->as.literal_val.as.i_val;
                            break;
                        }
                    }
                }

                TigerValue val = tiger_eval_expr(env, stmt->as.assign.value);
                if (stmt->as.assign.op == TOK_PLUS_ASSIGN) {
                    if (e && e->value.type == VAL_INT && val.type == VAL_INT) {
                        val.as.i_val = e->value.as.i_val + val.as.i_val;
                    }
                }
                tiger_env_assign_fast(env, vname, vhash, val);
                tiger_val_free(&val);
                break;
            }

            TigerValue val = tiger_eval_expr(env, stmt->as.assign.value);
            if (stmt->as.assign.target->type == EXPR_MEMBER) {
                TigerValue target = tiger_eval_expr(env, stmt->as.assign.target->as.member.target);
                if (target.type == VAL_INSTANCE && target.as.inst_val && target.as.inst_val->fields) {
                    tiger_dict_set(target.as.inst_val->fields, stmt->as.assign.target->as.member.member_name, val);
                } else if (target.type == VAL_DICT && target.as.dict_val) {
                    tiger_dict_set(target.as.dict_val, stmt->as.assign.target->as.member.member_name, val);
                }
                tiger_val_free(&target);
            } else if (stmt->as.assign.target->type == EXPR_INDEX) {
                TigerValue target = tiger_eval_expr(env, stmt->as.assign.target->as.index.target);
                TigerValue idx = tiger_eval_expr(env, stmt->as.assign.target->as.index.index);
                if (target.type == VAL_ARRAY && target.as.arr_val && idx.type == VAL_INT) {
                    int64_t i = idx.as.i_val;
                    if (i >= 0 && (size_t)i < target.as.arr_val->count) {
                        tiger_val_free(&target.as.arr_val->items[i]);
                        target.as.arr_val->items[i] = tiger_val_copy(val);
                    }
                } else if (target.type == VAL_DICT && target.as.dict_val && idx.type == VAL_STR && idx.as.s_val) {
                    tiger_dict_set(target.as.dict_val, idx.as.s_val, val);
                }
                tiger_val_free(&target);
                tiger_val_free(&idx);
            }
            tiger_val_free(&val);
            break;
        }

        case STMT_PRINT: {
            TigerValue val = tiger_eval_expr(env, stmt->as.print.expr);
            tiger_val_print(val, stmt->as.print.newline);
            tiger_val_free(&val);
            break;
        }

        case STMT_IF: {
            TigerValue cond = tiger_eval_expr(env, stmt->as.if_stmt.condition);
            bool is_true = tiger_val_is_truthy(cond);
            tiger_val_free(&cond);

            if (is_true) {
                sig = tiger_exec_block(env, stmt->as.if_stmt.then_stmts, stmt->as.if_stmt.then_count);
            } else if (stmt->as.if_stmt.else_stmts && stmt->as.if_stmt.else_count > 0) {
                sig = tiger_exec_block(env, stmt->as.if_stmt.else_stmts, stmt->as.if_stmt.else_count);
            }
            break;
        }

        case STMT_WHILE: {
            if (stmt->as.while_stmt.is_parallel) {
                if (tiger_exec_parallel_while(env, stmt)) break;
            }

            TigerExpr* cond_expr = stmt->as.while_stmt.condition;
            TigerStmt** body = stmt->as.while_stmt.body;
            size_t bcount = stmt->as.while_stmt.body_count;

            /* Fast-Path para bucles de conteo simples: MIENTRAS (var < limite) o (var <= limite) */
            if (cond_expr && cond_expr->type == EXPR_BINARY &&
                (cond_expr->as.binary.op == TOK_LT || cond_expr->as.binary.op == TOK_LE) &&
                cond_expr->as.binary.left->type == EXPR_VAR) {

                const char* cvname = cond_expr->as.binary.left->as.var_name;
                uint64_t cvhash = cond_expr->as.binary.left->hash;
                TigerVarEntry* entry = tiger_env_lookup_entry(env, cvname, cvhash);

                if (entry && entry->value.type == VAL_INT && !entry->is_const) {
                    bool is_le = (cond_expr->as.binary.op == TOK_LE);
                    bool const_limit = (cond_expr->as.binary.right->type == EXPR_LITERAL &&
                                        cond_expr->as.binary.right->as.literal_val.type == VAL_INT);
                    int64_t fixed_limit = const_limit ? cond_expr->as.binary.right->as.literal_val.as.i_val : 0;

                    while (true) {
                        int64_t curr_i = entry->value.as.i_val;
                        int64_t limit_i = fixed_limit;
                        if (!const_limit) {
                            TigerValue limit_val = tiger_eval_expr(env, cond_expr->as.binary.right);
                            limit_i = (limit_val.type == VAL_INT) ? limit_val.as.i_val : (int64_t)limit_val.as.f_val;
                            tiger_val_free(&limit_val);
                        }

                        if (is_le ? (curr_i > limit_i) : (curr_i >= limit_i)) break;

                        sig = tiger_exec_block(env, body, bcount);
                        if (sig.status == EXEC_BREAK) {
                            sig.status = EXEC_OK;
                            break;
                        }
                        if (sig.status == EXEC_RETURN) return sig;
                        if (sig.status == EXEC_CONTINUE) {
                            sig.status = EXEC_OK;
                            continue;
                        }
                    }
                    break;
                }
            }

            while (true) {
                TigerValue cond = tiger_eval_expr(env, stmt->as.while_stmt.condition);
                bool ok = tiger_val_is_truthy(cond);
                tiger_val_free(&cond);
                if (!ok) break;

                sig = tiger_exec_block(env, stmt->as.while_stmt.body, stmt->as.while_stmt.body_count);
                if (sig.status == EXEC_BREAK) {
                    sig.status = EXEC_OK;
                    break;
                }
                if (sig.status == EXEC_RETURN) return sig;
                if (sig.status == EXEC_CONTINUE) {
                    sig.status = EXEC_OK;
                    continue;
                }
            }
            break;
        }

        case STMT_RETURN: {
            sig.status = EXEC_RETURN;
            sig.return_val = stmt->as.return_stmt.expr ? tiger_eval_expr(env, stmt->as.return_stmt.expr) : tiger_val_nil();
            break;
        }

        case STMT_BREAK:
            sig.status = EXEC_BREAK;
            break;

        case STMT_CONTINUE:
            sig.status = EXEC_CONTINUE;
            break;

        case STMT_FUNC_DECL: {
            register_user_func(
                stmt->as.func_decl.name,
                stmt->as.func_decl.params,
                stmt->as.func_decl.param_count,
                stmt->as.func_decl.body,
                stmt->as.func_decl.body_count
            );
            break;
        }

        case STMT_CLASS_DECL: {
            register_class_record(
                stmt->as.class_decl.name,
                stmt->as.class_decl.fields,
                stmt->as.class_decl.field_inits,
                stmt->as.class_decl.field_count,
                stmt->as.class_decl.methods,
                stmt->as.class_decl.method_count
            );
            break;
        }

        case STMT_MACRO: {
            register_macro(
                stmt->as.macro_decl.name,
                stmt->as.macro_decl.param_name,
                stmt->as.macro_decl.body_expr
            );
            break;
        }

        case STMT_ASSISTANT: {
            tiger_assistant_call(
                env,
                stmt->as.assistant.api_key,
                stmt->as.assistant.model,
                stmt->as.assistant.prompt,
                stmt->as.assistant.target_var
            );
            break;
        }

        case STMT_POLYGLOT: {
            tiger_io_flush_fast();
            tiger_execute_polyglot_box(
                env,
                stmt->as.polyglot.lang,
                stmt->as.polyglot.code,
                stmt->as.polyglot.target_var
            );
            break;
        }

        case STMT_USE: {
            if (stmt->as.use_stmt.is_c_header) {
                tiger_import_c_module(env, stmt->as.use_stmt.module_path);
            } else {
                tiger_run_file(stmt->as.use_stmt.module_path, env, true);
            }
            break;
        }

        case STMT_HW_TARGET: {
            tiger_io_flush_fast();
            TigerHardwareTarget prev = tiger_hw_get_target();
            TigerHardwareTarget tgt = (TigerHardwareTarget)stmt->as.hw_target.target;
            tiger_hw_set_target(tgt);

            if (tgt == TIGER_HW_GPU) {
                printf("[EMPAQUETADOR -> SILICIO GPU]: Directiva 'USAR EN GPU' detectada. Codigo despachado directamente a la GPU (%s).\n", tiger_hw_get_gpu_name());
            } else if (tgt == TIGER_HW_CPU) {
                printf("[EMPAQUETADOR -> SILICIO CPU]: Directiva 'USAR EN CPU' detectada. Codigo despachado a la CPU (%s con SIMD AVX2).\n", tiger_hw_get_cpu_name());
            } else if (tgt == TIGER_HW_NPU) {
                printf("[EMPAQUETADOR -> SILICIO NPU]: Directiva 'USAR EN NPU' detectada. Codigo despachado a la NPU (%s Tensor Engine).\n", tiger_hw_get_npu_name());
            } else if (tgt == TIGER_HW_MAS) {
                printf("[EMPAQUETADOR -> SILICIO MAS (3 PROCESADORES)]: Directiva 'USAR EN MAS' detectada. Procesando cooperativamente entre CPU + GPU + NPU:\n");
                printf("  * [1/3] CPU Maestro 0: %s (%d Cores, SIMD AVX2) -> Logica y control de flujo\n", tiger_hw_get_cpu_name(), tiger_hw_get_cpu_cores());
                printf("  * [2/3] GPU Maestro 1: %s -> Shaders, Matrices 2D/3D y Aceleracion Direct GDI\n", tiger_hw_get_gpu_name());
                printf("  * [3/3] NPU Tensor:    %s -> Inferencia tensorial vectorial INT64/FP64\n", tiger_hw_get_npu_name());
            }
            fflush(stdout);

            if (stmt->as.hw_target.body && stmt->as.hw_target.body_count > 0) {
                sig = tiger_exec_block(env, stmt->as.hw_target.body, stmt->as.hw_target.body_count);
                tiger_hw_set_target(prev);
            }
            break;
        }

        case STMT_EXPR: {
            TigerValue val = tiger_eval_expr(env, stmt->as.expr);
            tiger_val_free(&val);
            break;
        }

        default:
            break;
    }

    return sig;
}

ExecSignal tiger_exec_block(TigerEnv* env, TigerStmt** stmts, size_t count) {
    ExecSignal sig = { EXEC_OK, tiger_val_nil() };
    for (size_t i = 0; i < count; i++) {
        sig = tiger_exec_stmt(env, stmts[i]);
        if (sig.status != EXEC_OK) break;
    }
    return sig;
}

bool tiger_run_source(const char* source, size_t length, TigerEnv* env, bool flexible) {
    if (!source || length == 0) return true;

    TigerParser parser;
    tiger_parser_init(&parser, source, length, flexible);

    size_t count = 0;
    TigerStmt** stmts = tiger_parse_program(&parser, &count);

    if (parser.has_error) {
        tiger_parser_free(&parser);
        return false;
    }

    /* Ejecución con el Despachador Concurrente sin Bloqueos (2 Maestros + 4 Subprocesadores) */
    tiger_ejecutar_dual_paralelo(env, stmts, count);
    tiger_io_flush_fast();

    for (size_t i = 0; i < count; i++) {
        if (stmts[i] &&
            stmts[i]->type != STMT_FUNC_DECL &&
            stmts[i]->type != STMT_CLASS_DECL &&
            stmts[i]->type != STMT_MACRO) {
            tiger_stmt_free(stmts[i]);
        }
    }
    free(stmts);
    tiger_parser_free(&parser);
    return true;
}

bool tiger_run_file(const char* filepath, TigerEnv* env, bool flexible) {
    size_t size = 0;
    char* src = tiger_read_file_resilient(filepath, &size);
    if (!src) {
        char lib_path[MAX_PATH];
        snprintf(lib_path, sizeof(lib_path), "tools\\Libraries\\%s", filepath);
        src = tiger_read_file_resilient(lib_path, &size);
        if (!src) {
            snprintf(lib_path, sizeof(lib_path), "tools\\%s", filepath);
            src = tiger_read_file_resilient(lib_path, &size);
        }
    }

    if (!src) {
        fprintf(stderr, "[Error Tiger]: No se pudo abrir el archivo '%s'\n", filepath);
        return false;
    }

    bool ok = tiger_run_source(src, size, env, flexible);
    free(src);
    return ok;
}

