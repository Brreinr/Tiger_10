#include "tiger_ai_chat.h"
#include "tiger_c_interop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#define CHAT_DIR "chats"
#define CONFIG_PATH "chats/.ai_config.json"

static void tiger_ai_get_chats_dir(char* out_path, size_t out_max) {
    if (!out_path || out_max == 0) return;
    out_path[0] = '\0';

#ifdef _WIN32
    char exe_buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exe_buf, MAX_PATH);
    if (len > 0) {
        for (DWORD i = 0; i < len; i++) {
            if (exe_buf[i] == '\\') exe_buf[i] = '/';
        }
        char* last_slash = strrchr(exe_buf, '/');
        if (last_slash) *last_slash = '\0';

        char* bin_suffix = strstr(exe_buf, "/bin");
        if (bin_suffix && *(bin_suffix + 4) == '\0') {
            *bin_suffix = '\0';
        }

        snprintf(out_path, out_max, "%s/chats", exe_buf);
        return;
    }
#endif

    snprintf(out_path, out_max, "chats");
}

static void tiger_ai_get_config_path(char* out_path, size_t out_max) {
    if (!out_path || out_max == 0) return;

    /* 1. Comprobar en carpeta chats relativa al ejecutable */
    char chats_dir[MAX_PATH];
    tiger_ai_get_chats_dir(chats_dir, sizeof(chats_dir));
    char candidate[MAX_PATH];
    snprintf(candidate, sizeof(candidate), "%s/.ai_config.json", chats_dir);
    FILE* f = fopen(candidate, "r");
    if (f) {
        fclose(f);
        strncpy(out_path, candidate, out_max - 1);
        out_path[out_max - 1] = '\0';
        return;
    }

    /* 2. Comprobar en el directorio actual de trabajo (CWD) */
    f = fopen("chats/.ai_config.json", "r");
    if (f) {
        fclose(f);
        strncpy(out_path, "chats/.ai_config.json", out_max - 1);
        out_path[out_max - 1] = '\0';
        return;
    }

    /* 3. Comprobar en %USERPROFILE%/.tiger/.ai_config.json */
#ifdef _WIN32
    const char* user_profile = getenv("USERPROFILE");
    if (user_profile && *user_profile) {
        char user_cfg[MAX_PATH];
        snprintf(user_cfg, sizeof(user_cfg), "%s/.tiger/.ai_config.json", user_profile);
        f = fopen(user_cfg, "r");
        if (f) {
            fclose(f);
            strncpy(out_path, user_cfg, out_max - 1);
            out_path[out_max - 1] = '\0';
            return;
        }
    }
#endif

    /* 4. Por defecto, guardar en la carpeta chats del ejecutable */
    strncpy(out_path, candidate, out_max - 1);
    out_path[out_max - 1] = '\0';
}

static const char* tiger_casestr_fast(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
            const char* h = haystack;
            const char* n = needle;
            while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
                h++; n++;
            }
            if (!*n) return haystack;
        }
    }
    return NULL;
}

static void ensure_chats_dir(void) {
    char chats_dir[MAX_PATH];
    tiger_ai_get_chats_dir(chats_dir, sizeof(chats_dir));
#ifdef _WIN32
    CreateDirectoryA(chats_dir, NULL);
    CreateDirectoryA("chats", NULL);
#else
    mkdir(chats_dir, 0755);
    mkdir("chats", 0755);
#endif
}

static void get_timestamp_str(char* buf, size_t buf_size) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    if (t) {
        strftime(buf, buf_size, "%Y-%m-%d %H:%M:%S", t);
    } else {
        snprintf(buf, buf_size, "2026-10-28 00:00:00");
    }
}

static void get_filename_timestamp(char* buf, size_t buf_size) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    if (t) {
        strftime(buf, buf_size, "%Y%m%d_%H%M%S", t);
    } else {
        snprintf(buf, buf_size, "20261028_000000");
    }
}

static bool json_parse_str_val(const char* line, const char* key, char* dst, size_t dst_max) {
    if (!line || !key || !dst || dst_max == 0) return false;
    char* k = strstr(line, key);
    if (!k) return false;
    char* col = strchr(k, ':');
    if (!col) return false;
    char* q1 = strchr(col, '"');
    if (!q1) return false;
    char* q2 = strrchr(q1 + 1, '"');
    if (!q2) return false;
    size_t len = q2 - (q1 + 1);
    if (len >= dst_max) len = dst_max - 1;
    memcpy(dst, q1 + 1, len);
    dst[len] = '\0';
    return true;
}

static bool json_parse_bool_val(const char* line, const char* key, bool default_val) {
    char* k = strstr(line, key);
    if (!k) return default_val;
    if (strstr(k, "false") || strstr(k, "0")) return false;
    if (strstr(k, "true") || strstr(k, "1")) return true;
    return default_val;
}

static void tiger_ai_set_default_model(TigerAIChatConfig* cfg) {
    if (!cfg) return;
    if (strcasecmp(cfg->provider, "openai") == 0) {
        strncpy(cfg->model, "gpt-4o-mini", sizeof(cfg->model) - 1);
    } else if (strcasecmp(cfg->provider, "anthropic") == 0) {
        strncpy(cfg->model, "claude-3-5-haiku-20241022", sizeof(cfg->model) - 1);
    } else if (strcasecmp(cfg->provider, "groq") == 0) {
        strncpy(cfg->model, "llama-3.3-70b-versatile", sizeof(cfg->model) - 1);
    } else if (strcasecmp(cfg->provider, "deepseek") == 0) {
        strncpy(cfg->model, "deepseek-chat", sizeof(cfg->model) - 1);
    } else if (strcasecmp(cfg->provider, "openrouter") == 0) {
        strncpy(cfg->model, "meta-llama/llama-3.3-70b-instruct:free", sizeof(cfg->model) - 1);
    } else if (strcasecmp(cfg->provider, "ollama") == 0) {
        strncpy(cfg->model, "llama3", sizeof(cfg->model) - 1);
    } else {
        strncpy(cfg->provider, "gemini", sizeof(cfg->provider) - 1);
        strncpy(cfg->model, "gemini-flash-lite-latest", sizeof(cfg->model) - 1);
    }
}

void tiger_ai_load_config(TigerAIChatConfig* cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(TigerAIChatConfig));
    strncpy(cfg->provider, "gemini", sizeof(cfg->provider) - 1);
    strncpy(cfg->model, "gemini-flash-lite-latest", sizeof(cfg->model) - 1);
    cfg->auto_fallback = true;
    snprintf(cfg->system_prompt, sizeof(cfg->system_prompt),
             "Eres el Copiloto Inteligente de Tiger 10.2, experto en arquitectura de silicio, codigo bilingüe, optimizacion de memoria y calculo paralelo masivo.");

    ensure_chats_dir();
    char cfg_path[MAX_PATH];
    tiger_ai_get_config_path(cfg_path, sizeof(cfg_path));
    FILE* f = fopen(cfg_path, "r");
    if (f) {
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            json_parse_str_val(line, "\"provider\":", cfg->provider, sizeof(cfg->provider));
            json_parse_str_val(line, "\"model\":", cfg->model, sizeof(cfg->model));
            if (json_parse_str_val(line, "\"api_key\":", cfg->api_key, sizeof(cfg->api_key))) {
                if (strlen(cfg->api_key) > 0) cfg->is_configured = true;
            }
            json_parse_str_val(line, "\"custom_endpoint\":", cfg->custom_endpoint, sizeof(cfg->custom_endpoint));
            json_parse_str_val(line, "\"system_prompt\":", cfg->system_prompt, sizeof(cfg->system_prompt));
            if (strstr(line, "\"auto_fallback\":")) {
                cfg->auto_fallback = json_parse_bool_val(line, "\"auto_fallback\":", true);
            }
        }
        fclose(f);
    }

    /* Asignar modelo por defecto si no se especificó */
    if (cfg->model[0] == '\0') {
        tiger_ai_set_default_model(cfg);
    }

    /* Fallback a variables de entorno según el proveedor */
    if (!cfg->is_configured || strlen(cfg->api_key) == 0) {
        const char* env_key = NULL;
        if (strcasecmp(cfg->provider, "gemini") == 0) {
            env_key = getenv("GEMINI_API_KEY");
            if (!env_key || !*env_key) env_key = getenv("GOOGLE_API_KEY");
            if (!env_key || !*env_key) env_key = getenv("TIGER_AI_KEY");
        } else if (strcasecmp(cfg->provider, "openai") == 0) {
            env_key = getenv("OPENAI_API_KEY");
        } else if (strcasecmp(cfg->provider, "anthropic") == 0) {
            env_key = getenv("ANTHROPIC_API_KEY");
        } else if (strcasecmp(cfg->provider, "groq") == 0) {
            env_key = getenv("GROQ_API_KEY");
        } else if (strcasecmp(cfg->provider, "deepseek") == 0) {
            env_key = getenv("DEEPSEEK_API_KEY");
        } else if (strcasecmp(cfg->provider, "openrouter") == 0) {
            env_key = getenv("OPENROUTER_API_KEY");
        }

        if (env_key && *env_key) {
            strncpy(cfg->api_key, env_key, sizeof(cfg->api_key) - 1);
            cfg->is_configured = true;
        }
    }

    /* Ollama local opera sin necesidad de clave */
    if (strcasecmp(cfg->provider, "ollama") == 0) {
        cfg->is_configured = true;
    }
}

void tiger_ai_save_config(const TigerAIChatConfig* cfg) {
    if (!cfg) return;
    ensure_chats_dir();
    char cfg_path[MAX_PATH];
    tiger_ai_get_config_path(cfg_path, sizeof(cfg_path));
    FILE* f = fopen(cfg_path, "w");
    if (f) {
        fprintf(f, "{\n");
        fprintf(f, "  \"provider\": \"%s\",\n", cfg->provider[0] ? cfg->provider : "gemini");
        fprintf(f, "  \"model\": \"%s\",\n", cfg->model[0] ? cfg->model : "gemini-flash-lite-latest");
        fprintf(f, "  \"api_key\": \"%s\",\n", cfg->api_key);
        fprintf(f, "  \"custom_endpoint\": \"%s\",\n", cfg->custom_endpoint);
        fprintf(f, "  \"system_prompt\": \"%s\",\n", cfg->system_prompt);
        fprintf(f, "  \"auto_fallback\": %s\n", cfg->auto_fallback ? "true" : "false");
        fprintf(f, "}\n");
        fclose(f);
    }

    /* Sincronizar en carpeta local de chats si es distinta */
    if (strcmp(cfg_path, "chats/.ai_config.json") != 0) {
        FILE* flocal = fopen("chats/.ai_config.json", "w");
        if (flocal) {
            fprintf(flocal, "{\n");
            fprintf(flocal, "  \"provider\": \"%s\",\n", cfg->provider[0] ? cfg->provider : "gemini");
            fprintf(flocal, "  \"model\": \"%s\",\n", cfg->model[0] ? cfg->model : "gemini-flash-lite-latest");
            fprintf(flocal, "  \"api_key\": \"%s\",\n", cfg->api_key);
            fprintf(flocal, "  \"custom_endpoint\": \"%s\",\n", cfg->custom_endpoint);
            fprintf(flocal, "  \"system_prompt\": \"%s\",\n", cfg->system_prompt);
            fprintf(flocal, "  \"auto_fallback\": %s\n", cfg->auto_fallback ? "true" : "false");
            fprintf(flocal, "}\n");
            fclose(flocal);
        }
    }

#ifdef _WIN32
    const char* user_profile = getenv("USERPROFILE");
    if (user_profile && *user_profile) {
        char user_dir[MAX_PATH];
        char user_cfg[MAX_PATH];
        snprintf(user_dir, sizeof(user_dir), "%s/.tiger", user_profile);
        CreateDirectoryA(user_dir, NULL);
        snprintf(user_cfg, sizeof(user_cfg), "%s/.tiger/.ai_config.json", user_profile);
        FILE* fu = fopen(user_cfg, "w");
        if (fu) {
            fprintf(fu, "{\n");
            fprintf(fu, "  \"provider\": \"%s\",\n", cfg->provider[0] ? cfg->provider : "gemini");
            fprintf(fu, "  \"model\": \"%s\",\n", cfg->model[0] ? cfg->model : "gemini-flash-lite-latest");
            fprintf(fu, "  \"api_key\": \"%s\",\n", cfg->api_key);
            fprintf(fu, "  \"custom_endpoint\": \"%s\",\n", cfg->custom_endpoint);
            fprintf(fu, "  \"system_prompt\": \"%s\",\n", cfg->system_prompt);
            fprintf(fu, "  \"auto_fallback\": %s\n", cfg->auto_fallback ? "true" : "false");
            fprintf(fu, "}\n");
            fclose(fu);
        }
    }
#endif
}

static void log_to_chat_file(const char* filepath, const char* role, const char* content) {
    if (!filepath || !content) return;
    ensure_chats_dir();
    FILE* f = fopen(filepath, "a");
    if (!f) return;

    char ts[64];
    get_timestamp_str(ts, sizeof(ts));

    fprintf(f, "================================================================================\n");
    fprintf(f, "[%s - %s]\n", role, ts);
    fprintf(f, "%s\n", content);
    fprintf(f, "================================================================================\n\n");
    fclose(f);
}

static void print_ai_banner(void) {
    printf("%s", TIGER_COLOR_ORANGE);
    printf("  ████████╗██╗ ██████╗ ███████╗██████╗     ██╗ █████╗ \n");
    printf("  ╚══██╔══╝██║██╔════╝ ██╔════╝██╔══██╗    ██║██╔══██╗\n");
    printf("     ██║   ██║██║  ███╗█████╗  ██████╔╝    ██║███████║\n");
    printf("     ██║   ██║██║   ██║██╔══╝  ██╔══██╗    ██║██╔══██║\n");
    printf("     ██║   ██║╚██████╔╝███████╗██║  ██║    ██║██║  ██║\n");
    printf("     ╚═╝   ╚═╝ ╚═════╝ ╚══════╝╚═╝  ╚═╝    ╚═╝╚═╝  ╚═╝\n");
    printf("%s", TIGER_COLOR_CYAN);
    printf("================================================================================\n");
    printf("  🐯 TIGER 10.2: COPILOTO DE IA Y TERMINAL CONVERSACIONAL (MCP AGENT)\n");
    printf("================================================================================\n%s", TIGER_COLOR_RESET);
}

static void load_and_display_history(const char* filepath) {
    FILE* f = fopen(filepath, "r");
    if (!f) return;

    printf("\n%s╭── [HISTORIAL CARGADO DESDE: %s] %s\n", TIGER_COLOR_CYAN, filepath, TIGER_COLOR_RESET);
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "[USUARIO") || strstr(line, "[USER")) {
            printf("%s│ %s%s", TIGER_COLOR_ORANGE, line, TIGER_COLOR_RESET);
        } else if (strstr(line, "[TIGER IA") || strstr(line, "[TIGER AI")) {
            printf("%s│ %s%s", TIGER_COLOR_GREEN, line, TIGER_COLOR_RESET);
        } else if (strstr(line, "======")) {
            printf("%s│ ----------------------------------------------------------------------%s\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
        } else {
            printf("│ %s", line);
        }
    }
    printf("%s╰── [FIN DEL HISTORIAL CARGADO - SESION RESTAURADA] ──────────────────────────%s\n\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
    fclose(f);
}

static void tiger_json_escape(const char* src, char* dst, size_t dst_max) {
    if (!src || !dst || dst_max == 0) return;
    size_t d = 0;
    for (size_t s = 0; src[s] && d + 4 < dst_max; s++) {
        unsigned char c = (unsigned char)src[s];
        if (c == '"') { dst[d++] = '\\'; dst[d++] = '"'; }
        else if (c == '\\') { dst[d++] = '\\'; dst[d++] = '\\'; }
        else if (c == '\n') { dst[d++] = '\\'; dst[d++] = 'n'; }
        else if (c == '\r') { dst[d++] = '\\'; dst[d++] = 'r'; }
        else if (c == '\t') { dst[d++] = '\\'; dst[d++] = 't'; }
        else if (c >= 32) { dst[d++] = (char)c; }
    }
    dst[d] = '\0';
}

static void tiger_json_unescape(const char* src, char* dst, size_t dst_max) {
    if (!src || !dst || dst_max == 0) return;
    size_t d = 0;
    for (size_t s = 0; src[s] && d + 1 < dst_max; s++) {
        if (src[s] == '\\' && src[s + 1]) {
            s++;
            if (src[s] == 'n') dst[d++] = '\n';
            else if (src[s] == 'r') dst[d++] = '\r';
            else if (src[s] == 't') dst[d++] = '\t';
            else if (src[s] == '"') dst[d++] = '"';
            else if (src[s] == '\\') dst[d++] = '\\';
            else if (src[s] == 'u' && s + 4 < strlen(src)) {
                dst[d++] = ' ';
                s += 4;
            } else dst[d++] = src[s];
        } else {
            dst[d++] = src[s];
        }
    }
    dst[d] = '\0';
}

static bool tiger_exec_cmd_capture(const char* cmd, char* out_buf, size_t out_max) {
    if (!cmd || !out_buf || out_max == 0) return false;
    out_buf[0] = '\0';

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hStdOutRd = NULL, hStdOutWr = NULL;
    if (!CreatePipe(&hStdOutRd, &hStdOutWr, &sa, 0)) return false;
    SetHandleInformation(hStdOutRd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = hStdOutWr;
    si.hStdError = hStdOutWr;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    char cmd_copy[2048];
    strncpy(cmd_copy, cmd, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';

    if (!CreateProcessA(NULL, cmd_copy, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hStdOutRd);
        CloseHandle(hStdOutWr);
        return false;
    }
    CloseHandle(hStdOutWr);

    DWORD total_read = 0;
    DWORD bytes_read = 0;
    static char s_exec_chunk[4096];
    while (ReadFile(hStdOutRd, s_exec_chunk, sizeof(s_exec_chunk) - 1, &bytes_read, NULL) && bytes_read > 0) {
        if (total_read + bytes_read < out_max) {
            memcpy(out_buf + total_read, s_exec_chunk, bytes_read);
            total_read += bytes_read;
            out_buf[total_read] = '\0';
        }
    }
    CloseHandle(hStdOutRd);

    WaitForSingleObject(pi.hProcess, 20000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return total_read > 0;
#else
    FILE* p = popen(cmd, "r");
    if (!p) return false;
    size_t rd = fread(out_buf, 1, out_max - 1, p);
    out_buf[rd] = '\0';
    pclose(p);
    return rd > 0;
#endif
}

static char s_gemini_esc_sys[2048];
static char s_gemini_esc_user[16384];
static char s_gemini_resp_buf[65536];
static char s_mcp_line_buf[65536];
static char s_mcp_result_text[16384];
static char s_mcp_esc_res[32768];

static char s_smart_err_buf[4096];

bool tiger_ai_query_gemini_ex(const char* api_key, const char* model, const char* system_prompt, const char* user_prompt, char* out_text, size_t out_max, bool* out_is_quota) {
    if (!api_key || !*api_key || !user_prompt || !out_text || out_max == 0) return false;
    out_text[0] = '\0';
    if (out_is_quota) *out_is_quota = false;

    ensure_chats_dir();
    char chats_dir[MAX_PATH];
    tiger_ai_get_chats_dir(chats_dir, sizeof(chats_dir));
    char temp_req[MAX_PATH];
    snprintf(temp_req, sizeof(temp_req), "%s/.ai_req_gemini.json", chats_dir);
    FILE* f = fopen(temp_req, "wb");
    if (!f) return false;

    s_gemini_esc_sys[0] = '\0';
    s_gemini_esc_user[0] = '\0';
    tiger_json_escape(system_prompt ? system_prompt : "Eres el Copiloto Inteligente de Tiger 10.2", s_gemini_esc_sys, sizeof(s_gemini_esc_sys));
    tiger_json_escape(user_prompt, s_gemini_esc_user, sizeof(s_gemini_esc_user));

    fprintf(f, "{\n");
    fprintf(f, "  \"system_instruction\": {\"parts\": [{\"text\": \"%s\"}]},\n", s_gemini_esc_sys);
    fprintf(f, "  \"contents\": [{\"parts\": [{\"text\": \"%s\"}]}]\n", s_gemini_esc_user);
    fprintf(f, "}\n");
    fclose(f);

    const char* fallback_models[] = {
        (model && *model) ? model : "gemini-flash-lite-latest",
        "gemini-flash-lite-latest",
        "gemini-3.1-flash-lite",
        "gemini-3.5-flash-lite"
    };

    bool success = false;
    for (size_t mi = 0; mi < sizeof(fallback_models) / sizeof(fallback_models[0]) && !success; mi++) {
        if (mi > 0 && strcmp(fallback_models[mi], fallback_models[0]) == 0) continue;

        char cmd[2048];
        snprintf(cmd, sizeof(cmd),
                 "curl.exe -s -m 25 -X POST \"https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent\" "
                 "-H \"Content-Type: application/json\" -H \"x-goog-api-key: %s\" -d @\"%s\"",
                 fallback_models[mi], api_key, temp_req);

        s_gemini_resp_buf[0] = '\0';
        if (!tiger_exec_cmd_capture(cmd, s_gemini_resp_buf, sizeof(s_gemini_resp_buf))) continue;

        if (strstr(s_gemini_resp_buf, "RESOURCE_EXHAUSTED") ||
            strstr(s_gemini_resp_buf, "429") ||
            strstr(s_gemini_resp_buf, "Quota exceeded") ||
            strstr(s_gemini_resp_buf, "quota limit")) {
            if (out_is_quota) *out_is_quota = true;
            continue;
        }
        if (strstr(s_gemini_resp_buf, "\"error\":")) continue;

        char* text_key = strstr(s_gemini_resp_buf, "\"text\": \"");
        if (!text_key) text_key = strstr(s_gemini_resp_buf, "\"text\":\"");
        if (text_key) {
            text_key += (text_key[7] == ' ') ? 9 : 8;
            char* end = text_key;
            while (*end) {
                if (*end == '"' && *(end - 1) != '\\') break;
                end++;
            }
            if (*end == '"') {
                *end = '\0';
                tiger_json_unescape(text_key, out_text, out_max);
                success = true;
                break;
            }
        }
    }

    remove(temp_req);
    return success && strlen(out_text) > 0;
}

bool tiger_ai_query_gemini(const char* api_key, const char* system_prompt, const char* user_prompt, char* out_text, size_t out_max) {
    bool is_quota = false;
    return tiger_ai_query_gemini_ex(api_key, NULL, system_prompt, user_prompt, out_text, out_max, &is_quota);
}

bool tiger_ai_query_openai_compat(const char* endpoint, const char* api_key, const char* model, const char* system_prompt, const char* user_prompt, char* out_text, size_t out_max, bool* out_is_quota) {
    if (!endpoint || !*endpoint || !user_prompt || !out_text || out_max == 0) return false;
    out_text[0] = '\0';
    if (out_is_quota) *out_is_quota = false;

    ensure_chats_dir();
    char chats_dir[MAX_PATH];
    tiger_ai_get_chats_dir(chats_dir, sizeof(chats_dir));
    char temp_req[MAX_PATH];
    snprintf(temp_req, sizeof(temp_req), "%s/.ai_req_openai.json", chats_dir);
    FILE* f = fopen(temp_req, "wb");
    if (!f) return false;

    s_gemini_esc_sys[0] = '\0';
    s_gemini_esc_user[0] = '\0';
    tiger_json_escape(system_prompt ? system_prompt : "Eres el Copiloto Inteligente de Tiger 10.2", s_gemini_esc_sys, sizeof(s_gemini_esc_sys));
    tiger_json_escape(user_prompt, s_gemini_esc_user, sizeof(s_gemini_esc_user));

    const char* use_model = (model && *model) ? model : "gpt-4o-mini";

    fprintf(f, "{\n");
    fprintf(f, "  \"model\": \"%s\",\n", use_model);
    fprintf(f, "  \"messages\": [\n");
    fprintf(f, "    {\"role\": \"system\", \"content\": \"%s\"},\n", s_gemini_esc_sys);
    fprintf(f, "    {\"role\": \"user\", \"content\": \"%s\"}\n", s_gemini_esc_user);
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    fclose(f);

    char cmd[4096];
    if (api_key && *api_key) {
        snprintf(cmd, sizeof(cmd),
                 "curl.exe -s -m 35 -X POST \"%s\" "
                 "-H \"Content-Type: application/json\" "
                 "-H \"Authorization: Bearer %s\" -d @\"%s\"",
                 endpoint, api_key, temp_req);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "curl.exe -s -m 35 -X POST \"%s\" "
                 "-H \"Content-Type: application/json\" -d @\"%s\"",
                 endpoint, temp_req);
    }

    s_gemini_resp_buf[0] = '\0';
    bool ok = tiger_exec_cmd_capture(cmd, s_gemini_resp_buf, sizeof(s_gemini_resp_buf));
    remove(temp_req);
    if (!ok) return false;

    if (strstr(s_gemini_resp_buf, "insufficient_quota") ||
        strstr(s_gemini_resp_buf, "429") ||
        strstr(s_gemini_resp_buf, "rate_limit") ||
        strstr(s_gemini_resp_buf, "out of credits") ||
        strstr(s_gemini_resp_buf, "credit_balance") ||
        strstr(s_gemini_resp_buf, "quota exceeded") ||
        strstr(s_gemini_resp_buf, "balance")) {
        if (out_is_quota) *out_is_quota = true;
    }

    char* content_key = strstr(s_gemini_resp_buf, "\"content\": \"");
    if (!content_key) content_key = strstr(s_gemini_resp_buf, "\"content\":\"");
    if (content_key) {
        content_key += (content_key[10] == ' ') ? 12 : 11;
        char* end = content_key;
        while (*end) {
            if (*end == '"' && *(end - 1) != '\\') break;
            end++;
        }
        if (*end == '"') {
            *end = '\0';
            tiger_json_unescape(content_key, out_text, out_max);
            return strlen(out_text) > 0;
        }
    }

    return false;
}

bool tiger_ai_query_anthropic(const char* api_key, const char* model, const char* system_prompt, const char* user_prompt, char* out_text, size_t out_max, bool* out_is_quota) {
    if (!api_key || !*api_key || !user_prompt || !out_text || out_max == 0) return false;
    out_text[0] = '\0';
    if (out_is_quota) *out_is_quota = false;

    ensure_chats_dir();
    char chats_dir[MAX_PATH];
    tiger_ai_get_chats_dir(chats_dir, sizeof(chats_dir));
    char temp_req[MAX_PATH];
    snprintf(temp_req, sizeof(temp_req), "%s/.ai_req_anthropic.json", chats_dir);
    FILE* f = fopen(temp_req, "wb");
    if (!f) return false;

    s_gemini_esc_sys[0] = '\0';
    s_gemini_esc_user[0] = '\0';
    tiger_json_escape(system_prompt ? system_prompt : "Eres el Copiloto Inteligente de Tiger 10.2", s_gemini_esc_sys, sizeof(s_gemini_esc_sys));
    tiger_json_escape(user_prompt, s_gemini_esc_user, sizeof(s_gemini_esc_user));

    const char* use_model = (model && *model) ? model : "claude-3-5-haiku-20241022";

    fprintf(f, "{\n");
    fprintf(f, "  \"model\": \"%s\",\n", use_model);
    fprintf(f, "  \"max_tokens\": 4096,\n");
    fprintf(f, "  \"system\": \"%s\",\n", s_gemini_esc_sys);
    fprintf(f, "  \"messages\": [\n");
    fprintf(f, "    {\"role\": \"user\", \"content\": \"%s\"}\n", s_gemini_esc_user);
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    fclose(f);

    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "curl.exe -s -m 35 -X POST \"https://api.anthropic.com/v1/messages\" "
             "-H \"Content-Type: application/json\" "
             "-H \"x-api-key: %s\" "
             "-H \"anthropic-version: 2023-06-01\" -d @\"%s\"",
             api_key, temp_req);

    s_gemini_resp_buf[0] = '\0';
    bool ok = tiger_exec_cmd_capture(cmd, s_gemini_resp_buf, sizeof(s_gemini_resp_buf));
    remove(temp_req);
    if (!ok) return false;

    if (strstr(s_gemini_resp_buf, "rate_limit_error") ||
        strstr(s_gemini_resp_buf, "credit_balance_too_low") ||
        strstr(s_gemini_resp_buf, "429")) {
        if (out_is_quota) *out_is_quota = true;
    }

    char* text_key = strstr(s_gemini_resp_buf, "\"text\": \"");
    if (!text_key) text_key = strstr(s_gemini_resp_buf, "\"text\":\"");
    if (text_key) {
        text_key += (text_key[7] == ' ') ? 9 : 8;
        char* end = text_key;
        while (*end) {
            if (*end == '"' && *(end - 1) != '\\') break;
            end++;
        }
        if (*end == '"') {
            *end = '\0';
            tiger_json_unescape(text_key, out_text, out_max);
            return strlen(out_text) > 0;
        }
    }

    return false;
}

static void tiger_ai_show_models(void) {
    printf("\n%s================================================================================%s\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %sCATALOGO DE MODELOS Y EMPRESAS COMPATIBLES CON TIGER IA%s\n", TIGER_COLOR_GREEN, TIGER_COLOR_RESET);
    printf("%s================================================================================%s\n\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);

    printf("%s1. Google Gemini (:proveedor gemini)%s\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
    printf("   • gemini-flash-lite-latest   (Ultra-rápido, tier gratuito de alta cuota) [Recomendado]\n");
    printf("   • gemini-3.1-flash-lite      (Última generación ligera)\n");
    printf("   • gemini-2.5-flash           (Razonamiento equilibrado)\n");
    printf("   • gemini-1.5-pro             (Ventana de contexto masiva)\n\n");

    printf("%s2. Groq (:proveedor groq) - ¡Super Rápido & Tier Gratuito!%s\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
    printf("   • llama-3.3-70b-versatile    (Altísimo nivel, ultra-rápido LPU) [Recomendado]\n");
    printf("   • llama-3.1-8b-instant       (Respuesta instantánea)\n");
    printf("   • mixtral-8x7b-32768         (Excelente para código)\n\n");

    printf("%s3. DeepSeek (:proveedor deepseek) - ¡Ultra Económico & Potente!%s\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
    printf("   • deepseek-chat              (DeepSeek V3, equivalente a modelos premium)\n");
    printf("   • deepseek-reasoner          (DeepSeek R1, razonamiento matemático y código)\n\n");

    printf("%s4. OpenRouter (:proveedor openrouter) - ¡Modelos Libres y Multicloud!%s\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
    printf("   • meta-llama/llama-3.3-70b-instruct:free  (100%% Gratuito si te quedas sin saldo)\n");
    printf("   • deepseek/deepseek-r1:free               (Razonamiento 100%% gratis)\n");
    printf("   • openrouter/auto                         (Selección automática inteligente)\n\n");

    printf("%s5. OpenAI (:proveedor openai)%s\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
    printf("   • gpt-4o-mini                (Rápido, económico y capaz)\n");
    printf("   • gpt-4o                     (Modelo insignia multimodal)\n");
    printf("   • o3-mini                    (Razonamiento lógico avanzado)\n\n");

    printf("%s6. Anthropic Claude (:proveedor anthropic)%s\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
    printf("   • claude-3-5-sonnet-20241022 (Máxima excelencia en arquitectura de software)\n");
    printf("   • claude-3-5-haiku-20241022  (Económico y ágil)\n\n");

    printf("%s7. Ollama Local (:proveedor ollama) - ¡100%% Privado, Offline & Sin Créditos!%s\n", TIGER_COLOR_GREEN, TIGER_COLOR_RESET);
    printf("   • llama3, deepseek-r1, qwen2.5, mistral, phi3\n");
    printf("   • Funciona en http://localhost:11434 sin conexión a internet y sin costo alguno.\n\n");

    printf("%sUso rápido:%s\n", TIGER_COLOR_YELLOW, TIGER_COLOR_RESET);
    printf("  :proveedor <nombre>    -> Cambia de empresa proveedora.\n");
    printf("  :modelo <nombre>       -> Cambia el modelo activo.\n");
    printf("  :api <llave>           -> Asigna la API Key para el proveedor actual.\n\n");
}

static void tiger_ai_show_config(const TigerAIChatConfig* cfg) {
    if (!cfg) return;
    printf("\n%s╭── [TIGER IA CONFIGURACION] ────────────────────────────────────────────────%s\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
    printf("  Proveedor activo : %s%s%s\n", TIGER_COLOR_GREEN, cfg->provider[0] ? cfg->provider : "gemini", TIGER_COLOR_RESET);
    printf("  Modelo neuronal  : %s%s%s\n", TIGER_COLOR_YELLOW, cfg->model[0] ? cfg->model : "(default)", TIGER_COLOR_RESET);
    if (cfg->custom_endpoint[0]) {
        printf("  Endpoint custom  : %s%s%s\n", TIGER_COLOR_ORANGE, cfg->custom_endpoint, TIGER_COLOR_RESET);
    }
    if (strlen(cfg->api_key) > 4) {
        printf("  API Key          : %s****%s%s (Configurada)\n",
               TIGER_COLOR_ORANGE, cfg->api_key + strlen(cfg->api_key) - 4, TIGER_COLOR_RESET);
    } else if (strcasecmp(cfg->provider, "ollama") == 0) {
        printf("  API Key          : %sNo requerida (Local Offline)%s\n", TIGER_COLOR_GREEN, TIGER_COLOR_RESET);
    } else {
        printf("  API Key          : %sSin configurar (Escribe :api <llave>)%s\n", TIGER_COLOR_RED, TIGER_COLOR_RESET);
    }
    printf("  Auto-Fallback    : %s%s%s (Salto automático ante fin de cuota/créditos)\n",
           cfg->auto_fallback ? TIGER_COLOR_GREEN : TIGER_COLOR_RED,
           cfg->auto_fallback ? "ACTIVADO" : "DESACTIVADO", TIGER_COLOR_RESET);
    printf("  Historial chat   : %s%s%s\n", TIGER_COLOR_CYAN, cfg->current_chat_file, TIGER_COLOR_RESET);
    printf("%s╰───────────────────────────────────────────────────────────────────────────%s\n\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
}

bool tiger_ai_query_smart(TigerAIChatConfig* cfg, const char* user_prompt, char* out_text, size_t out_max) {
    if (!cfg || !user_prompt || !out_text || out_max == 0) return false;
    out_text[0] = '\0';
    bool is_quota = false;
    bool ok = false;

    const char* prov = cfg->provider[0] ? cfg->provider : "gemini";

    /* 1. Consulta al proveedor principal */
    if (strcasecmp(prov, "gemini") == 0) {
        ok = tiger_ai_query_gemini_ex(cfg->api_key, cfg->model, cfg->system_prompt, user_prompt, out_text, out_max, &is_quota);
    } else if (strcasecmp(prov, "openai") == 0) {
        ok = tiger_ai_query_openai_compat("https://api.openai.com/v1/chat/completions", cfg->api_key, cfg->model, cfg->system_prompt, user_prompt, out_text, out_max, &is_quota);
    } else if (strcasecmp(prov, "anthropic") == 0) {
        ok = tiger_ai_query_anthropic(cfg->api_key, cfg->model, cfg->system_prompt, user_prompt, out_text, out_max, &is_quota);
    } else if (strcasecmp(prov, "groq") == 0) {
        ok = tiger_ai_query_openai_compat("https://api.groq.com/openai/v1/chat/completions", cfg->api_key, cfg->model, cfg->system_prompt, user_prompt, out_text, out_max, &is_quota);
    } else if (strcasecmp(prov, "deepseek") == 0) {
        ok = tiger_ai_query_openai_compat("https://api.deepseek.com/chat/completions", cfg->api_key, cfg->model, cfg->system_prompt, user_prompt, out_text, out_max, &is_quota);
    } else if (strcasecmp(prov, "openrouter") == 0) {
        ok = tiger_ai_query_openai_compat("https://openrouter.ai/api/v1/chat/completions", cfg->api_key, cfg->model, cfg->system_prompt, user_prompt, out_text, out_max, &is_quota);
    } else if (strcasecmp(prov, "ollama") == 0) {
        const char* endpoint = cfg->custom_endpoint[0] ? cfg->custom_endpoint : "http://localhost:11434/v1/chat/completions";
        ok = tiger_ai_query_openai_compat(endpoint, "", cfg->model, cfg->system_prompt, user_prompt, out_text, out_max, &is_quota);
    } else {
        const char* endpoint = cfg->custom_endpoint[0] ? cfg->custom_endpoint : "http://localhost:11434/v1/chat/completions";
        ok = tiger_ai_query_openai_compat(endpoint, cfg->api_key, cfg->model, cfg->system_prompt, user_prompt, out_text, out_max, &is_quota);
    }

    if (ok && strlen(out_text) > 0) return true;

    /* 2. Recuperación en cascada inteligente ante agotamiento de créditos */
    if (cfg->auto_fallback) {
        if (is_quota) {
            printf("\n%s[TIGER IA - AVISO]: Cuota/creditos agotados en '%s' (%s). Activando fallback inteligente...%s\n",
                   TIGER_COLOR_YELLOW, prov, cfg->model[0] ? cfg->model : "default", TIGER_COLOR_RESET);
        }

        /* Fallback 1: Si Ollama está ejecutándose en localhost, usarlo inmediatamente (0 costo, ilimitado) */
        if (strcasecmp(prov, "ollama") != 0) {
            bool q_ol = false;
            if (tiger_ai_query_openai_compat("http://localhost:11434/v1/chat/completions", "", "llama3", cfg->system_prompt, user_prompt, out_text, out_max, &q_ol)) {
                printf("%s[TIGER IA - FALLBACK]: Conectado exitosamente con Ollama Local (0 creditos consumidos).%s\n",
                       TIGER_COLOR_GREEN, TIGER_COLOR_RESET);
                return true;
            }
        }

        /* Fallback 2: Si no estábamos en Gemini y hay clave de Gemini activa, probar Gemini Flash Lite */
        if (strcasecmp(prov, "gemini") != 0) {
            const char* gkey = getenv("GEMINI_API_KEY");
            if (!gkey || !*gkey) gkey = getenv("GOOGLE_API_KEY");
            if (gkey && *gkey) {
                bool q_g = false;
                if (tiger_ai_query_gemini_ex(gkey, "gemini-flash-lite-latest", cfg->system_prompt, user_prompt, out_text, out_max, &q_g)) {
                    printf("%s[TIGER IA - FALLBACK]: Recuperado mediante Google Gemini Flash Lite.%s\n",
                           TIGER_COLOR_GREEN, TIGER_COLOR_RESET);
                    return true;
                }
            }
        }
    }

    /* 3. Si se agotaron los créditos y no hubo proveedor secundario disponible, orientar al usuario */
    if (is_quota) {
        snprintf(out_text, out_max,
                 "⚠️ [TIGER IA - CUOTA O CRÉDITOS AGOTADOS]\n\n"
                 "El proveedor '%s' (modelo '%s') ha reportado saldo insuficiente o límite de cuota excedido (Error 429).\n\n"
                 "💡 ¿Cómo continuar de inmediato?\n"
                 "  • Cambiar de empresa proveedora:\n"
                 "      :proveedor groq        (Capa gratuita ultra-rápida)\n"
                 "      :proveedor openrouter  (Modelos libres con sufijo :free)\n"
                 "      :proveedor deepseek    (Alta potencia y precio mínimo)\n"
                 "      :proveedor openai      (Modelos ChatGPT GPT-4o / GPT-4o-mini)\n"
                 "      :proveedor anthropic   (Claude 3.5 Sonnet / Haiku)\n"
                 "      :proveedor ollama      (100%% Local, ilimitado y gratis en tu PC)\n\n"
                 "  • Asignar una nueva clave:\n"
                 "      :api <tu_nueva_llave>\n\n"
                 "  • Ver catálogo de modelos disponibles:\n"
                 "      :modelos\n\n"
                 "  • Ver configuración y estado actual:\n"
                 "      :config",
                 prov, cfg->model[0] ? cfg->model : "default");
        return true;
    }

    return false;
}

static void execute_code_helper(TigerEnv* env, const char* code, char* out_buf, size_t out_max) {
    if (!code || strlen(code) == 0) return;
    printf("\n%s>>> [TIGER IA RUNNER]: Ejecutando codigo dinamicamente...%s\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);

#ifdef _WIN32
    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
#endif

    bool ok = tiger_run_source(code, strlen(code), env, true);

#ifdef _WIN32
    QueryPerformanceCounter(&t1);
    double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
    if (out_buf) {
        snprintf(out_buf, out_max, "[Resultado]: %s | Tiempo: %.3f ms (ultra-rapido)",
                 ok ? "Ejecutado con EXITO" : "Error en ejecucion", ms);
    }
    printf("%s[TIGER IA RUNNER]: Ejecutado en %.3f ms | Estado: %s%s\n\n",
           TIGER_COLOR_GREEN, ms, ok ? "EXITO" : "ERROR", TIGER_COLOR_RESET);
#else
    if (out_buf) snprintf(out_buf, out_max, "[Resultado]: %s", ok ? "EXITO" : "ERROR");
#endif
}

static void generate_ai_response(TigerEnv* env, const TigerAIChatConfig* cfg, const char* user_prompt, char* out_resp, size_t out_max, const char* chat_file) {
    /* 1. Comandos de control directo dentro del chat */
    if (strncasecmp(user_prompt, ":leer ", 6) == 0 || strncasecmp(user_prompt, ":read ", 6) == 0) {
        const char* target_file = user_prompt + 6;
        while (*target_file == ' ') target_file++;
        size_t size = 0;
        char* file_content = tiger_read_file_resilient(target_file, &size);
        if (file_content) {
            snprintf(out_resp, out_max,
                     "He leido el archivo '%s' (%zu bytes):\n\n```tiger\n%.1500s%s\n```\n¿Deseas que lo analice, lo optimice o lo ejecute?",
                     target_file, size, file_content, size > 1500 ? "\n... (truncado para visualización)" : "");
            free(file_content);
        } else {
            snprintf(out_resp, out_max, "No se pudo encontrar o abrir el archivo '%s'. Verifica la ruta.", target_file);
        }
        return;
    }

    if (strncasecmp(user_prompt, ":ejecutar ", 10) == 0 || strncasecmp(user_prompt, ":exec ", 6) == 0) {
        const char* code = (user_prompt[1] == 'e' && user_prompt[2] == 'j') ? (user_prompt + 10) : (user_prompt + 6);
        while (*code == ' ') code++;
        char exec_summary[256];
        execute_code_helper(env, code, exec_summary, sizeof(exec_summary));
        snprintf(out_resp, out_max, "Codigo enviado al silicio de Tiger:\n```tiger\n%s\n```\n%s", code, exec_summary);
        return;
    }

    /* 2. Comandos de ayuda directa */
    if (strcasecmp(user_prompt, ":ayuda") == 0 || strcasecmp(user_prompt, ":help") == 0 ||
        strcasecmp(user_prompt, ":comandos") == 0 || strcasecmp(user_prompt, "ayuda") == 0 ||
        strcasecmp(user_prompt, "help") == 0) {
        snprintf(out_resp, out_max,
                 "Comandos rapidos disponibles en la terminal interactiva de la IA:\n"
                 "  :leer <archivo>       Lee cualquier archivo de proyecto y muestra su contenido.\n"
                 "  :ejecutar <codigo>    Ejecuta codigo Tiger dinamicamente en caliente.\n"
                 "  :sistemp <texto>      Configura el System Prompt de la IA.\n"
                 "  :api <llave>          Asigna tu API Key para modelos externos en la nube.\n"
                 "  :historial            Vuelve a mostrar el historial de la conversacion.\n"
                 "  :salir / exit         Guarda y cierra la sesion actual.");
        return;
    }

    /* 3. Consulta a la API de IA conectada (Multi-proveedor: Gemini, OpenAI, Anthropic, Groq, DeepSeek, OpenRouter, Ollama) */
    if (cfg && (cfg->is_configured || strcasecmp(cfg->provider, "ollama") == 0)) {
        printf("%s[TIGER IA]: Consultando modelo en %s (%s)...%s\n",
               TIGER_COLOR_CYAN,
               cfg->provider[0] ? cfg->provider : "gemini",
               cfg->model[0] ? cfg->model : "default",
               TIGER_COLOR_RESET);
        TigerAIChatConfig mutable_cfg = *cfg;
        if (tiger_ai_query_smart(&mutable_cfg, user_prompt, out_resp, out_max)) {
            return;
        }
        snprintf(out_resp, out_max,
                 "[TIGER IA - AVISO]: No se pudo conectar con el proveedor '%s'.\n"
                 "Escribe ':proveedor <nombre>' para cambiar de empresa o ':modelos' para ver opciones.",
                 cfg->provider[0] ? cfg->provider : "gemini");
        return;
    }

    /* 4. Detección de intenciones en lenguaje natural (Modo Copiloto / Silicio Offline) */
    if (tiger_casestr_fast(user_prompt, "primos") || tiger_casestr_fast(user_prompt, "prime")) {
        snprintf(out_resp, out_max,
                 "Para calculo masivo de numeros primos, Tiger utiliza su algoritmo nativo multihilo escalable hasta 8 subprocesadores con AVX2:\n\n"
                 "```tiger\n"
                 "# Calculo de primos hasta 500,000 en 8 trabajadores\n"
                 "res = contar_primos_hilos(500000 | 8).\n"
                 "IMPRIMIR \"Primos encontrados:\", res[\"primos\"], \"en\", res[\"tiempo_ms\"], \"ms\".\n"
                 "```\n"
                 "¿Deseas que lo ejecute en caliente ahora mismo? (Escribe ':ejecutar' seguido del codigo).");
        return;
    }

    if (tiger_casestr_fast(user_prompt, "memoria") || tiger_casestr_fast(user_prompt, "ram") || tiger_casestr_fast(user_prompt, "benchmark")) {
        snprintf(out_resp, out_max,
                 "En Tiger 10.2, la gestion de memoria cuenta con el 'Tiger Memory Benchmark 1.0' y el Planificador Adaptativo Inteligente:\n"
                 "* Sweet Spot en RAM: 4 trabajadores evitan la saturacion del bus DDR5, alcanzando 20.5 GB/s.\n"
                 "* Evaluacion automatica: 'planificador_auto_ajuste(\"TareaRAM\" | 2 | 67108864)' selecciona 4 hilos para memoria y 8 para calculo.\n"
                 "* Telemetria: Puedes consultar el estado del hardware con ASISTENTE(\"hardware\" | \"status\").");
        return;
    }

    /* 5. Respuesta contextual en Modo Silicio Offline */
    snprintf(out_resp, out_max,
             "Estas en modo 'Silicio Offline' (sin API Key de Gemini activa).\n\n"
             "Para activar la IA neuronal completa y conversar con ella, escribe:\n"
             "  :api <tu_clave_de_gemini>\n\n"
             "O puedes usar comandos locales:\n"
             "  :ejecutar <codigo tiger>   - Ejecuta codigo dinamicamente.\n"
             "  :leer <archivo>            - Lee un archivo de proyecto.");
}

void tiger_ai_start_chat_session(const char* chat_filename, const char* initial_prompt, const char* api_key, const char* system_prompt) {
    ensure_chats_dir();

    TigerAIChatConfig cfg;
    tiger_ai_load_config(&cfg);

    if (api_key && strlen(api_key) > 0) {
        strncpy(cfg.api_key, api_key, sizeof(cfg.api_key) - 1);
        cfg.is_configured = true;
        tiger_ai_save_config(&cfg);
    }
    if (system_prompt && strlen(system_prompt) > 0) {
        strncpy(cfg.system_prompt, system_prompt, sizeof(cfg.system_prompt) - 1);
        tiger_ai_save_config(&cfg);
    }

    /* Determinar el archivo de historial de chat */
    char full_chat_path[512];
    bool is_existing = false;

    char chats_dir[MAX_PATH];
    tiger_ai_get_chats_dir(chats_dir, sizeof(chats_dir));

    if (chat_filename && strlen(chat_filename) > 0) {
        if (strstr(chat_filename, "/") || strstr(chat_filename, "\\")) {
            snprintf(full_chat_path, sizeof(full_chat_path), "%s", chat_filename);
        } else {
            snprintf(full_chat_path, sizeof(full_chat_path), "%s/%s", chats_dir, chat_filename);
        }
        FILE* check_f = fopen(full_chat_path, "r");
        if (check_f) {
            is_existing = true;
            fclose(check_f);
        }
    } else {
        char fn_ts[64];
        get_filename_timestamp(fn_ts, sizeof(fn_ts));
        snprintf(full_chat_path, sizeof(full_chat_path), "%s/chat_%s.txt", chats_dir, fn_ts);
    }

    strncpy(cfg.current_chat_file, full_chat_path, sizeof(cfg.current_chat_file) - 1);

    print_ai_banner();

    printf("%s[SISTEMA]:%s Archivo de historial: %s%s%s%s\n",
           TIGER_COLOR_CYAN, TIGER_COLOR_RESET,
           TIGER_COLOR_GREEN, full_chat_path,
           is_existing ? " (Historial anterior recuperado)" : " (Nueva sesion creada)",
           TIGER_COLOR_RESET);

    printf("%s[CONFIG]:%s  Proveedor: %s%s%s | Modelo: %s%s%s\n",
           TIGER_COLOR_CYAN, TIGER_COLOR_RESET,
           TIGER_COLOR_GREEN, cfg.provider[0] ? cfg.provider : "gemini", TIGER_COLOR_RESET,
           TIGER_COLOR_YELLOW, cfg.model[0] ? cfg.model : "default", TIGER_COLOR_RESET);

    if (cfg.is_configured && strlen(cfg.api_key) > 0) {
        printf("%s[CONFIG]:%s  API Key: %s****%s%s | Fallback Cuota: %s%s%s\n",
               TIGER_COLOR_CYAN, TIGER_COLOR_RESET,
               TIGER_COLOR_ORANGE, cfg.api_key + (strlen(cfg.api_key) > 4 ? strlen(cfg.api_key) - 4 : 0), TIGER_COLOR_RESET,
               cfg.auto_fallback ? TIGER_COLOR_GREEN : TIGER_COLOR_RED,
               cfg.auto_fallback ? "ACTIVO" : "INACTIVO", TIGER_COLOR_RESET);
    } else if (strcasecmp(cfg.provider, "ollama") == 0) {
        printf("%s[CONFIG]:%s  Modo: %sOllama Local Offline%s (Sin claves, 0 creditos)\n",
               TIGER_COLOR_CYAN, TIGER_COLOR_RESET, TIGER_COLOR_GREEN, TIGER_COLOR_RESET);
    } else {
        printf("%s[CONFIG]:%s  Modo: %sSilicio Offline%s (Escribe ':api <llave>' o ':proveedor <nombre>')\n",
               TIGER_COLOR_CYAN, TIGER_COLOR_RESET, TIGER_COLOR_YELLOW, TIGER_COLOR_RESET);
    }
    printf("--------------------------------------------------------------------------------\n");

    /* Si el archivo ya existía, mostrar su contenido formateado */
    if (is_existing) {
        load_and_display_history(full_chat_path);
    } else {
        char header[256];
        char ts[64];
        get_timestamp_str(ts, sizeof(ts));
        snprintf(header, sizeof(header), "INICIO DE CONVERSACION TIGER IA - SISTEMA: %s", cfg.system_prompt);
        log_to_chat_file(full_chat_path, "SISTEMA", header);
    }

    TigerEnv* env = tiger_env_new(NULL);
    static char s_chat_session_resp[4096];
    static char s_chat_session_input[4096];

    /* Si se proporcionó un prompt inicial en la línea de comandos, procesarlo */
    if (initial_prompt && strlen(initial_prompt) > 0) {
        printf("\n%s[USUARIO]:%s %s\n\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET, initial_prompt);
        log_to_chat_file(full_chat_path, "USUARIO", initial_prompt);

        generate_ai_response(env, &cfg, initial_prompt, s_chat_session_resp, sizeof(s_chat_session_resp), full_chat_path);

        printf("%s[TIGER IA]:%s\n%s\n\n", TIGER_COLOR_GREEN, TIGER_COLOR_RESET, s_chat_session_resp);
        log_to_chat_file(full_chat_path, "TIGER IA", s_chat_session_resp);
    }

    /* Bucle interactivo conversacional REPL */
    printf("%sEscribe tu consulta o ':ayuda' para ver comandos (:salir para terminar).%s\n\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);

    for (;;) {
        printf("%stiger-ai> %s", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
        fflush(stdout);

        if (!fgets(s_chat_session_input, sizeof(s_chat_session_input), stdin)) break;

        /* Eliminar saltos de línea y espacios finales */
        size_t len = strlen(s_chat_session_input);
        while (len > 0 && (s_chat_session_input[len - 1] == '\n' || s_chat_session_input[len - 1] == '\r' || s_chat_session_input[len - 1] == ' ' || s_chat_session_input[len - 1] == '\t')) {
            s_chat_session_input[--len] = '\0';
        }
        if (len == 0) continue;

        if (strcasecmp(s_chat_session_input, ":salir") == 0 || strcasecmp(s_chat_session_input, ":exit") == 0 ||
            strcasecmp(s_chat_session_input, "salir") == 0 || strcasecmp(s_chat_session_input, "exit") == 0) {
            printf("\n%s[TIGER IA]: Sesion guardada en '%s'. ¡Hasta pronto!%s\n",
                   TIGER_COLOR_GREEN, full_chat_path, TIGER_COLOR_RESET);
            break;
        }

        if (strncasecmp(s_chat_session_input, ":proveedor ", 11) == 0 || strncasecmp(s_chat_session_input, ":provider ", 10) == 0) {
            const char* prov = (s_chat_session_input[2] == 'o' || s_chat_session_input[2] == 'O') ? (s_chat_session_input + 11) : (s_chat_session_input + 10);
            while (*prov == ' ') prov++;
            strncpy(cfg.provider, prov, sizeof(cfg.provider) - 1);
            tiger_ai_set_default_model(&cfg);
            tiger_ai_save_config(&cfg);
            printf("%s[TIGER IA]: Proveedor cambiado a: '%s' | Modelo: '%s'%s\n\n",
                   TIGER_COLOR_GREEN, cfg.provider, cfg.model, TIGER_COLOR_RESET);
            continue;
        }

        if (strncasecmp(s_chat_session_input, ":modelo ", 8) == 0 || strncasecmp(s_chat_session_input, ":model ", 7) == 0) {
            const char* mod = (s_chat_session_input[5] == 'o' || s_chat_session_input[5] == 'O') ? (s_chat_session_input + 8) : (s_chat_session_input + 7);
            while (*mod == ' ') mod++;
            strncpy(cfg.model, mod, sizeof(cfg.model) - 1);
            tiger_ai_save_config(&cfg);
            printf("%s[TIGER IA]: Modelo actualizado exitosamente a: '%s'%s\n\n",
                   TIGER_COLOR_GREEN, cfg.model, TIGER_COLOR_RESET);
            continue;
        }

        if (strcasecmp(s_chat_session_input, ":modelos") == 0 || strcasecmp(s_chat_session_input, ":models") == 0) {
            tiger_ai_show_models();
            continue;
        }

        if (strcasecmp(s_chat_session_input, ":config") == 0 || strcasecmp(s_chat_session_input, ":estado") == 0 || strcasecmp(s_chat_session_input, ":status") == 0) {
            tiger_ai_show_config(&cfg);
            continue;
        }

        if (strncasecmp(s_chat_session_input, ":endpoint ", 10) == 0) {
            const char* ep = s_chat_session_input + 10;
            while (*ep == ' ') ep++;
            strncpy(cfg.custom_endpoint, ep, sizeof(cfg.custom_endpoint) - 1);
            tiger_ai_save_config(&cfg);
            printf("%s[TIGER IA]: Endpoint personalizado asignado: '%s'%s\n\n",
                   TIGER_COLOR_GREEN, cfg.custom_endpoint, TIGER_COLOR_RESET);
            continue;
        }

        if (strncasecmp(s_chat_session_input, ":fallback ", 10) == 0) {
            const char* fb = s_chat_session_input + 10;
            while (*fb == ' ') fb++;
            if (strcasecmp(fb, "on") == 0 || strcasecmp(fb, "si") == 0 || strcasecmp(fb, "1") == 0 || strcasecmp(fb, "true") == 0) {
                cfg.auto_fallback = true;
                printf("%s[TIGER IA]: Fallback automático ante agotamiento de créditos ACTIVADO.%s\n\n", TIGER_COLOR_GREEN, TIGER_COLOR_RESET);
            } else {
                cfg.auto_fallback = false;
                printf("%s[TIGER IA]: Fallback automático DESACTIVADO.%s\n\n", TIGER_COLOR_YELLOW, TIGER_COLOR_RESET);
            }
            tiger_ai_save_config(&cfg);
            continue;
        }

        if (strcasecmp(s_chat_session_input, ":ayuda") == 0 || strcasecmp(s_chat_session_input, ":help") == 0) {
            printf("\n%s╭── [COMANDOS DISPONIBLES EN TIGER IA] ────────────────────────────────────%s\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
            printf("  :proveedor <nombre>  Cambia de empresa (gemini, openai, anthropic, groq, deepseek, openrouter, ollama)\n");
            printf("  :modelo <nombre>     Cambia el modelo neuronal a utilizar\n");
            printf("  :modelos             Muestra catálogo de modelos y opciones 100%% gratuitas\n");
            printf("  :api <llave>         Configura la API Key para el proveedor actual\n");
            printf("  :endpoint <url>      Configura URL de servidor local/custom (Ollama, LM Studio)\n");
            printf("  :fallback <on|off>   Activa/desactiva contingencia ante agotamiento de créditos\n");
            printf("  :config / :estado    Muestra la configuración activa y estado de conexión\n");
            printf("  :sistemp <texto>     Personaliza las directivas del sistema (System Prompt)\n");
            printf("  :leer <archivo>      Lee e inyecta un archivo del proyecto en el contexto\n");
            printf("  :ejecutar <codigo>   Ejecuta código Tiger dinámicamente en caliente\n");
            printf("  :historial           Muestra el historial de la conversación actual\n");
            printf("  :limpiar / :clear    Limpia la pantalla de la terminal\n");
            printf("  :salir / exit        Guarda y cierra la sesión de IA\n");
            printf("%s╰───────────────────────────────────────────────────────────────────────────%s\n\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
            continue;
        }

        if (strncasecmp(s_chat_session_input, ":api ", 5) == 0 || strncasecmp(s_chat_session_input, "API ", 4) == 0) {
            const char* key = (s_chat_session_input[0] == ':') ? (s_chat_session_input + 5) : (s_chat_session_input + 4);
            while (*key == ' ') key++;
            strncpy(cfg.api_key, key, sizeof(cfg.api_key) - 1);
            cfg.is_configured = true;
            tiger_ai_save_config(&cfg);
            printf("%s[TIGER IA]: API Key configurada y guardada exitosamente.%s\n\n", TIGER_COLOR_GREEN, TIGER_COLOR_RESET);
            continue;
        }

        if (strncasecmp(s_chat_session_input, ":sistemp ", 9) == 0 || strncasecmp(s_chat_session_input, "SISTEMP ", 8) == 0) {
            const char* sys = (s_chat_session_input[0] == ':') ? (s_chat_session_input + 9) : (s_chat_session_input + 8);
            while (*sys == ' ') sys++;
            strncpy(cfg.system_prompt, sys, sizeof(cfg.system_prompt) - 1);
            tiger_ai_save_config(&cfg);
            printf("%s[TIGER IA]: System Prompt actualizado exitosamente a: '%s'%s\n\n",
                   TIGER_COLOR_GREEN, cfg.system_prompt, TIGER_COLOR_RESET);
            continue;
        }

        if (strcasecmp(s_chat_session_input, ":historial") == 0 || strcasecmp(s_chat_session_input, ":history") == 0) {
            load_and_display_history(full_chat_path);
            continue;
        }

        if (strcasecmp(s_chat_session_input, ":limpiar") == 0 || strcasecmp(s_chat_session_input, ":clear") == 0) {
#ifdef _WIN32
            system("cls");
#else
            system("clear");
#endif
            print_ai_banner();
            continue;
        }

        /* Guardar la consulta del usuario en el archivo de historial */
        log_to_chat_file(full_chat_path, "USUARIO", s_chat_session_input);

        /* Generar respuesta de la IA */
        generate_ai_response(env, &cfg, s_chat_session_input, s_chat_session_resp, sizeof(s_chat_session_resp), full_chat_path);

        /* Imprimir con estética Tiger */
        printf("\n%s╭── [TIGER IA] ─────────────────────────────────────────────────────────────%s\n", TIGER_COLOR_GREEN, TIGER_COLOR_RESET);
        printf("%s\n", s_chat_session_resp);
        printf("%s╰───────────────────────────────────────────────────────────────────────────%s\n\n", TIGER_COLOR_GREEN, TIGER_COLOR_RESET);

        /* Guardar respuesta de la IA en el historial */
        log_to_chat_file(full_chat_path, "TIGER IA", s_chat_session_resp);
    }

    tiger_env_free(env);
}

int tiger_ai_chat_cli(int argc, char** argv) {
    /* argv[1] es "IA" o "AI". Parsear argumentos adicionales:
       tiger AI chat_datos.txt
       tiger AI API llave SISTEMP "actua..." "prompt..."
    */
    const char* chat_file = NULL;
    char api_key[256];
    char sys_prompt[1024];
    char prompt_acc[2048];
    api_key[0] = '\0';
    sys_prompt[0] = '\0';
    prompt_acc[0] = '\0';

    for (int i = 2; i < argc; i++) {
        if ((strcasecmp(argv[i], "API") == 0 || strcasecmp(argv[i], "API_KEY") == 0) && i + 1 < argc) {
            strncpy(api_key, argv[++i], sizeof(api_key) - 1);
        } else if ((strcasecmp(argv[i], "SISTEMP") == 0 || strcasecmp(argv[i], "SYSTEMP") == 0 ||
                    strcasecmp(argv[i], "SISTEMA") == 0) && i + 1 < argc) {
            sys_prompt[0] = '\0';
            while (i + 1 < argc) {
                if (strcasecmp(argv[i + 1], "API") == 0 || strcasecmp(argv[i + 1], "API_KEY") == 0 ||
                    strstr(argv[i + 1], ".txt") != NULL) {
                    break;
                }
                if (sys_prompt[0] != '\0') strncat(sys_prompt, " ", sizeof(sys_prompt) - strlen(sys_prompt) - 1);
                strncat(sys_prompt, argv[++i], sizeof(sys_prompt) - strlen(sys_prompt) - 1);
            }
        } else if (strstr(argv[i], ".txt") != NULL && !chat_file) {
            chat_file = argv[i];
        } else {
            /* Acumular como parte del prompt */
            if (prompt_acc[0] != '\0') strncat(prompt_acc, " ", sizeof(prompt_acc) - strlen(prompt_acc) - 1);
            strncat(prompt_acc, argv[i], sizeof(prompt_acc) - strlen(prompt_acc) - 1);
        }
    }

    tiger_ai_start_chat_session(chat_file, prompt_acc[0] ? prompt_acc : NULL,
                                api_key[0] ? api_key : NULL,
                                sys_prompt[0] ? sys_prompt : NULL);
    return 0;
}

int tiger_mcp_server_run(void) {
    /* Desactivar buffering en stdin y stdout para respuesta inmediata */
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    TigerAIChatConfig cfg;
    tiger_ai_load_config(&cfg);

    TigerEnv* env = tiger_env_new(NULL);

    while (fgets(s_mcp_line_buf, sizeof(s_mcp_line_buf), stdin)) {
        size_t len = strlen(s_mcp_line_buf);
        while (len > 0 && (s_mcp_line_buf[len - 1] == '\r' || s_mcp_line_buf[len - 1] == '\n')) {
            s_mcp_line_buf[--len] = '\0';
        }
        if (len == 0) continue;

        /* Extraer id si existe: "id": 1 o "id": "1" */
        char id_str[64] = "1";
        char* id_pos = strstr(s_mcp_line_buf, "\"id\":");
        if (!id_pos) id_pos = strstr(s_mcp_line_buf, "\"id\" :");
        if (id_pos) {
            char* val = strchr(id_pos, ':');
            if (val) {
                val++;
                while (*val == ' ') val++;
                size_t idx = 0;
                while (*val && *val != ',' && *val != '}' && *val != ' ' && idx < sizeof(id_str) - 1) {
                    id_str[idx++] = *val++;
                }
                id_str[idx] = '\0';
            }
        }

        /* 1. Protocolo MCP: initialize */
        if (strstr(s_mcp_line_buf, "\"method\":\"initialize\"") || strstr(s_mcp_line_buf, "\"method\": \"initialize\"")) {
            printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"tiger-mcp\",\"version\":\"10.2\"}}}\n", id_str);
            fflush(stdout);
            continue;
        }

        /* 2. Protocolo MCP: notifications/initialized */
        if (strstr(s_mcp_line_buf, "notifications/initialized")) {
            continue;
        }

        /* 3. Protocolo MCP: ping */
        if (strstr(s_mcp_line_buf, "\"method\":\"ping\"") || strstr(s_mcp_line_buf, "\"method\": \"ping\"")) {
            printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{}}\n", id_str);
            fflush(stdout);
            continue;
        }

        /* 4. Protocolo MCP: tools/list */
        if (strstr(s_mcp_line_buf, "\"method\":\"tools/list\"") || strstr(s_mcp_line_buf, "\"method\": \"tools/list\"")) {
            printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"tools\":["
                   "{\"name\":\"tiger_chat\",\"description\":\"Envia un mensaje a la IA de Tiger con su API activa y obtiene respuesta de razonamiento y codigo.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"mensaje\":{\"type\":\"string\",\"description\":\"Mensaje para la IA de Tiger\"}},\"required\":[\"mensaje\"]}},"
                   "{\"name\":\"tiger_eval\",\"description\":\"Ejecuta codigo del lenguaje Tiger 10.2 nativamente en silicio.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"codigo\":{\"type\":\"string\",\"description\":\"Codigo fuente Tiger (.tg)\"}},\"required\":[\"codigo\"]}},"
                   "{\"name\":\"tiger_read_file\",\"description\":\"Lee un archivo del proyecto.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"ruta\":{\"type\":\"string\",\"description\":\"Ruta del archivo\"}},\"required\":[\"ruta\"]}},"
                   "{\"name\":\"tiger_hardware_status\",\"description\":\"Consulta telemetria del silicio (Cores, SIMD AVX2, RAM).\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"
                   "]}}\n", id_str);
            fflush(stdout);
            continue;
        }

        /* 5. Protocolo MCP: tools/call */
        if (strstr(s_mcp_line_buf, "\"method\":\"tools/call\"") || strstr(s_mcp_line_buf, "\"method\": \"tools/call\"")) {
            snprintf(s_mcp_result_text, sizeof(s_mcp_result_text), "Operacion completada.");

            if (strstr(s_mcp_line_buf, "\"name\":\"tiger_chat\"") || strstr(s_mcp_line_buf, "\"name\": \"tiger_chat\"")) {
                char prompt[2048] = "Hola Tiger";
                char* p_pos = strstr(s_mcp_line_buf, "\"mensaje\":");
                if (!p_pos) p_pos = strstr(s_mcp_line_buf, "\"prompt\":");
                if (p_pos) {
                    char* q1 = strchr(p_pos, '"');
                    if (q1) q1 = strchr(q1 + 1, '"');
                    if (q1) q1 = strchr(q1 + 1, '"');
                    if (q1) {
                        char* q2 = strchr(q1 + 1, '"');
                        if (q2) {
                            size_t plen = (size_t)(q2 - (q1 + 1));
                            if (plen >= sizeof(prompt)) plen = sizeof(prompt) - 1;
                            strncpy(prompt, q1 + 1, plen);
                            prompt[plen] = '\0';
                        }
                    }
                }
                if (!tiger_ai_query_smart(&cfg, prompt, s_mcp_result_text, sizeof(s_mcp_result_text))) {
                    snprintf(s_mcp_result_text, sizeof(s_mcp_result_text), "Tiger IA: Mensaje recibido: '%s'", prompt);
                }
            } else if (strstr(s_mcp_line_buf, "\"name\":\"tiger_eval\"") || strstr(s_mcp_line_buf, "\"name\": \"tiger_eval\"")) {
                char code[2048] = "IMPRIMIR \"Tiger OK\".";
                char* c_pos = strstr(s_mcp_line_buf, "\"codigo\":");
                if (c_pos) {
                    char* q1 = strchr(c_pos, ':');
                    if (q1) q1 = strchr(q1, '"');
                    if (q1) {
                        char* q2 = strchr(q1 + 1, '"');
                        if (q2) {
                            size_t clen = (size_t)(q2 - (q1 + 1));
                            if (clen >= sizeof(code)) clen = sizeof(code) - 1;
                            strncpy(code, q1 + 1, clen);
                            code[clen] = '\0';
                        }
                    }
                }
                bool ok = tiger_run_source(code, strlen(code), env, true);
                snprintf(s_mcp_result_text, sizeof(s_mcp_result_text), "[Tiger Engine]: %s al ejecutar codigo.", ok ? "EXITO" : "ERROR");
            } else if (strstr(s_mcp_line_buf, "\"name\":\"tiger_read_file\"") || strstr(s_mcp_line_buf, "\"name\": \"tiger_read_file\"")) {
                char path[512] = "README.md";
                char* r_pos = strstr(s_mcp_line_buf, "\"ruta\":");
                if (r_pos) {
                    char* q1 = strchr(r_pos, ':');
                    if (q1) q1 = strchr(q1, '"');
                    if (q1) {
                        char* q2 = strchr(q1 + 1, '"');
                        if (q2) {
                            size_t rlen = (size_t)(q2 - (q1 + 1));
                            if (rlen >= sizeof(path)) rlen = sizeof(path) - 1;
                            strncpy(path, q1 + 1, rlen);
                            path[rlen] = '\0';
                        }
                    }
                }
                size_t sz = 0;
                char* content = tiger_read_file_resilient(path, &sz);
                if (content) {
                    snprintf(s_mcp_result_text, sizeof(s_mcp_result_text), "Contenido de '%s' (%zu bytes):\n%.4000s", path, sz, content);
                    free(content);
                } else {
                    snprintf(s_mcp_result_text, sizeof(s_mcp_result_text), "Error al leer '%s'.", path);
                }
            } else if (strstr(s_mcp_line_buf, "\"name\":\"tiger_hardware_status\"") || strstr(s_mcp_line_buf, "\"name\": \"tiger_hardware_status\"")) {
                snprintf(s_mcp_result_text, sizeof(s_mcp_result_text), "Hardware Tiger 10.2: Silicio Dual Maestro activo, AVX2 SIMD 256-bit, Arena Memory 64MB.");
            }

            s_mcp_esc_res[0] = '\0';
            tiger_json_escape(s_mcp_result_text, s_mcp_esc_res, sizeof(s_mcp_esc_res));
            printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"%s\"}]}}\n", id_str, s_mcp_esc_res);
            fflush(stdout);
            continue;
        }

        /* 6. Si el usuario envia un mensaje en texto plano (no JSON-RPC) al servidor */
        if (s_mcp_line_buf[0] != '{') {
            s_mcp_result_text[0] = '\0';
            if (tiger_ai_query_smart(&cfg, s_mcp_line_buf, s_mcp_result_text, sizeof(s_mcp_result_text))) {
                printf("[Tiger IA]: %s\n", s_mcp_result_text);
            } else {
                printf("[Tiger IA]: Mensaje recibido: '%s'\n", s_mcp_line_buf);
            }
            fflush(stdout);
            continue;
        }

        printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{}}\n", id_str);
        fflush(stdout);
    }

    tiger_env_free(env);
    return 0;
}
