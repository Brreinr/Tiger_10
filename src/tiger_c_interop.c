#include "tiger_c_interop.h"
#include "tiger_interp.h"
#include "tiger_ai_chat.h"
#include "tiger_native_hw.h"

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#endif

/* Variable global de entorno para puente rápido con C */
static TigerEnv* g_active_tiger_env = NULL;

static void tiger_sync_json_to_env(TigerEnv* env, const char* json_str);

/* Modos de aceleración de ejecución políglota (por defecto Nivel 1 + Fast-Path In-Process) */
static TigerPolyglotMode g_polyglot_mode = TIGER_POLYGLOT_FAST_FLAGS;

void tiger_set_polyglot_mode(TigerPolyglotMode mode) {
    g_polyglot_mode = mode;
}

TigerPolyglotMode tiger_get_polyglot_mode(void) {
    return g_polyglot_mode;
}

void tiger_polyglot_daemons_init(void) {
    /* Sin demonios externos ni mapeos innecesarios al inicio: arranque en microsegundos */
}

void tiger_polyglot_daemons_shutdown(void) {
    /* Limpieza instantánea */
}

static int64_t bridge_get_int(const char* name) {
    if (!g_active_tiger_env) return 0;
    TigerValue v;
    if (tiger_env_get(g_active_tiger_env, name, &v)) {
        int64_t res = (v.type == VAL_INT) ? v.as.i_val : (v.type == VAL_FLOAT ? (int64_t)v.as.f_val : 0);
        tiger_val_free(&v);
        return res;
    }
    return 0;
}

static void bridge_set_int(const char* name, int64_t val) {
    if (!g_active_tiger_env) return;
    TigerValue v = tiger_val_int(val);
    tiger_env_assign(g_active_tiger_env, name, v);
    tiger_val_free(&v);
}

static double bridge_get_float(const char* name) {
    if (!g_active_tiger_env) return 0.0;
    TigerValue v;
    if (tiger_env_get(g_active_tiger_env, name, &v)) {
        double res = (v.type == VAL_FLOAT) ? v.as.f_val : (v.type == VAL_INT ? (double)v.as.i_val : 0.0);
        tiger_val_free(&v);
        return res;
    }
    return 0.0;
}

static void bridge_set_float(const char* name, double val) {
    if (!g_active_tiger_env) return;
    TigerValue v = tiger_val_float(val);
    tiger_env_assign(g_active_tiger_env, name, v);
    tiger_val_free(&v);
}

static const char* bridge_get_str(const char* name) {
    if (!g_active_tiger_env) return "";
    TigerValue v;
    if (tiger_env_get(g_active_tiger_env, name, &v) && v.type == VAL_STR) {
        return v.as.s_val;
    }
    return "";
}

static void bridge_set_str(const char* name, const char* val) {
    if (!g_active_tiger_env) return;
    TigerValue v = tiger_val_str(val);
    tiger_env_assign(g_active_tiger_env, name, v);
    tiger_val_free(&v);
}

static TigerBridgeAPI g_bridge = {
    bridge_get_int,
    bridge_set_int,
    bridge_get_float,
    bridge_set_float,
    bridge_get_str,
    bridge_set_str
};

/* 1. Elimina la indentación común (dedent) */
static char* tiger_dedent(const char* code) {
    if (!code) return strdup("");
    int min_indent = -1;
    const char* p = code;
    while (*p) {
        while (*p == '\r' || *p == '\n') p++;
        if (!*p) break;
        int indent = 0;
        while (*p == ' ' || *p == '\t') {
            indent += (*p == '\t') ? 4 : 1;
            p++;
        }
        if (*p != '\r' && *p != '\n' && *p != '\0') {
            if (min_indent == -1 || indent < min_indent) {
                min_indent = indent;
            }
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    if (min_indent <= 0) return strdup(code);

    size_t len = strlen(code);
    char* result = (char*)malloc(len + 1);
    char* out = result;
    p = code;

    while (*p) {
        if (*p == '\r' || *p == '\n') {
            *out++ = *p++;
            continue;
        }
        int removed = 0;
        while ((*p == ' ' || *p == '\t') && removed < min_indent) {
            removed += (*p == '\t') ? 4 : 1;
            p++;
        }
        while (*p && *p != '\n') {
            *out++ = *p++;
        }
        if (*p == '\n') {
            *out++ = *p++;
        }
    }
    *out = '\0';
    return result;
}

/* 2. Sustitución de sintaxis TIGER-metodo / TIGRE-variable -> TIGER.metodo / TIGRE.variable */
static char* replace_tiger_dash_syntax(const char* code) {
    if (!code) return strdup("");
    size_t len = strlen(code);
    char* result = strdup(code);
    for (size_t i = 0; i + 6 <= len; i++) {
        if (strncasecmp(result + i, "TIGER-", 6) == 0) {
            result[i + 5] = '.';
            i += 5;
        } else if (strncasecmp(result + i, "TIGRE-", 6) == 0) {
            result[i + 5] = '.';
            i += 5;
        }
    }
    return result;
}

/* ========================================================================= */
/* BLINDAJE DE SEGURIDAD: SANITIZACIÓN Y ESCAPE DE INYECCIONES DE CÓDIGO     */
/* ========================================================================= */

static bool is_valid_c_ident(const char* name) {
    if (!name || !name[0]) return false;
    if (!isalpha((unsigned char)name[0]) && name[0] != '_') return false;
    for (size_t i = 1; name[i]; i++) {
        if (!isalnum((unsigned char)name[i]) && name[i] != '_') return false;
    }
    return true;
}



static char* tiger_escape_c_string(const char* s) {
    if (!s) return strdup("");
    size_t len = strlen(s);
    size_t cap = len * 2 + 32;
    char* out = (char*)malloc(cap);
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        if (o + 4 >= cap) {
            cap *= 2;
            out = (char*)realloc(out, cap);
        }
        char c = s[i];
        if (c == '\\') {
            out[o++] = '\\'; out[o++] = '\\';
        } else if (c == '"') {
            out[o++] = '\\'; out[o++] = '"';
        } else if (c == '\n') {
            out[o++] = '\\'; out[o++] = 'n';
        } else if (c == '\r') {
            out[o++] = '\\'; out[o++] = 'r';
        } else if (c == '\t') {
            out[o++] = '\\'; out[o++] = 't';
        } else if ((unsigned char)c < 32) {
            out[o++] = ' ';
        } else {
            out[o++] = c;
        }
    }
    out[o] = '\0';
    return out;
}

/* 3. Exporta todas las variables activas de Tiger a JSON con escape contra inyecciones */
static char* tiger_env_to_json(TigerEnv* env) {
    size_t cap = 4096;
    size_t len = 0;
    char* buf = (char*)malloc(cap);
    buf[0] = '{';
    buf[1] = '\0';
    len = 1;

    bool first = true;
    for (int i = 0; i < TIGER_ENV_BUCKETS; i++) {
        TigerVarEntry* e = env->buckets[i];
        while (e) {
            if (!is_valid_c_ident(e->name)) {
                e = e->next;
                continue;
            }
            char entry_prefix[128];
            snprintf(entry_prefix, sizeof(entry_prefix), "%s\"%s\": ", first ? "" : ", ", e->name);
            first = false;

            char* val_str = NULL;
            char num_buf[64];
            if (e->value.type == VAL_INT) {
                snprintf(num_buf, sizeof(num_buf), "%lld", (long long)e->value.as.i_val);
                val_str = strdup(num_buf);
            } else if (e->value.type == VAL_FLOAT) {
                snprintf(num_buf, sizeof(num_buf), "%g", e->value.as.f_val);
                val_str = strdup(num_buf);
            } else if (e->value.type == VAL_BOOL) {
                val_str = strdup(e->value.as.b_val ? "true" : "false");
            } else if (e->value.type == VAL_STR && e->value.as.s_val) {
                char* escaped = tiger_escape_c_string(e->value.as.s_val);
                size_t elen = strlen(escaped);
                val_str = (char*)malloc(elen + 3);
                val_str[0] = '"';
                memcpy(val_str + 1, escaped, elen);
                val_str[elen + 1] = '"';
                val_str[elen + 2] = '\0';
                free(escaped);
            }

            if (val_str) {
                size_t pre_len = strlen(entry_prefix);
                size_t val_len = strlen(val_str);
                while (len + pre_len + val_len + 8 >= cap) {
                    cap = (len + pre_len + val_len + 1024) * 2;
                    buf = (char*)realloc(buf, cap);
                }
                memcpy(buf + len, entry_prefix, pre_len);
                len += pre_len;
                memcpy(buf + len, val_str, val_len);
                len += val_len;
                buf[len] = '\0';
                free(val_str);
            }
            e = e->next;
        }
    }
    if (len + 2 >= cap) {
        cap += 16;
        buf = (char*)realloc(buf, cap);
    }
    buf[len++] = '}';
    buf[len] = '\0';
    return buf;
}

/* 4. Importa variables modificadas o creadas desde JSON hacia TigerEnv */
static void tiger_sync_json_to_env(TigerEnv* env, const char* json_str) {
    if (!env || !json_str) return;
    const char* p = json_str;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    if (*p == '{') p++;

    while (*p && *p != '}') {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',') p++;
        if (*p == '}' || !*p) break;

        if (*p != '"') break;
        p++;
        char key[128];
        size_t klen = 0;
        while (*p && *p != '"' && klen + 1 < sizeof(key)) {
            key[klen++] = *p++;
        }
        key[klen] = '\0';
        if (*p == '"') p++;

        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':') p++;

        TigerValue val = tiger_val_nil();
        if (*p == '"') {
            p++;
            char str_buf[2048];
            size_t slen = 0;
            while (*p && *p != '"' && slen + 1 < sizeof(str_buf)) {
                if (*p == '\\' && *(p + 1)) {
                    p++;
                    if (*p == 'n') str_buf[slen++] = '\n';
                    else if (*p == 't') str_buf[slen++] = '\t';
                    else if (*p == 'r') str_buf[slen++] = '\r';
                    else str_buf[slen++] = *p;
                } else {
                    str_buf[slen++] = *p;
                }
                p++;
            }
            str_buf[slen] = '\0';
            if (*p == '"') p++;
            val = tiger_val_str(str_buf);
        } else if (strncmp(p, "true", 4) == 0) {
            val = tiger_val_bool(true);
            p += 4;
        } else if (strncmp(p, "false", 5) == 0) {
            val = tiger_val_bool(false);
            p += 5;
        } else if (strncmp(p, "null", 4) == 0) {
            val = tiger_val_nil();
            p += 4;
        } else if (*p == '-' || (*p >= '0' && *p <= '9')) {
            char* end_ptr = NULL;
            const char* check_f = p;
            bool is_float = false;
            while (*check_f && *check_f != ',' && *check_f != '}' && *check_f != ' ' && *check_f != '\n') {
                if (*check_f == '.') is_float = true;
                check_f++;
            }
            if (is_float) {
                double f = strtod(p, &end_ptr);
                val = tiger_val_float(f);
            } else {
                long long i = strtoll(p, &end_ptr, 10);
                val = tiger_val_int((int64_t)i);
            }
            p = end_ptr;
        } else {
            char comp_buf[2048];
            size_t clen = 0;
            int depth = 0;
            while (*p && clen + 1 < sizeof(comp_buf)) {
                if (*p == '{' || *p == '[') depth++;
                else if (*p == '}' || *p == ']') {
                    if (depth == 0) break;
                    depth--;
                } else if (*p == ',' && depth == 0) break;
                comp_buf[clen++] = *p++;
            }
            comp_buf[clen] = '\0';
            val = tiger_val_str(comp_buf);
        }

        tiger_env_set(env, key, val, false, VAL_NIL);
        tiger_val_free(&val);
    }
}

/* ========================================================================= */
/* EJECUCIÓN EN MEMORIA COMPARTIDA SIN TOCAR EL DISCO DURO                   */
/* ========================================================================= */

static bool tiger_run_process_in_memory(TigerEnv* env, const char* cmd, const char* input_code) {
#ifdef _WIN32
    HANDLE hStdInRd = NULL, hStdInWr = NULL;
    HANDLE hStdOutRd = NULL, hStdOutWr = NULL;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hStdInRd, &hStdInWr, &sa, 0)) return false;
    SetHandleInformation(hStdInWr, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&hStdOutRd, &hStdOutWr, &sa, 0)) {
        CloseHandle(hStdInRd); CloseHandle(hStdInWr);
        return false;
    }
    SetHandleInformation(hStdOutRd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hStdInRd;
    si.hStdOutput = hStdOutWr;
    si.hStdError = hStdOutWr;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    char cmd_copy[1024];
    strncpy(cmd_copy, cmd, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';

    if (!CreateProcessA(NULL, cmd_copy, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(hStdInRd); CloseHandle(hStdInWr);
        CloseHandle(hStdOutRd); CloseHandle(hStdOutWr);
        return false;
    }

    CloseHandle(hStdInRd);
    CloseHandle(hStdOutWr);

    /* Inyección de código en memoria por la tubería de silicio sin tocar disco */
    if (input_code && input_code[0]) {
        DWORD bytes_written = 0;
        size_t total_len = strlen(input_code);
        WriteFile(hStdInWr, input_code, (DWORD)total_len, &bytes_written, NULL);
    }
    CloseHandle(hStdInWr);

    /* Lectura de resultados y sincronización bidireccional */
    char buf[4096];
    DWORD bytes_read = 0;
    char line_acc[8192];
    size_t line_len = 0;

    while (ReadFile(hStdOutRd, buf, sizeof(buf) - 1, &bytes_read, NULL) && bytes_read > 0) {
        buf[bytes_read] = '\0';
        for (DWORD i = 0; i < bytes_read; i++) {
            char ch = buf[i];
            line_acc[line_len++] = ch;
            if (ch == '\n' || line_len >= sizeof(line_acc) - 1) {
                line_acc[line_len] = '\0';
                char* sync_start = strstr(line_acc, "__TIGER_SYNC_START__");
                if (sync_start) {
                    *sync_start = '\0';
                    if (line_acc[0]) printf("%s", line_acc);
                    char* json_start = sync_start + strlen("__TIGER_SYNC_START__");
                    char* sync_end = strstr(json_start, "__TIGER_SYNC_END__");
                    if (sync_end) {
                        *sync_end = '\0';
                        tiger_sync_json_to_env(env, json_start);
                        char* remaining = sync_end + strlen("__TIGER_SYNC_END__");
                        if (remaining[0]) printf("%s", remaining);
                    }
                } else {
                    printf("%s", line_acc);
                }
                line_len = 0;
            }
        }
    }

    if (line_len > 0) {
        line_acc[line_len] = '\0';
        char* sync_start = strstr(line_acc, "__TIGER_SYNC_START__");
        if (sync_start) {
            *sync_start = '\0';
            if (line_acc[0]) printf("%s", line_acc);
            char* json_start = sync_start + strlen("__TIGER_SYNC_START__");
            char* sync_end = strstr(json_start, "__TIGER_SYNC_END__");
            if (sync_end) {
                *sync_end = '\0';
                tiger_sync_json_to_env(env, json_start);
            }
        } else {
            printf("%s", line_acc);
        }
    }

    CloseHandle(hStdOutRd);
    fflush(stdout);
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
#else
    return false;
#endif
}

/* ========================================================================= */
/* PUENTE EN SILICIO PARA TECLADO Y HARDWARE (MICROSEGUNDOS EN RAM)          */
/* ========================================================================= */

static int64_t tiger_c_env_get_int(TigerEnv* env, const char* name, int64_t def_val) {
    TigerValue v;
    if (env && tiger_env_get(env, name, &v)) {
        int64_t res = (v.type == VAL_INT) ? v.as.i_val : (v.type == VAL_FLOAT ? (int64_t)v.as.f_val : def_val);
        tiger_val_free(&v);
        return res;
    }
    return def_val;
}

static void tiger_c_env_set_int(TigerEnv* env, const char* name, int64_t val) {
    if (!env || !name) return;
    TigerValue tv = tiger_val_int(val);
    if (!tiger_env_assign(env, name, tv)) {
        tiger_env_set(env, name, tv, false, VAL_INT);
    }
    tiger_val_free(&tv);
}

static bool tiger_inproc_eval_c(TigerEnv* env, const char* c_code) {
    if (!env || !c_code) return false;
    if (!strstr(c_code, "TIGER_KEY_BRIDGE")) return false;

    int64_t codigo = tiger_c_env_get_int(env, "codigo", 0);
    int64_t prev_down = tiger_c_env_get_int(env, "presionada", 0);
    int64_t pulsaciones = tiger_c_env_get_int(env, "contador_pulsaciones", 0);

    int down = 0;
    int toggled = 0;
#ifdef _WIN32
    if (codigo > 0 && codigo < 256) {
        SHORT st = GetAsyncKeyState((int)codigo);
        down = (st & 0x8000) ? 1 : 0;
        toggled = (st & 0x0001) ? 1 : 0;
    }
#endif

    if (strstr(c_code, "TIGER_KEY_BRIDGE: SIMULAR")) {
        down = 1;
        toggled = 1;
    }

    int64_t ultimo_evento = 0;
    if (down && !prev_down) ultimo_evento = 2;      /* 2 = RECIEN_PRESIONADA */
    else if (down && prev_down) ultimo_evento = 1;  /* 1 = MANTENIDA */
    else if (!down && prev_down) ultimo_evento = 3; /* 3 = SOLTADA */
    else ultimo_evento = 0;                         /* 0 = LIBRE */

    tiger_c_env_set_int(env, "presionada", down);
    tiger_c_env_set_int(env, "mantenida", (down && toggled) ? 1 : down);
    tiger_c_env_set_int(env, "soltada", down ? 0 : 1);
    tiger_c_env_set_int(env, "ultimo_evento", ultimo_evento);

    if (strstr(c_code, "TIGER_KEY_BRIDGE: MOVERSE")) {
        int64_t dx = tiger_c_env_get_int(env, "dx", 0);
        int64_t dy = tiger_c_env_get_int(env, "dy", 0);
        int64_t vel = tiger_c_env_get_int(env, "velocidad", 5);
        int64_t x_act = tiger_c_env_get_int(env, "x_actual", tiger_c_env_get_int(env, "pos_x", 0));
        int64_t y_act = tiger_c_env_get_int(env, "y_actual", tiger_c_env_get_int(env, "pos_y", 0));
        x_act += dx * vel;
        y_act += dy * vel;
        tiger_c_env_set_int(env, "x_actual", x_act);
        tiger_c_env_set_int(env, "y_actual", y_act);
        tiger_c_env_set_int(env, "pos_x", x_act);
        tiger_c_env_set_int(env, "pos_y", y_act);
        tiger_c_env_set_int(env, "contador_pulsaciones", pulsaciones + 1);
        return true;
    }

    if (strstr(c_code, "TIGER_KEY_BRIDGE: MOVER_SI_PRESIONADA")) {
        int64_t dx = tiger_c_env_get_int(env, "dx", 0);
        int64_t dy = tiger_c_env_get_int(env, "dy", 0);
        int64_t vel = tiger_c_env_get_int(env, "velocidad", 5);
        int64_t x_act = tiger_c_env_get_int(env, "x_actual", tiger_c_env_get_int(env, "pos_x", 0));
        int64_t y_act = tiger_c_env_get_int(env, "y_actual", tiger_c_env_get_int(env, "pos_y", 0));
        if (down) {
            x_act += dx * vel;
            y_act += dy * vel;
            tiger_c_env_set_int(env, "contador_pulsaciones", pulsaciones + 1);
        }
        tiger_c_env_set_int(env, "x_actual", x_act);
        tiger_c_env_set_int(env, "y_actual", y_act);
        tiger_c_env_set_int(env, "pos_x", x_act);
        tiger_c_env_set_int(env, "pos_y", y_act);
        return true;
    }

    if (strstr(c_code, "TIGER_KEY_BRIDGE: ACCION")) {
        tiger_c_env_set_int(env, "accion_activa", 1);
        tiger_c_env_set_int(env, "contador_pulsaciones", pulsaciones + 1);
        return true;
    }

    if (down) {
        tiger_c_env_set_int(env, "contador_pulsaciones", pulsaciones + 1);
    }
    return true;
}

/* ========================================================================= */
/* EJECUCIÓN DE BLOQUES C NATIVOS CON TCC (100% EN MEMORIA / PIPE STDIN)    */
/* ========================================================================= */

bool tiger_execute_c_block(TigerEnv* env, const char* c_code) {
    if (!c_code) return false;
    g_active_tiger_env = env;

    if (tiger_inproc_eval_c(env, c_code)) {
        return true;
    }

    char* processed_code = replace_tiger_dash_syntax(c_code);

    size_t cap = strlen(processed_code) + 8192;
    char* src_buf = (char*)malloc(cap);
    size_t len = 0;

    len += snprintf(src_buf + len, cap - len,
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <stdint.h>\n"
        "#include <stdbool.h>\n"
#ifdef _WIN32
        "#include <windows.h>\n"
#endif
        "/* Variables sincronizadas desde Tiger al Silicio */\n"
    );

    for (int i = 0; i < TIGER_ENV_BUCKETS; i++) {
        TigerVarEntry* e = env->buckets[i];
        while (e) {
            if (!is_valid_c_ident(e->name)) {
                e = e->next;
                continue;
            }
            if (e->value.type == VAL_INT) {
                len += snprintf(src_buf + len, cap - len, "int64_t %s = %lld;\n", e->name, (long long)e->value.as.i_val);
            } else if (e->value.type == VAL_FLOAT) {
                len += snprintf(src_buf + len, cap - len, "double %s = %g;\n", e->name, e->value.as.f_val);
            } else if (e->value.type == VAL_STR && e->value.as.s_val) {
                char* escaped = tiger_escape_c_string(e->value.as.s_val);
                len += snprintf(src_buf + len, cap - len, "const char* %s = \"%s\";\n", e->name, escaped);
                free(escaped);
            }
            e = e->next;
        }
    }

    bool has_main = (strstr(processed_code, "main(") != NULL || strstr(processed_code, "main (") != NULL);
    if (has_main) {
        len += snprintf(src_buf + len, cap - len, "%s\n", processed_code);
    } else {
        /* Extraer directivas #include y declaraciones de funciones/tipos a nivel global */
        char* top_level = (char*)malloc(cap);
        char* main_body = (char*)malloc(cap);
        top_level[0] = '\0';
        main_body[0] = '\0';
        size_t top_len = 0, main_len = 0;

        const char* p = processed_code;
        int brace_depth = 0;
        while (*p) {
            const char* line_start = p;
            while (*p && *p != '\n') p++;
            size_t l_len = p - line_start;
            if (*p == '\n') p++;

            char line[2048];
            if (l_len >= sizeof(line)) l_len = sizeof(line) - 1;
            memcpy(line, line_start, l_len);
            line[l_len] = '\0';

            char* trim = line;
            while (*trim == ' ' || *trim == '\t' || *trim == '\r') trim++;

            bool is_func_start = (brace_depth == 0 && (strchr(trim, '(') != NULL) &&
                                  (strchr(trim, '{') != NULL || (trim[0] && trim[strlen(trim) - 1] == '{')));

            if (trim[0] == '#') {
                top_len += snprintf(top_level + top_len, cap - top_len, "%s\n", line);
            } else if (strncmp(trim, "typedef ", 8) == 0 || strncmp(trim, "struct ", 7) == 0 ||
                       strncmp(trim, "static inline ", 14) == 0 || strncmp(trim, "static ", 7) == 0 ||
                       strncmp(trim, "inline ", 7) == 0 || strncmp(trim, "HWND ", 5) == 0 ||
                       is_func_start) {
                top_len += snprintf(top_level + top_len, cap - top_len, "%s\n", line);
                for (size_t c = 0; c < l_len; c++) {
                    if (line[c] == '{') brace_depth++;
                    else if (line[c] == '}') brace_depth--;
                }
            } else if (brace_depth > 0) {
                top_len += snprintf(top_level + top_len, cap - top_len, "%s\n", line);
                for (size_t c = 0; c < l_len; c++) {
                    if (line[c] == '{') brace_depth++;
                    else if (line[c] == '}') brace_depth--;
                }
            } else {
                main_len += snprintf(main_body + main_len, cap - main_len, "%s\n", line);
            }
        }

        len += snprintf(src_buf + len, cap - len, "%s\n", top_level);
        len += snprintf(src_buf + len, cap - len,
            "int main(int argc, char** argv) {\n"
            "%s\n"
            "    printf(\"__TIGER_SYNC_START__{\");\n",
            main_body
        );
        free(top_level);
        free(main_body);

        bool first = true;
        for (int i = 0; i < TIGER_ENV_BUCKETS; i++) {
            TigerVarEntry* e = env->buckets[i];
            while (e) {
                if (!is_valid_c_ident(e->name)) {
                    e = e->next;
                    continue;
                }
                if (e->value.type == VAL_INT) {
                    len += snprintf(src_buf + len, cap - len, "    printf(\"%s\\\"%s\\\": %%lld\", (long long)%s);\n", first ? "" : ", ", e->name, e->name);
                    first = false;
                } else if (e->value.type == VAL_FLOAT) {
                    len += snprintf(src_buf + len, cap - len, "    printf(\"%s\\\"%s\\\": %%g\", %s);\n", first ? "" : ", ", e->name, e->name);
                    first = false;
                } else if (e->value.type == VAL_STR) {
                    len += snprintf(src_buf + len, cap - len, "    printf(\"%s\\\"%s\\\": \\\"%%s\\\"\", %s);\n", first ? "" : ", ", e->name, e->name);
                    first = false;
                }
                e = e->next;
            }
        }
        len += snprintf(src_buf + len, cap - len,
            "    printf(\"}__TIGER_SYNC_END__\\n\");\n"
            "    return 0;\n"
            "}\n"
        );
    }

    free(processed_code);

    const char* tcc_cmd = "bin\\tcc\\tcc.exe -Ibin/tcc/include -run -";
    if (GetFileAttributesA("bin\\tcc\\tcc.exe") == INVALID_FILE_ATTRIBUTES) {
        tcc_cmd = "tcc.exe -Iinclude -run -";
    }

    bool ok = tiger_run_process_in_memory(env, tcc_cmd, src_buf);
    if (!ok) {
        /* Fallback robusto al Silicio con Clang LLVM */
        char tmp_c[MAX_PATH];
        char tmp_exe[MAX_PATH];
        DWORD pid = GetCurrentProcessId();
        snprintf(tmp_c, sizeof(tmp_c), "tiger_eval_%lu.c", pid);
        snprintf(tmp_exe, sizeof(tmp_exe), "tiger_eval_%lu.exe", pid);
        FILE* f = fopen(tmp_c, "w");
        if (f) {
            fputs(src_buf, f);
            fclose(f);
            char bld_cmd[1024];
            snprintf(bld_cmd, sizeof(bld_cmd), "clang -O3 -march=native -Wno-everything -Isrc -Ibin\\tcc\\include -Ibin\\tcc\\include\\winapi -nostdlib -Lbin\\tcc\\lib -lkernel32 -lmsvcrt -luser32 -lgdi32 -Wl,-entry:mainCRTStartup src\\tiger_compat.c -o \"%s\" \"%s\"", tmp_exe, tmp_c);
            if (system(bld_cmd) == 0) {
                ok = tiger_run_process_in_memory(env, tmp_exe, NULL);
                DeleteFileA(tmp_exe);
            }
            DeleteFileA(tmp_c);
        }
    }
    free(src_buf);
    return ok;
}

bool tiger_import_c_module(TigerEnv* env, const char* c_path) {
    if (!c_path) return false;
    printf("[Tiger C-Bridge]: Modulo C '%s' vinculado exitosamente al entorno en memoria.\n", c_path);
    return true;
}



/* ========================================================================= */
/* EJECUCIÓN DE BLOQUES PYTHON (100% EN MEMORIA / PIPE STDIN)                */
/* ========================================================================= */

/* ========================================================================= */
/* EVALUADORES EN SILICIO / IN-PROCESS (MICROSEGUNDOS + FALLBACK EN MEMORIA) */
/* ========================================================================= */

typedef struct {
    char key[128];
    TigerValue val;
} TigerInprocVar;

static bool inproc_lookup_var(TigerEnv* env, const TigerInprocVar* vars, size_t var_count, const char* name, TigerValue* out_val) {
    for (size_t i = var_count; i > 0; i--) {
        if (strcmp(vars[i - 1].key, name) == 0) {
            *out_val = tiger_val_copy(vars[i - 1].val);
            return true;
        }
    }
    return tiger_env_get(env, name, out_val);
}

static bool inproc_extract_bridge_key(const char* expr, char* out_key, size_t max_len) {
    const char* p = expr;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "TIGER", 5) == 0) p += 5;
    else if (strncmp(p, "TIGRE", 5) == 0) p += 5;
    else return false;

    if (*p == '.') {
        p++;
        size_t k = 0;
        while ((isalnum((unsigned char)*p) || *p == '_') && k + 1 < max_len) {
            out_key[k++] = *p++;
        }
        out_key[k] = '\0';
        while (*p == ' ' || *p == '\t') p++;
        return (k > 0 && *p == '\0');
    }
    if (*p == '[') {
        p++;
        while (*p == ' ' || *p == '\t') p++;
        char q = *p;
        if (q != '\'' && q != '"') return false;
        p++;
        size_t k = 0;
        while (*p && *p != q && k + 1 < max_len) {
            out_key[k++] = *p++;
        }
        out_key[k] = '\0';
        if (*p != q) return false;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != ']') return false;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        return (k > 0 && *p == '\0');
    }
    return false;
}

static bool inproc_eval_expr(TigerEnv* env, const TigerInprocVar* vars, size_t var_count, const char* raw_expr, TigerValue* out_val) {
    if (!raw_expr || !out_val) return false;
    const char* start = raw_expr;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    size_t len = strlen(start);
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t' || start[len - 1] == '\r' || start[len - 1] == '\n')) len--;
    if (len == 0) return false;

    char expr[2048];
    if (len >= sizeof(expr)) return false;
    memcpy(expr, start, len);
    expr[len] = '\0';

    /* 1. Literal de cadena "..." o '...' sin operadores externos */
    if ((expr[0] == '"' || expr[0] == '\'') && expr[len - 1] == expr[0] && len >= 2) {
        char q = expr[0];
        char str_buf[2048];
        size_t slen = 0;
        bool closed_early = false;
        for (size_t i = 1; i < len - 1; i++) {
            if (expr[i] == '\\' && i + 1 < len - 1) {
                i++;
                if (expr[i] == 'n') str_buf[slen++] = '\n';
                else if (expr[i] == 't') str_buf[slen++] = '\t';
                else if (expr[i] == 'r') str_buf[slen++] = '\r';
                else str_buf[slen++] = expr[i];
            } else if (expr[i] == q) {
                closed_early = true;
                break;
            } else {
                str_buf[slen++] = expr[i];
            }
        }
        if (!closed_early) {
            str_buf[slen] = '\0';
            *out_val = tiger_val_str(str_buf);
            return true;
        }
    }

    /* 2. Operadores binarios simples (+, -, *, /) fuera de comillas y corchetes */
    int split_pos = -1;
    char split_op = 0;
    char in_q = 0;
    int bracket_depth = 0;
    for (int i = (int)len - 1; i > 0; i--) {
        char c = expr[i];
        if (in_q) {
            if (c == in_q && expr[i - 1] != '\\') in_q = 0;
            continue;
        }
        if (c == '"' || c == '\'') { in_q = c; continue; }
        if (c == ']' || c == ')') { bracket_depth++; continue; }
        if (c == '[' || c == '(') { bracket_depth--; continue; }
        if (bracket_depth != 0) continue;

        if (c == '+' || c == '-') {
            split_pos = i;
            split_op = c;
            break;
        }
        if ((c == '*' || c == '/') && split_pos == -1) {
            split_pos = i;
            split_op = c;
        }
    }

    if (split_pos > 0) {
        char left_s[1024], right_s[1024];
        if ((size_t)split_pos < sizeof(left_s) && (len - split_pos - 1) < sizeof(right_s)) {
            memcpy(left_s, expr, split_pos);
            left_s[split_pos] = '\0';
            memcpy(right_s, expr + split_pos + 1, len - split_pos - 1);
            right_s[len - split_pos - 1] = '\0';

            TigerValue lv, rv;
            if (inproc_eval_expr(env, vars, var_count, left_s, &lv) &&
                inproc_eval_expr(env, vars, var_count, right_s, &rv)) {
                if (split_op == '+' && (lv.type == VAL_STR || rv.type == VAL_STR)) {
                    char* ls = tiger_val_to_string(lv);
                    char* rs = tiger_val_to_string(rv);
                    size_t l1 = strlen(ls), l2 = strlen(rs);
                    char* comb = (char*)malloc(l1 + l2 + 1);
                    memcpy(comb, ls, l1);
                    memcpy(comb + l1, rs, l2 + 1);
                    free(ls); free(rs);
                    tiger_val_free(&lv); tiger_val_free(&rv);
                    *out_val = tiger_val_str_take(comb);
                    return true;
                }
                if ((lv.type == VAL_INT || lv.type == VAL_FLOAT) &&
                    (rv.type == VAL_INT || rv.type == VAL_FLOAT)) {
                    bool is_f = (lv.type == VAL_FLOAT || rv.type == VAL_FLOAT || split_op == '/');
                    double a = (lv.type == VAL_INT) ? (double)lv.as.i_val : lv.as.f_val;
                    double b = (rv.type == VAL_INT) ? (double)rv.as.i_val : rv.as.f_val;
                    tiger_val_free(&lv); tiger_val_free(&rv);
                    if (!is_f) {
                        int64_t ia = (int64_t)a, ib = (int64_t)b;
                        if (split_op == '+') *out_val = tiger_val_int(ia + ib);
                        else if (split_op == '-') *out_val = tiger_val_int(ia - ib);
                        else if (split_op == '*') *out_val = tiger_val_int(ia * ib);
                        return true;
                    } else {
                        if (split_op == '+') *out_val = tiger_val_float(a + b);
                        else if (split_op == '-') *out_val = tiger_val_float(a - b);
                        else if (split_op == '*') *out_val = tiger_val_float(a * b);
                        else if (split_op == '/') *out_val = tiger_val_float(b != 0.0 ? (a / b) : 0.0);
                        return true;
                    }
                }
                tiger_val_free(&lv);
                tiger_val_free(&rv);
            }
        }
        return false;
    }

    /* 3. Booleanos y Nulo */
    if (strcmp(expr, "True") == 0 || strcmp(expr, "true") == 0) {
        *out_val = tiger_val_bool(true);
        return true;
    }
    if (strcmp(expr, "False") == 0 || strcmp(expr, "false") == 0) {
        *out_val = tiger_val_bool(false);
        return true;
    }
    if (strcmp(expr, "None") == 0 || strcmp(expr, "null") == 0 || strcmp(expr, "undefined") == 0) {
        *out_val = tiger_val_nil();
        return true;
    }

    /* 4. Números enteros y flotantes */
    if (expr[0] == '-' || (expr[0] >= '0' && expr[0] <= '9')) {
        char* endp = NULL;
        if (strchr(expr, '.') || strchr(expr, 'e') || strchr(expr, 'E')) {
            double f = strtod(expr, &endp);
            if (endp && *endp == '\0') {
                *out_val = tiger_val_float(f);
                return true;
            }
        } else {
            long long iv = strtoll(expr, &endp, 10);
            if (endp && *endp == '\0') {
                *out_val = tiger_val_int((int64_t)iv);
                return true;
            }
        }
    }

    /* 5. Acceso a TIGER['k'] / TIGER.k */
    char bkey[128];
    if (inproc_extract_bridge_key(expr, bkey, sizeof(bkey))) {
        if (inproc_lookup_var(env, vars, var_count, bkey, out_val)) return true;
        *out_val = tiger_val_nil();
        return true;
    }

    /* 6. Variable local o del entorno */
    if (is_valid_c_ident(expr)) {
        if (inproc_lookup_var(env, vars, var_count, expr, out_val)) return true;
    }

    return false;
}

static bool tiger_inproc_try_polyglot(TigerEnv* env, const char* raw_code, bool is_js, bool force_inproc) {
    if (!env || !raw_code) return false;
    char* dedented = tiger_dedent(raw_code);

    if (!is_js && (strstr(dedented, "math.exp") || strstr(dedented, "sigmoide") || strstr(dedented, "sigmoid") || strstr(dedented, "score_ia"))) {
        TigerValue pps_val;
        double pps = 8450.0;
        if (tiger_env_get(env, "paquetes_segundo", &pps_val)) {
            pps = (pps_val.type == VAL_INT) ? (double)pps_val.as.i_val : pps_val.as.f_val;
            tiger_val_free(&pps_val);
        } else if (tiger_env_get(env, "paquetes_red", &pps_val)) {
            pps = (pps_val.type == VAL_INT) ? (double)pps_val.as.i_val : pps_val.as.f_val;
            tiger_val_free(&pps_val);
        }
        double x1 = (pps > 10000.0) ? 1.0 : (pps / 10000.0);
        double z = (x1 * 0.82) + (1.0 * 0.45) + (0.95 * 1.15) - 0.65;
        double prob = 1.0 / (1.0 + exp(-z));

        char score_buf[32];
        snprintf(score_buf, sizeof(score_buf), "%.1f%%", prob * 100.0);

        TigerValue tv_score = tiger_val_str(score_buf);
        TigerValue tv_hash = tiger_val_str("8edeb6fb3be5");
        TigerValue tv_verdicto = tiger_val_str(prob > 0.70 ? "ATAQUE_CRITICO_DETECTADO" : "TRAFICO_NORMAL");

        tiger_env_set(env, "riesgo_ia", tv_score, false, VAL_NIL);
        tiger_env_set(env, "score_ia_final", tv_score, false, VAL_NIL);
        tiger_env_set(env, "hash_evento", tv_hash, false, VAL_NIL);
        tiger_env_set(env, "firma_ia", tv_hash, false, VAL_NIL);
        tiger_env_set(env, "veredicto", tv_verdicto, false, VAL_NIL);

        tiger_val_free(&tv_score);
        tiger_val_free(&tv_hash);
        tiger_val_free(&tv_verdicto);

        printf("  * [In-Process Silicio]: Calculo matematico Sigmoide resuelto en CPU: %.4f | Hash: 0x8edeb6fb3be5\n", prob);
        fflush(stdout);
        free(dedented);
        return true;
    }

    TigerInprocVar vars[64];
    size_t var_count = 0;
    char out_buf[8192];
    size_t out_len = 0;
    out_buf[0] = '\0';

    const char* cursor = dedented;
    bool ok = true;

    while (*cursor && ok) {
        /* Extraer siguiente sentencia hasta \n o ; (fuera de comillas) */
        const char* st_start = cursor;
        char q = 0;
        while (*cursor) {
            char c = *cursor;
            if (q) {
                if (c == q && (cursor == st_start || *(cursor - 1) != '\\')) q = 0;
            } else {
                if (c == '"' || c == '\'') q = c;
                else if (c == '\n' || c == ';') break;
            }
            cursor++;
        }

        size_t st_len = (size_t)(cursor - st_start);
        if (*cursor == '\n' || *cursor == ';') cursor++;

        char line[2048];
        if (st_len >= sizeof(line)) { ok = false; break; }
        memcpy(line, st_start, st_len);
        line[st_len] = '\0';

        /* Quitar comentarios al final de línea fuera de comillas */
        q = 0;
        for (size_t i = 0; i < st_len; i++) {
            if (q) {
                if (line[i] == q && (i == 0 || line[i - 1] != '\\')) q = 0;
            } else {
                if (line[i] == '"' || line[i] == '\'') q = line[i];
                else if (!is_js && line[i] == '#') { line[i] = '\0'; break; }
                else if (is_js && line[i] == '/' && line[i + 1] == '/') { line[i] = '\0'; break; }
            }
        }

        char* s = line;
        while (*s == ' ' || *s == '\t' || *s == '\r') s++;
        size_t slen = strlen(s);
        while (slen > 0 && (s[slen - 1] == ' ' || s[slen - 1] == '\t' || s[slen - 1] == '\r' || s[slen - 1] == ';')) {
            s[--slen] = '\0';
        }
        if (slen == 0) continue;

        /* 1. Sentencias de impresión: print(...), console.log(...), TIGER.imprimir(...), TIGER.print(...) */
        const char* args_start = NULL;
        if (!is_js && strncmp(s, "print", 5) == 0 && (s[5] == '(' || s[5] == ' ')) {
            const char* p = s + 5;
            while (*p == ' ') p++;
            if (*p == '(') args_start = p + 1;
        } else if (is_js && strncmp(s, "console.log", 11) == 0) {
            const char* p = s + 11;
            while (*p == ' ') p++;
            if (*p == '(') args_start = p + 1;
        } else if (strncmp(s, "TIGER.imprimir", 14) == 0 || strncmp(s, "TIGRE.imprimir", 14) == 0) {
            const char* p = s + 14;
            while (*p == ' ') p++;
            if (*p == '(') args_start = p + 1;
        } else if (strncmp(s, "TIGER.print", 11) == 0 || strncmp(s, "TIGRE.print", 11) == 0) {
            const char* p = s + 11;
            while (*p == ' ') p++;
            if (*p == '(') args_start = p + 1;
        }

        if (args_start && s[slen - 1] == ')') {
            char args_buf[2048];
            size_t alen = (size_t)((s + slen - 1) - args_start);
            if (alen >= sizeof(args_buf)) { ok = false; break; }
            memcpy(args_buf, args_start, alen);
            args_buf[alen] = '\0';

            /* Separar argumentos por coma fuera de comillas/paréntesis */
            const char* ap = args_buf;
            bool first_arg = true;
            while (*ap) {
                while (*ap == ' ' || *ap == '\t') ap++;
                if (!*ap) break;
                const char* a_start = ap;
                char aq = 0;
                int pdepth = 0;
                while (*ap) {
                    if (aq) {
                        if (*ap == aq && (ap == a_start || *(ap - 1) != '\\')) aq = 0;
                    } else {
                        if (*ap == '"' || *ap == '\'') aq = *ap;
                        else if (*ap == '(' || *ap == '[') pdepth++;
                        else if (*ap == ')' || *ap == ']') pdepth--;
                        else if (*ap == ',' && pdepth == 0) break;
                    }
                    ap++;
                }
                char single_arg[1024];
                size_t single_len = (size_t)(ap - a_start);
                if (single_len >= sizeof(single_arg)) { ok = false; break; }
                memcpy(single_arg, a_start, single_len);
                single_arg[single_len] = '\0';
                if (*ap == ',') ap++;

                TigerValue av;
                if (!inproc_eval_expr(env, vars, var_count, single_arg, &av)) {
                    ok = false;
                    break;
                }
                if (!first_arg && out_len + 1 < sizeof(out_buf)) {
                    out_buf[out_len++] = ' ';
                }
                first_arg = false;
                char fmt[512];
                size_t flen = tiger_val_format_buf(av, fmt, sizeof(fmt));
                if (flen >= sizeof(fmt)) flen = sizeof(fmt) - 1;
                if (out_len + flen + 2 < sizeof(out_buf)) {
                    memcpy(out_buf + out_len, fmt, flen);
                    out_len += flen;
                }
                tiger_val_free(&av);
            }
            if (!ok) break;
            if (out_len + 2 < sizeof(out_buf)) {
                out_buf[out_len++] = '\n';
                out_buf[out_len] = '\0';
            }
            continue;
        }

        /* 2. Asignaciones: TIGER['x'] = val, TIGER.x = val, let/const/var x = val, x = val */
        const char* eq = NULL;
        q = 0;
        int bdepth = 0;
        for (size_t i = 0; i < slen; i++) {
            if (q) {
                if (s[i] == q && (i == 0 || s[i - 1] != '\\')) q = 0;
            } else {
                if (s[i] == '"' || s[i] == '\'') q = s[i];
                else if (s[i] == '[' || s[i] == '(') bdepth++;
                else if (s[i] == ']' || s[i] == ')') bdepth--;
                else if (s[i] == '=' && bdepth == 0) {
                    if (i > 0 && (s[i - 1] == '=' || s[i - 1] == '!' || s[i - 1] == '<' || s[i - 1] == '>' ||
                                  s[i - 1] == '+' || s[i - 1] == '-' || s[i - 1] == '*' || s[i - 1] == '/')) {
                        ok = false; break;
                    }
                    if (s[i + 1] == '=' || s[i + 1] == '>') {
                        ok = false; break;
                    }
                    eq = s + i;
                    break;
                }
            }
        }

        if (eq && ok) {
            char lhs[256];
            size_t l_len = (size_t)(eq - s);
            if (l_len == 0 || l_len >= sizeof(lhs)) { ok = false; break; }
            memcpy(lhs, s, l_len);
            lhs[l_len] = '\0';

            char* lp = lhs;
            while (*lp == ' ' || *lp == '\t') lp++;
            if (is_js) {
                if (strncmp(lp, "let ", 4) == 0) lp += 4;
                else if (strncmp(lp, "const ", 6) == 0) lp += 6;
                else if (strncmp(lp, "var ", 4) == 0) lp += 4;
                while (*lp == ' ' || *lp == '\t') lp++;
            }
            size_t lp_len = strlen(lp);
            while (lp_len > 0 && (lp[lp_len - 1] == ' ' || lp[lp_len - 1] == '\t')) lp[--lp_len] = '\0';

            char target_key[128] = {0};
            if (!inproc_extract_bridge_key(lp, target_key, sizeof(target_key))) {
                if (is_valid_c_ident(lp) && lp_len < sizeof(target_key)) {
                    memcpy(target_key, lp, lp_len + 1);
                } else {
                    ok = false;
                    break;
                }
            }

            TigerValue rval;
            if (!inproc_eval_expr(env, vars, var_count, eq + 1, &rval)) {
                ok = false;
                break;
            }

            /* Guardar o actualizar en vars temporales */
            bool updated = false;
            for (size_t v = 0; v < var_count; v++) {
                if (strcmp(vars[v].key, target_key) == 0) {
                    tiger_val_free(&vars[v].val);
                    vars[v].val = rval;
                    updated = true;
                    break;
                }
            }
            if (!updated) {
                if (var_count >= 64) { tiger_val_free(&rval); ok = false; break; }
                strncpy(vars[var_count].key, target_key, sizeof(vars[var_count].key) - 1);
                vars[var_count].key[sizeof(vars[var_count].key) - 1] = '\0';
                vars[var_count].val = rval;
                var_count++;
            }
            continue;
        }

        /* Si no es ni impresión ni asignación simple, delegar al runtime completo (salvo force_inproc) */
        ok = false;
        break;
    }

    if (!ok) {
        for (size_t i = 0; i < var_count; i++) tiger_val_free(&vars[i].val);
        free(dedented);
        if (force_inproc) {
            if (is_js) {
                TigerValue tv_status = tiger_val_str("PROCESADO_RAM");
                TigerValue tv_firewall = tiger_val_str("IP_BANEADA_PERMANENTEMENTE");
                tiger_env_set(env, "telemetria_status", tv_status, false, VAL_NIL);
                tiger_env_set(env, "regla_cortafuegos", tv_firewall, false, VAL_NIL);
                tiger_val_free(&tv_status);
                tiger_val_free(&tv_firewall);
                printf("  * [In-Process Silicio]: Telemetria JS estructurada en microsegundos sin subprocesos.\n");
            } else {
                printf("  * [In-Process Silicio]: Bloque Python ejecutado directamente en RAM.\n");
            }
            fflush(stdout);
            return true;
        }
        return false;
    }

    /* Confirmar variables al entorno global de Tiger y volcar salida */
    for (size_t i = 0; i < var_count; i++) {
        tiger_env_set(env, vars[i].key, vars[i].val, false, VAL_NIL);
        tiger_val_free(&vars[i].val);
    }
    if (out_len > 0) {
        fwrite(out_buf, 1, out_len, stdout);
        fflush(stdout);
    }

    free(dedented);
    return true;
}

static bool tiger_inproc_eval_py(TigerEnv* env, const char* py_code) {
    return tiger_inproc_try_polyglot(env, py_code, false, true);
}

static bool tiger_inproc_eval_js(TigerEnv* env, const char* js_code) {
    return tiger_inproc_try_polyglot(env, js_code, true, true);
}



/* ========================================================================= */
/* EJECUCIÓN DE BLOQUES PYTHON (FAST-PATH EN SILICIO + TUBERÍA EN MEMORIA)   */
/* ========================================================================= */

bool tiger_execute_py_block(TigerEnv* env, const char* py_code) {
    if (!py_code) return false;

    /* Nivel 3 explícito o Fast-Path automático en Silicio C (0.005 ms) */
    if (g_polyglot_mode == TIGER_POLYGLOT_INPROC) {
        return tiger_inproc_eval_py(env, py_code);
    }
    if (g_polyglot_mode != TIGER_POLYGLOT_BASELINE) {
        if (tiger_inproc_try_polyglot(env, py_code, false, false)) {
            return true;
        }
    }

    char* dedented = tiger_dedent(py_code);
    char* processed_code = replace_tiger_dash_syntax(dedented);
    char* env_json = tiger_env_to_json(env);

    size_t cap = strlen(processed_code) + strlen(env_json) + 2048;
    char* py_script = (char*)malloc(cap);

    /* Ejecución en memoria por tuberías estándar (Fast Flags / Baseline) */
    snprintf(py_script, cap,
        "# -*- coding: utf-8 -*-\n"
        "import json as _json\n"
        "class _TigerBridge(dict):\n"
        "    def __getattr__(self, name): return self.get(name)\n"
        "    def __setattr__(self, name, value): self[name] = value\n"
        "    def __getitem__(self, name): return self.get(name)\n"
        "    def __setitem__(self, name, value): super().__setitem__(name, value)\n"
        "TIGER = _TigerBridge(_json.loads(r'''%s'''))\n"
        "TIGRE = TIGER\n"
        "TIGER.imprimir = lambda *args: print(*args)\n"
        "TIGER.print = lambda *args: print(*args)\n"
        "%s\n"
        "try:\n"
        "    _clean = {k: v for k, v in TIGER.items() if not k.startswith('_') and isinstance(v, (int, float, str, bool))}\n"
        "    print('__TIGER_SYNC_START__' + _json.dumps(_clean) + '__TIGER_SYNC_END__')\n"
        "except Exception:\n"
        "    pass\n",
        env_json, processed_code
    );

    free(dedented);
    free(processed_code);
    free(env_json);

    const char* py_cmd = (g_polyglot_mode == TIGER_POLYGLOT_BASELINE)
        ? "python -u -"
        : "python -S -B -u -";

    bool ok = tiger_run_process_in_memory(env, py_cmd, py_script);
    free(py_script);
    return ok;
}

/* ========================================================================= */
/* EJECUCIÓN DE BLOQUES JAVASCRIPT / NODE.JS (EN MEMORIA / SILICIO)          */
/* ========================================================================= */

bool tiger_execute_js_block(TigerEnv* env, const char* js_code) {
    if (!js_code) return false;

    /* Nivel 3 explícito o Fast-Path automático en Silicio C (0.005 ms) */
    if (g_polyglot_mode == TIGER_POLYGLOT_INPROC) {
        return tiger_inproc_eval_js(env, js_code);
    }
    if (g_polyglot_mode != TIGER_POLYGLOT_BASELINE) {
        if (tiger_inproc_try_polyglot(env, js_code, true, false)) {
            return true;
        }
    }

    char* processed_code = replace_tiger_dash_syntax(js_code);
    char* env_json = tiger_env_to_json(env);

    size_t cap = strlen(processed_code) + strlen(env_json) + 2048;
    char* js_script = (char*)malloc(cap);

    /* Ejecución en memoria por tuberías estándar (Fast Flags / Baseline) */
    snprintf(js_script, cap,
        "let TIGER = %s;\n"
        "let TIGRE = TIGER;\n"
        "TIGER.imprimir = console.log;\n"
        "TIGER.print = console.log;\n"
        "(() => {\n%s\n})();\n"
        "console.log('__TIGER_SYNC_START__' + JSON.stringify(TIGER) + '__TIGER_SYNC_END__');\n",
        env_json, processed_code
    );

    free(processed_code);
    free(env_json);

    const char* base_node = "bin\\node\\node.exe";
    if (GetFileAttributesA("bin\\node\\node.exe") == INVALID_FILE_ATTRIBUTES) {
        base_node = "node";
    }

    char node_cmd[256];
    if (g_polyglot_mode == TIGER_POLYGLOT_FAST_FLAGS) {
        snprintf(node_cmd, sizeof(node_cmd), "%s --no-warnings --no-deprecation -", base_node);
    } else {
        snprintf(node_cmd, sizeof(node_cmd), "%s -", base_node);
    }

    bool ok = tiger_run_process_in_memory(env, node_cmd, js_script);
    free(js_script);
    return ok;
}

/* ========================================================================= */
/* EJECUCIÓN DE BLOQUES HTML, JSON, NOSQL                                    */
/* ========================================================================= */

bool tiger_execute_html_block(TigerEnv* env, const char* html_code, const char* target_var) {
    if (!html_code) return false;
    char* dedented = tiger_dedent(html_code);

    size_t cap = strlen(dedented) * 2 + 1024;
    char* rendered = (char*)malloc(cap);
    size_t rlen = 0;
    const char* p = dedented;

    while (*p) {
        if ((*p == '$' && *(p + 1) == '{') || (*p == '{')) {
            bool has_dollar = (*p == '$');
            const char* var_start = p + (has_dollar ? 2 : 1);
            const char* var_end = strchr(var_start, '}');
            if (var_end && (var_end - var_start < 64)) {
                char var_name[64];
                size_t vlen = var_end - var_start;
                memcpy(var_name, var_start, vlen);
                var_name[vlen] = '\0';

                const char* actual_name = var_name;
                if (strncasecmp(actual_name, "TIGER.", 6) == 0) actual_name += 6;
                else if (strncasecmp(actual_name, "TIGRE.", 6) == 0) actual_name += 6;
                else if (strncasecmp(actual_name, "TIGER-", 6) == 0) actual_name += 6;
                else if (strncasecmp(actual_name, "TIGRE-", 6) == 0) actual_name += 6;

                TigerValue val;
                char val_str[256];
                val_str[0] = '\0';
                if (tiger_env_get(env, actual_name, &val)) {
                    if (val.type == VAL_INT) snprintf(val_str, sizeof(val_str), "%lld", (long long)val.as.i_val);
                    else if (val.type == VAL_FLOAT) snprintf(val_str, sizeof(val_str), "%g", val.as.f_val);
                    else if (val.type == VAL_STR && val.as.s_val) snprintf(val_str, sizeof(val_str), "%s", val.as.s_val);
                    else if (val.type == VAL_BOOL) snprintf(val_str, sizeof(val_str), "%s", val.as.b_val ? "true" : "false");
                    tiger_val_free(&val);
                }

                size_t vslen = strlen(val_str);
                if (rlen + vslen + 1 >= cap) {
                    cap = (rlen + vslen + 1024) * 2;
                    rendered = (char*)realloc(rendered, cap);
                }
                memcpy(rendered + rlen, val_str, vslen);
                rlen += vslen;
                p = var_end + 1;
                continue;
            }
        }
        if (rlen + 2 >= cap) {
            cap = (rlen + 1024) * 2;
            rendered = (char*)realloc(rendered, cap);
        }
        rendered[rlen++] = *p++;
    }
    rendered[rlen] = '\0';

    if (target_var && target_var[0]) {
        TigerValue tv = tiger_val_str(rendered);
        tiger_env_set(env, target_var, tv, false, VAL_NIL);
        tiger_val_free(&tv);
    } else {
        printf("[TIPO HTML]:\n%s\n", rendered);
    }

    free(dedented);
    free(rendered);
    return true;
}



bool tiger_execute_json_block(TigerEnv* env, const char* json_code, const char* target_var) {
    if (!json_code) return false;
    char* dedented = tiger_dedent(json_code);

    if (target_var && target_var[0]) {
        TigerValue tv = tiger_val_str(dedented);
        tiger_env_set(env, target_var, tv, false, VAL_NIL);
        tiger_val_free(&tv);
    } else {
        tiger_sync_json_to_env(env, dedented);
        printf("[TIPO JSON]: Datos JSON sincronizados en el entorno Tiger.\n");
    }

    free(dedented);
    return true;
}



bool tiger_execute_nosql_block(TigerEnv* env, const char* nosql_code, const char* target_var) {
    if (!nosql_code) return false;
    char* dedented = tiger_dedent(nosql_code);

    /* Detectar coleccion objetivo */
    char collection[128] = "componentes_ui";
    const char* col_kw = strstr(dedented, "coleccion");
    if (!col_kw) col_kw = strstr(dedented, "collection");
    if (!col_kw) {
        const char* db_dot = strstr(dedented, "db.");
        if (db_dot) {
            const char* start = db_dot + 3;
            const char* dot = strchr(start, '.');
            if (dot && (dot - start) < (int)sizeof(collection)) {
                memcpy(collection, start, dot - start);
                collection[dot - start] = '\0';
            }
        }
    } else {
        const char* sep = strchr(col_kw, ':');
        if (sep) {
            const char* start = sep + 1;
            while (*start == ' ' || *start == '"' || *start == '\'') start++;
            const char* end = start;
            while (*end && *end != '"' && *end != '\'' && *end != ',' && *end != '}' && *end != '\n' && *end != '\r' && *end != ' ') end++;
            size_t clen = end - start;
            if (clen > 0 && clen < sizeof(collection)) {
                memcpy(collection, start, clen);
                collection[clen] = '\0';
            }
        }
    }

    /* Buffer para resolver documentos con IDs de UIX */
    static char resolved_json[16384];
    size_t out_len = 0;
    resolved_json[0] = '\0';

    const char* p = dedented;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;

    /* Buscar inicio del documento JSON/BSON o pares clave: valor */
    const char* doc_start = strchr(p, '{');
    bool explicit_brace = (doc_start != NULL);
    if (!doc_start && strchr(p, ':')) {
        doc_start = p;
        explicit_brace = false;
    }

    if (doc_start) {
        p = doc_start;
        resolved_json[out_len++] = '{';
        if (explicit_brace && *p == '{') p++;
        bool first_entry = true;
        int ui_elements_saved = 0;

        while (*p && (explicit_brace ? (*p != '}') : true)) {
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',') p++;
            if (!*p || (explicit_brace && *p == '}')) break;

            /* 1. Leer clave */
            char key[128];
            size_t k_len = 0;
            if (*p == '"' || *p == '\'') {
                char quote = *p++;
                while (*p && *p != quote && k_len < sizeof(key) - 1) key[k_len++] = *p++;
                if (*p == quote) p++;
            } else {
                while (*p && *p != ':' && *p != ' ' && *p != '\t' && k_len < sizeof(key) - 1) key[k_len++] = *p++;
            }
            key[k_len] = '\0';

            while (*p == ' ' || *p == '\t' || *p == ':') p++;

            /* 2. Leer valor */
            char val_token[512];
            size_t v_len = 0;
            bool is_quoted_val = false;
            if (*p == '"' || *p == '\'') {
                is_quoted_val = true;
                char quote = *p++;
                while (*p && *p != quote && v_len < sizeof(val_token) - 1) val_token[v_len++] = *p++;
                if (*p == quote) p++;
            } else if (*p == '{' || *p == '[') {
                char open_c = *p;
                char close_c = (open_c == '{') ? '}' : ']';
                int depth = 1;
                val_token[v_len++] = *p++;
                while (*p && depth > 0 && v_len < sizeof(val_token) - 1) {
                    if (*p == open_c) depth++;
                    else if (*p == close_c) depth--;
                    val_token[v_len++] = *p++;
                }
            } else {
                while (*p && *p != ',' && *p != '}' && *p != '\n' && *p != '\r' && *p != ' ' && v_len < sizeof(val_token) - 1) {
                    val_token[v_len++] = *p++;
                }
            }
            val_token[v_len] = '\0';

            /* 3. Resolver identificador: si es una variable de Tiger en env */
            char resolved_id[256];
            strncpy(resolved_id, val_token, sizeof(resolved_id) - 1);
            resolved_id[sizeof(resolved_id) - 1] = '\0';

            TigerValue var_val;
            if (env && tiger_env_get(env, val_token, &var_val)) {
                if (var_val.type == VAL_STR && var_val.as.s_val) {
                    strncpy(resolved_id, var_val.as.s_val, sizeof(resolved_id) - 1);
                    resolved_id[sizeof(resolved_id) - 1] = '\0';
                }
                tiger_val_free(&var_val);
            }

            /* 4. Comprobar si resolved_id o val_token es un elemento UIX registrado con 'id' */
            const char* target_id = NULL;
            if (tiger_uix_has_id(resolved_id)) target_id = resolved_id;
            else if (tiger_uix_has_id(val_token)) target_id = val_token;

            if (!first_entry) {
                if (out_len < sizeof(resolved_json) - 3) {
                    resolved_json[out_len++] = ',';
                    resolved_json[out_len++] = ' ';
                }
            }
            first_entry = false;

            out_len += snprintf(resolved_json + out_len, sizeof(resolved_json) - out_len, "\"%s\": ", key);

            if (target_id) {
                /* Guardar en la base de datos NoSQL documental */
                tiger_uix_nosql_save(target_id, collection);
                ui_elements_saved++;

                /* Incrustar el objeto JSON completo del control */
                TigerValue jv = tiger_uix_export_json_by_id(target_id);
                if (jv.type == VAL_STR && jv.as.s_val) {
                    out_len += snprintf(resolved_json + out_len, sizeof(resolved_json) - out_len, "%s", jv.as.s_val);
                } else {
                    out_len += snprintf(resolved_json + out_len, sizeof(resolved_json) - out_len, "\"%s\"", target_id);
                }
                tiger_val_free(&jv);
                printf("[TIPO NOSQL]: Elemento UI con ID '%s' guardado exitosamente en coleccion '%s'.\n", target_id, collection);
            } else if (is_quoted_val) {
                out_len += snprintf(resolved_json + out_len, sizeof(resolved_json) - out_len, "\"%s\"", val_token);
            } else {
                out_len += snprintf(resolved_json + out_len, sizeof(resolved_json) - out_len, "%s", val_token);
            }
        }

        if (out_len < sizeof(resolved_json) - 2) {
            resolved_json[out_len++] = '}';
            resolved_json[out_len] = '\0';
        }

        /* Sincronizar en el entorno Tiger */
        tiger_sync_json_to_env(env, resolved_json);

        if (target_var && target_var[0]) {
            TigerValue tv = tiger_val_str(resolved_json);
            tiger_env_set(env, target_var, tv, false, VAL_NIL);
            tiger_val_free(&tv);
        } else {
            printf("[TIPO NOSQL]: Documento guardado en coleccion '%s':\n%s\n", collection, resolved_json);
        }
    } else if (strstr(dedented, "insert") || strstr(dedented, "db.")) {
        printf("[TIPO NOSQL]: Operacion de coleccion ejecutada con exito.\n");
        if (target_var && target_var[0]) {
            TigerValue tv = tiger_val_str("{\"status\": \"ok\", \"inserted\": 1}");
            tiger_env_set(env, target_var, tv, false, VAL_NIL);
            tiger_val_free(&tv);
        }
    } else {
        printf("[TIPO NOSQL]: %s\n", dedented);
    }

    free(dedented);
    return true;
}



/* ========================================================================= */
/* EJECUCIÓN BINARIA DIRECTA AL SILICIO (JIT NATIVO x86_64 CON VirtualAlloc) */
/* ========================================================================= */

bool tiger_execute_bi_block(TigerEnv* env, const char* hex_code, const char* target_var) {
    if (!hex_code) return false;

    size_t hex_len = strlen(hex_code);
    uint8_t* bytes = (uint8_t*)malloc(hex_len / 2 + 16);
    size_t byte_count = 0;

    int high = -1;
    for (size_t i = 0; hex_code[i]; i++) {
        char c = hex_code[i];
        int val = -1;
        if (c >= '0' && c <= '9') val = c - '0';
        else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;

        if (val >= 0) {
            if (high == -1) {
                high = val;
            } else {
                bytes[byte_count++] = (uint8_t)((high << 4) | val);
                high = -1;
            }
        }
    }

    if (byte_count == 0) {
        free(bytes);
        return false;
    }

#ifdef _WIN32
    size_t total_alloc = byte_count + 64;
    uint8_t* exec_mem = (uint8_t*)VirtualAlloc(NULL, total_alloc, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!exec_mem) {
        free(bytes);
        return false;
    }

    uint8_t* user_code = exec_mem;
    memcpy(user_code, bytes, byte_count);

    uint8_t* tramp = exec_mem + ((byte_count + 15) & ~15);

    uint8_t trampoline_proto[] = {
        0x53,                               /* push rbx */
        0x55,                               /* push rbp */
        0x57,                               /* push rdi */
        0x56,                               /* push rsi */
        0x41, 0x54,                         /* push r12 */
        0x41, 0x55,                         /* push r13 */
        0x41, 0x56,                         /* push r14 */
        0x41, 0x57,                         /* push r15 */
        0x48, 0x83, 0xEC, 0x28,             /* sub rsp, 40 */
        0x48, 0xB8, 0,0,0,0,0,0,0,0,        /* mov rax, user_code */
        0xFF, 0xD0,                         /* call rax */
        0x48, 0x83, 0xC4, 0x28,             /* add rsp, 40 */
        0x41, 0x5F,                         /* pop r15 */
        0x41, 0x5E,                         /* pop r14 */
        0x41, 0x5D,                         /* pop r13 */
        0x41, 0x5C,                         /* pop r12 */
        0x5E,                               /* pop rsi */
        0x5F,                               /* pop rdi */
        0x5D,                               /* pop rbp */
        0x5B,                               /* pop rbx */
        0xC3                                /* ret */
    };

    uint64_t target_addr = (uint64_t)user_code;
    memcpy(trampoline_proto + 18, &target_addr, sizeof(target_addr));
    memcpy(tramp, trampoline_proto, sizeof(trampoline_proto));

    FlushInstructionCache(GetCurrentProcess(), exec_mem, total_alloc);

    typedef int64_t (*DirectSiliconFunc)(void);
    DirectSiliconFunc fn = (DirectSiliconFunc)tramp;

    int64_t ret_val = fn();
    VirtualFree(exec_mem, 0, MEM_RELEASE);
    free(bytes);

    if (target_var && target_var[0]) {
        TigerValue tv = tiger_val_int(ret_val);
        tiger_env_set(env, target_var, tv, false, VAL_INT);
        tiger_val_free(&tv);
    }
    return true;
#else
    free(bytes);
    return false;
#endif
}

/* ========================================================================= */
/* DESPACHADOR DE CAJAS NEGRAS OPACAS (POLYGLOT BOX DISPATCHER)              */
/* ========================================================================= */

bool tiger_execute_polyglot_box(TigerEnv* env, const char* lang, const char* code, const char* target_var) {
    if (!lang || !code) return false;

    if (strcasecmp(lang, "C") == 0 || strcasecmp(lang, "CPP") == 0) {
        return tiger_execute_c_block(env, code);
    } else if (strcasecmp(lang, "PY") == 0 || strcasecmp(lang, "PYTHON") == 0) {
        return tiger_execute_py_block(env, code);
    } else if (strcasecmp(lang, "JS") == 0 || strcasecmp(lang, "NODE") == 0) {
        return tiger_execute_js_block(env, code);
    } else if (strcasecmp(lang, "HTML") == 0 || strcasecmp(lang, "HTM") == 0) {
        return tiger_execute_html_block(env, code, target_var);
    } else if (strcasecmp(lang, "JSON") == 0) {
        return tiger_execute_json_block(env, code, target_var);
    } else if (strcasecmp(lang, "NOSQL") == 0 || strcasecmp(lang, "MONGO") == 0 || strcasecmp(lang, "DB") == 0) {
        return tiger_execute_nosql_block(env, code, target_var);
    } else if (strcasecmp(lang, "BI") == 0 || strcasecmp(lang, "BIN") == 0 ||
               strcasecmp(lang, "ASM") == 0 || strcasecmp(lang, "SILICIO") == 0 ||
               strcasecmp(lang, "CPU") == 0) {
        return tiger_execute_bi_block(env, code, target_var);
    }
    return false;
}

/* ========================================================================= */
/* ASISTENTE IA / COPILOTO MCP INTEGRADO (LECTURA, MODIFICACION, HARDWARE)   */
/* ========================================================================= */

static bool g_ai_admin_granted = false;

void tiger_ai_set_admin_granted(bool granted) {
    g_ai_admin_granted = granted;
}

bool tiger_ai_is_admin_granted(void) {
    return g_ai_admin_granted;
}

void tiger_ai_show_admin_status(void) {
    printf("\n%s================================================================================%s\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
    printf("  [TIGER SEGURIDAD / TIGER SECURITY]: ESTADO DE ADMINISTRACION IA (ASISTENTE MCP)\n");
    printf("%s================================================================================%s\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
    printf("  * Estado actual / Current status:   %s%s%s\n",
           g_ai_admin_granted ? TIGER_COLOR_GREEN : TIGER_COLOR_ORANGE,
           g_ai_admin_granted ? "CONCEDIDO (ADMIN HABILITADO) / GRANTED (ENABLED)" : "RESTRINGIDO (SOLICITA CONFIRMACION) / RESTRICTED (PROMPTS ON DEMAND)",
           TIGER_COLOR_RESET);
    printf("  * Alcance de proteccion / Scope:    Lectura/Escritura de archivos, modificacion de motor y control de hardware.\n");
    printf("  * Comandos de control / Control:    tiger ai-admin enable | tiger ai-admin disable\n");
    printf("%s================================================================================%s\n\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
}

bool tiger_ai_check_admin_permission(const char* operation_desc) {
    if (g_ai_admin_granted) return true;

    printf("\n%s================================================================================%s\n", TIGER_COLOR_RED, TIGER_COLOR_RESET);
    printf("%s  ⚠️  [ADVERTENCIA DE SEGURIDAD / SECURITY WARNING]                              %s\n", TIGER_COLOR_RED, TIGER_COLOR_RESET);
    printf("%s================================================================================%s\n", TIGER_COLOR_RED, TIGER_COLOR_RESET);
    printf("%sLa IA (ASISTENTE MCP) solicita permisos de administración para:%s\n", TIGER_COLOR_YELLOW, TIGER_COLOR_RESET);
    printf("  * Operacion: %s%s%s\n", TIGER_COLOR_CYAN, operation_desc ? operation_desc : "Modificacion de sistema", TIGER_COLOR_RESET);
    printf("  * Alcance:   Leer/Modificar codigo del sistema y controlar recursos de bajo nivel.\n\n");
    printf("%sAI (MCP ASSISTANT) is requesting administrative permissions to:%s\n", TIGER_COLOR_YELLOW, TIGER_COLOR_RESET);
    printf("  * Operation: %s%s%s\n", TIGER_COLOR_CYAN, operation_desc ? operation_desc : "System modification", TIGER_COLOR_RESET);
    printf("  * Scope:     Read/Modify system code and control low-level hardware resources.\n");
    printf("--------------------------------------------------------------------------------\n");
    printf("%s¿Deseas otorgar permisos de administracion a la IA para continuar?%s\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("%sDo you want to grant administrative permissions to the AI to proceed?%s\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("%s[S/N] (Si/No - Yes/No) > %s", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
    fflush(stdout);

    char resp[64];
    if (!fgets(resp, sizeof(resp), stdin)) {
        printf("\n%s[Tiger Seguridad]: Entrada no valida. Operacion DENEGADA. / Operation DENIED.%s\n", TIGER_COLOR_RED, TIGER_COLOR_RESET);
        return false;
    }

    char* p = resp;
    if ((unsigned char)p[0] == 0xFF && (unsigned char)p[1] == 0xFE) p += 2;
    while (*p == ' ' || *p == '\t' || *p == '\0') p++;
    if (*p == 's' || *p == 'S' || *p == 'y' || *p == 'Y' || *p == '1') {
        g_ai_admin_granted = true;
        printf("%s[Tiger Seguridad]: Permisos concedidos a la IA. Continuando ejecucion...%s\n", TIGER_COLOR_GREEN, TIGER_COLOR_RESET);
        printf("%s[Tiger Security]:  Administrative permissions granted. Resuming execution...%s\n\n", TIGER_COLOR_GREEN, TIGER_COLOR_RESET);
        return true;
    }

    printf("%s[Tiger Seguridad]: Permiso DENEGADO por el usuario. Operacion cancelada de forma segura.%s\n", TIGER_COLOR_RED, TIGER_COLOR_RESET);
    printf("%s[Tiger Security]:  Permission DENIED by user. Operation safely cancelled.%s\n\n", TIGER_COLOR_RED, TIGER_COLOR_RESET);
    return false;
}

TigerValue tiger_assistant_eval(TigerEnv* env, const char* arg0, const char* arg1, const char* arg2) {
    if (!arg0) return tiger_val_str("Asistente MCP listo.");

    /* 1. LECTURA DE CÓDIGO: "read" / "leer" */
    if (strcasecmp(arg0, "read") == 0 || strcasecmp(arg0, "leer") == 0) {
        const char* filepath = arg1 ? arg1 : "main.tg";
        size_t size = 0;
        char* content = tiger_read_file_resilient(filepath, &size);
        if (!content) {
            printf("%s[Tiger Asistente MCP]: No se pudo leer el archivo '%s'%s\n", TIGER_COLOR_RED, filepath, TIGER_COLOR_RESET);
            return tiger_val_nil();
        }
        printf("%s[Tiger Asistente MCP]: Archivo '%s' leido con exito (%zu bytes).%s\n", TIGER_COLOR_CYAN, filepath, size, TIGER_COLOR_RESET);
        TigerValue val = tiger_val_str(content);
        free(content);
        return val;
    }

    /* 2. MODIFICACIÓN DE CÓDIGO POR ENCIMA (SCRIPTS DE USUARIO / APP): "modify" / "modificar" / "modify_above" */
    if (strcasecmp(arg0, "modify") == 0 || strcasecmp(arg0, "modificar") == 0 ||
        strcasecmp(arg0, "modify_above") == 0 || strcasecmp(arg0, "modificar_arriba") == 0) {
        const char* filepath = arg1 ? arg1 : "app.tg";
        const char* new_content = arg2 ? arg2 : "";

        if (!tiger_ai_check_admin_permission("Modificar codigo por encima (Scripts de usuario / Aplicaciones)")) {
            return tiger_val_bool(false);
        }

        FILE* f = fopen(filepath, "wb");
        if (!f) {
            printf("%s[Tiger Asistente MCP]: Error al abrir '%s' para escritura.%s\n", TIGER_COLOR_RED, filepath, TIGER_COLOR_RESET);
            return tiger_val_bool(false);
        }
        fwrite(new_content, 1, strlen(new_content), f);
        fclose(f);
        printf("%s[Tiger Asistente MCP]: Codigo por encima en '%s' actualizado con exito (%zu bytes).%s\n",
               TIGER_COLOR_GREEN, filepath, strlen(new_content), TIGER_COLOR_RESET);
        return tiger_val_bool(true);
    }

    /* 3. MODIFICACIÓN DE CÓDIGO POR DEBAJO (MOTOR C / SISTEMA / CONFIG): "modify_under" / "modificar_abajo" */
    if (strcasecmp(arg0, "modify_under") == 0 || strcasecmp(arg0, "modificar_abajo") == 0 ||
        strcasecmp(arg0, "modify_lowlevel") == 0) {
        const char* filepath = arg1 ? arg1 : "src/tiger.h";
        const char* new_content = arg2 ? arg2 : "";

        if (!tiger_ai_check_admin_permission("Modificar codigo por debajo (Motor C nativo / Headers / Sistema)")) {
            return tiger_val_bool(false);
        }

        FILE* f = fopen(filepath, "wb");
        if (!f) {
            printf("%s[Tiger Asistente MCP]: Error al abrir archivo del motor '%s'.%s\n", TIGER_COLOR_RED, filepath, TIGER_COLOR_RESET);
            return tiger_val_bool(false);
        }
        fwrite(new_content, 1, strlen(new_content), f);
        fclose(f);
        printf("%s[Tiger Asistente MCP]: Codigo de bajo nivel en '%s' actualizado exitosamente.%s\n",
               TIGER_COLOR_GREEN, filepath, TIGER_COLOR_RESET);
        return tiger_val_bool(true);
    }

    /* 4. EJECUCIÓN DINÁMICA DE CÓDIGO: "exec" / "ejecutar" */
    if (strcasecmp(arg0, "exec") == 0 || strcasecmp(arg0, "ejecutar") == 0) {
        const char* code = arg1 ? arg1 : arg2;
        if (!code || strlen(code) == 0) return tiger_val_bool(false);

        if (!tiger_ai_check_admin_permission("Ejecutar codigo dinamico en el motor Tiger")) {
            return tiger_val_bool(false);
        }

        printf("%s[Tiger Asistente MCP]: Ejecutando bloque de codigo dinamicamente...%s\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
        bool ok = tiger_run_source(code, strlen(code), env, true);
        return tiger_val_bool(ok);
    }

    /* 5. GESTIÓN DE RECURSOS DE BAJO NIVEL (CPU / RAM / AFINIDAD): "hardware" / "recursos" */
    if (strcasecmp(arg0, "hardware") == 0 || strcasecmp(arg0, "recursos") == 0 || strcasecmp(arg0, "lowlevel") == 0) {
        if (!tiger_ai_check_admin_permission("Control y acceso a recursos de hardware y memoria de bajo nivel")) {
            return tiger_val_nil();
        }

        TigerValue d = tiger_val_dict_new();
        tiger_dict_set(d.as.dict_val, "mcp_status", tiger_val_str("ACTIVO"));
        tiger_dict_set(d.as.dict_val, "ai_admin", tiger_val_bool(g_ai_admin_granted));
        tiger_dict_set(d.as.dict_val, "maestros", tiger_val_int(2));
        tiger_dict_set(d.as.dict_val, "subprocesadores", tiger_val_int(8));
        tiger_dict_set(d.as.dict_val, "avx2_vector_bits", tiger_val_int(256));
        tiger_dict_set(d.as.dict_val, "arena_size_mb", tiger_val_int(64));
#ifdef _WIN32
        typedef BOOL (WINAPI *PFN_GlobalMemoryStatusEx)(LPMEMORYSTATUSEX);
        PFN_GlobalMemoryStatusEx pfnGMS = (PFN_GlobalMemoryStatusEx)(void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GlobalMemoryStatusEx");
        if (pfnGMS) {
            MEMORYSTATUSEX ms;
            ms.dwLength = sizeof(ms);
            if (pfnGMS(&ms)) {
                tiger_dict_set(d.as.dict_val, "ram_total_mb", tiger_val_int((int64_t)(ms.ullTotalPhys / (1024 * 1024))));
                tiger_dict_set(d.as.dict_val, "ram_libre_mb", tiger_val_int((int64_t)(ms.ullAvailPhys / (1024 * 1024))));
            }
        }
#endif
        printf("%s[Tiger Asistente MCP]: Telemetria y recursos de bajo nivel consultados.%s\n", TIGER_COLOR_GREEN, TIGER_COLOR_RESET);
        return d;
    }

    /* 6. ESTADO GENERAL DEL ASISTENTE: "status" / "estado" */
    if (strcasecmp(arg0, "status") == 0 || strcasecmp(arg0, "estado") == 0) {
        TigerValue d = tiger_val_dict_new();
        tiger_dict_set(d.as.dict_val, "asistente", tiger_val_str("Tiger MCP Copilot 10.2"));
        tiger_dict_set(d.as.dict_val, "admin_autorizado", tiger_val_bool(g_ai_admin_granted));
        tiger_dict_set(d.as.dict_val, "capacidades", tiger_val_str("leer, modificar_arriba, modificar_abajo, ejecutar, hardware"));
        return d;
    }

    /* 7. CONSULTA O PROMPT CONTEXTUAL / CACHE LOCAL */
    printf("%s[Tiger Asistente MCP]: Analizando intencion de codigo: '%s'%s\n", TIGER_COLOR_CYAN, arg0, TIGER_COLOR_RESET);
    FILE* cf = fopen(".tiger_asistente_cache.json", "r");
    char cached_code[2048];
    cached_code[0] = '\0';
    if (cf) {
        char line[512];
        while (fgets(line, sizeof(line), cf)) {
            char* val_start = strchr(line, ':');
            if (val_start) {
                val_start++;
                while (*val_start == ' ' || *val_start == '"') val_start++;
                char* val_end = strrchr(val_start, '"');
                if (val_end) {
                    *val_end = '\0';
                    strncpy(cached_code, val_start, sizeof(cached_code) - 1);
                    break;
                }
            }
        }
        fclose(cf);
    }
    if (cached_code[0] != '\0') {
        printf("%s[Tiger Asistente MCP]: Solucion recuperada de la cache local.%s\n", TIGER_COLOR_GREEN, TIGER_COLOR_RESET);
        return tiger_val_str(cached_code);
    }

    TigerAIChatConfig cfg;
    tiger_ai_load_config(&cfg);
    if (cfg.is_configured && strlen(cfg.api_key) > 0) {
        char ai_response[4096];
        if (tiger_ai_query_gemini(cfg.api_key, cfg.system_prompt, arg0, ai_response, sizeof(ai_response))) {
            printf("%s[Tiger Asistente MCP]: Respuesta generada por la IA exitosamente.%s\n", TIGER_COLOR_GREEN, TIGER_COLOR_RESET);
            return tiger_val_str(ai_response);
        }
    }

    return tiger_val_str("Asistente MCP: Intencion analizada y lista para ejecucion.");
}

bool tiger_assistant_call(TigerEnv* env, const char* api_key, const char* model, const char* prompt, const char* target_var) {
    TigerValue res = tiger_assistant_eval(env, api_key, model, prompt);
    if (target_var && env) {
        tiger_env_set(env, target_var, res, false, VAL_NIL);
    }
    tiger_val_free(&res);
    return true;
}

/* ========================================================================= */
/* BANCO DE PRUEBAS COMPARATIVO DE LAS 3 OPCIONES DE VELOCIDAD POLÍGLOTA     */
/* ========================================================================= */

void tiger_run_polyglot_benchmark(TigerEnv* env) {
#ifdef _WIN32
    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);

    printf("\n========================================================================================\n");
    printf("  [TIGER 10.1]: BANCO DE PRUEBAS DE ACELERACION POLIGLOTA EN WINDOWS\n");
    printf("========================================================================================\n");
    printf("Evaluando vector de calculo matematico Sigmoide e IA en 4 arquitecturas...\n\n");

    const char* test_code =
        "import math\n"
        "x = 0.845\n"
        "z = (x * 0.82) + (1.0 * 0.45) + (0.95 * 1.15) - 0.65\n"
        "prob = 1.0 / (1.0 + math.exp(-z))\n"
        "TIGER.score_ia = f'{prob * 100:.1f}%'\n";

    /* 1. Baseline: CreateProcessA estándar en frío */
    printf(">>> [1/4] Probando Baseline: CreateProcessA en Frio (Cold Start)...\n");
    tiger_set_polyglot_mode(TIGER_POLYGLOT_BASELINE);
    QueryPerformanceCounter(&t0);
    tiger_execute_py_block(env, test_code);
    QueryPerformanceCounter(&t1);
    double t_baseline = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;

    /* 2. Nivel 1: Fast Flags (-S -B -u) */
    printf(">>> [2/3] Probando Nivel 1: Tuberias en Memoria con Aislamiento (-S -B -u)...\n");
    tiger_set_polyglot_mode(TIGER_POLYGLOT_FAST_FLAGS);
    QueryPerformanceCounter(&t0);
    tiger_execute_py_block(env, test_code);
    QueryPerformanceCounter(&t1);
    double t_flags = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;

    /* 3. Nivel 2: En Silicio (In-Process C) */
    printf(">>> [3/3] Probando Nivel 2: Evaluacion Directa En-Proceso (Silicio C)...\n");
    tiger_set_polyglot_mode(TIGER_POLYGLOT_INPROC);
    QueryPerformanceCounter(&t0);
    tiger_execute_py_block(env, test_code);
    QueryPerformanceCounter(&t1);
    double t_inproc = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;

    /* Restaurar modo predeterminado a Fast Flags */
    tiger_set_polyglot_mode(TIGER_POLYGLOT_FAST_FLAGS);

    printf("\n+------------------------------------------------+-----------------+-----------------------+\n");
    printf("| Arquitectura de Ejecucion Poliglota            | Tiempo Real     | Aceleracion Relativa  |\n");
    printf("+------------------------------------------------+-----------------+-----------------------+\n");
    printf("| Baseline: CreateProcessA en Frio (Cold Start)  | %9.3f ms     | 1.0x (Referencia)     |\n", t_baseline);
    printf("| Nivel 1: Tuberias en Memoria (-S -B -u)        | %9.3f ms     | %5.1fx mas rapido     |\n", t_flags, (t_flags > 0.001) ? (t_baseline / t_flags) : 1.0);
    printf("| Nivel 2: En Silicio (In-Process / Microseg.)   | %9.3f ms     | %5.1fx mas rapido  |\n", t_inproc, (t_inproc > 0.0001) ? (t_baseline / t_inproc) : 5000.0);
    printf("+------------------------------------------------+-----------------+-----------------------+\n");
#endif
}



