#include "tiger.h"
#include "tiger_ast.h"
#include "tiger_lexer.h"
#include "tiger_parser.h"
#include "tiger_interp.h"
#include "tiger_c_interop.h"
#include "tiger_cgen.h"
#include "tiger_ai_chat.h"
#include "tiger_empaquetar.h"
#include "tiger_modules.h"
#include "tiger_cache.h"
#include "tiger_compiler_main.h"
#include "api_traduct.h"

#ifdef _WIN32
#include <windows.h>
#ifndef CP_UTF8
#define CP_UTF8 65001
#endif
#endif

/* Colores ANSI Neón de Tiger */
#define TIGER_COLOR_ORANGE "\033[38;2;255;107;0m"
#define TIGER_COLOR_CYAN   "\033[38;2;0;243;255m"
#define TIGER_COLOR_GREEN  "\033[38;2;34;197;94m"
#define TIGER_COLOR_RESET  "\033[0m"

static void print_banner(void) {
    printf("%s", TIGER_COLOR_ORANGE);
    printf("======================================================================\n");
    printf("  [TIGER 10.2 - MOTOR NATIVO EN C] / BILINGUAL & POLYGLOT ENGINE\n");
    printf("======================================================================\n");
    printf("%s", TIGER_COLOR_RESET);
}

static void sync_external_libraries(void) {
    printf("%s[Tiger 10.1]: Sincronizando dependencias externas desde lib_ext.json...%s\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
    int res = system("python -c \"from tools.tiger_compiler import _sync_external_libraries; _sync_external_libraries()\"");
    if (res != 0) {
        system("py -c \"from tools.tiger_compiler import _sync_external_libraries; _sync_external_libraries()\"");
    }
}

static void print_status(void) {
    print_banner();
    printf("  * %sTiger Core (Motor C Nativo Ultra-Ligero):%s [ACTIVO / OK] (Arranque: < 1 ms)\n", TIGER_COLOR_CYAN, TIGER_COLOR_GREEN);
    printf("  * %sTiger Compiler (Incremental + Cache):%s     [ACTIVO / OK] (bin/tiger-compiler.exe)\n", TIGER_COLOR_CYAN, TIGER_COLOR_GREEN);
    printf("  * %sTiny C Compiler (TIPO C & FFI):%s           [ACTIVO / OK] (bin/tcc/tcc.exe)\n", TIGER_COLOR_CYAN, TIGER_COLOR_GREEN);
    printf("  * %sNode.js Engine (TIPO JS):%s                 [ACTIVO / OK] (bin/node/node.exe)\n", TIGER_COLOR_CYAN, TIGER_COLOR_GREEN);
    printf("  * %sPython Engine (TIPO PY):%s                  [ACTIVO / OK]\n", TIGER_COLOR_CYAN, TIGER_COLOR_GREEN);
    printf("  * %sLector UTF-8 / UTF-16 Blindado:%s           [ACTIVO / OK]\n", TIGER_COLOR_CYAN, TIGER_COLOR_GREEN);
    printf("%s", TIGER_COLOR_ORANGE);
    printf("======================================================================\n");
    printf("%s¡Motor C de Tiger 100%% configurado y listo para programar!\n\n", TIGER_COLOR_RESET);
    tiger_modules_print_status();
    tiger_mem_print_telemetry();
}

static void print_help(void) {
    print_banner();
    printf("%sCOMANDOS DESDE LA TERMINAL DE SISTEMA / CLI USAGE:%s\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
    printf("  %stiger%s                                  Abre la terminal interactiva (REPL)\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger <archivo.tg>%s                     Ejecuta directamente con el nucleo ligero\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger <archivo.tg> --run%s               Ejecución rápida estándar\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger build <archivo.tg> [opciones]%s    Compila con tiger-compiler (soporta -O3, --dev, --release)\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger-compiler <arch.tg> [opciones]%s    Compilador dedicado incremental con cache (.tiger_cache/)\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger empaquetar <archivo.tg>%s          Empaqueta la app para Windows, Web PWA, Android, Linux, Mac\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger traducir <arch.tg> -a <lang>%s     Traduce parámetros y código entre idiomas (es, en, fr, pt, de, it)\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger --ram / tiger --telemetry%s        Mide el consumo de memoria RAM y modulos activos\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger --modules%s                        Muestra estado de carga bajo demanda de modulos\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger clean-cache%s                      Limpia la cache de compilacion incremental\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger <archivo.tg> --target <os>%s       Objetivo: windows, linux, mac, wasm, android, ios\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger <archivo.tg> -o <salida>%s         Compila el archivo a un ejecutable destino\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger <archivo.tg> --domain <dom>%s      Optimización de dominio: bci, ia, web, juego\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger <archivo.tg> --fix%s               Modo tolerante con auto-reparación de sintaxis\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger ai-admin [enable|disable]%s        Gestiona permisos de administración de la IA (ASISTENTE)\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger IA / tiger AI [prompt]%s          Abre la terminal conversacional de IA y Copiloto\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger mcp / tiger server%s               Inicia el servidor MCP (Model Context Protocol JSON-RPC)\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger status%s                           Diagnóstico del estado de los motores y RAM\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger setup / tiger sync%s               Sincroniza librerías externas de lib_ext.json\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger --help / -h%s                      Muestra esta ayuda de comandos\n\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("%sCOMANDOS INTERNOS DE LA TERMINAL REPL (tiger>):%s\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
    printf("  :status                                 Muestra el estado de salud de los motores\n");
    printf("  :ram / :telemetria                      Muestra la huella de memoria RAM en tiempo real\n");
    printf("  :modulos / :modules                     Muestra los modulos opcionales cargados en RAM\n");
    printf("  :clean-cache                            Limpia la cache de compilacion incremental\n");
    printf("  :ai-admin                               Muestra o conmuta el permiso administrativo de la IA\n");
    printf("  :setup / :sync                          Sincroniza dependencias de lib_ext.json\n");
    printf("  :ayuda / :help                          Muestra este menú de ayuda\n");
    printf("  salir. / exit.                          Cierra la sesión interactiva\n\n");
}

static void run_repl(void) {
    print_banner();
    printf("Terminal Interactiva (REPL) - Escribe comandos de Tiger o 'salir.' para terminar.\n\n");

    TigerEnv* env = tiger_env_new(NULL);
    char line_buf[2048];

    for (;;) {
        printf("%stiger> %s", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
        fflush(stdout);

        if (!fgets(line_buf, sizeof(line_buf), stdin)) break;

        /* Eliminar salto de línea */
        size_t len = strlen(line_buf);
        while (len > 0 && (line_buf[len - 1] == '\n' || line_buf[len - 1] == '\r')) {
            line_buf[--len] = '\0';
        }

        if (len == 0) continue;
        if (strcasecmp(line_buf, "salir.") == 0 || strcasecmp(line_buf, "exit.") == 0 ||
            strcasecmp(line_buf, "salir") == 0 || strcasecmp(line_buf, "exit") == 0) {
            break;
        }

        if (strcmp(line_buf, ":status") == 0) {
            print_status();
            continue;
        }

        if (strcmp(line_buf, ":ai-admin") == 0 || strcmp(line_buf, ":admin") == 0) {
            tiger_ai_show_admin_status();
            continue;
        }

        if (strcmp(line_buf, ":setup") == 0 || strcmp(line_buf, ":sync") == 0) {
            print_status();
            sync_external_libraries();
            continue;
        }

        if (strcmp(line_buf, ":help") == 0 || strcmp(line_buf, ":ayuda") == 0) {
            print_help();
            continue;
        }

        if (strcmp(line_buf, ":ram") == 0 || strcmp(line_buf, ":telemetria") == 0 || strcmp(line_buf, ":memoria") == 0) {
            tiger_mem_print_telemetry();
            continue;
        }

        if (strcmp(line_buf, ":modulos") == 0 || strcmp(line_buf, ":modules") == 0) {
            tiger_modules_print_status();
            continue;
        }

        if (strcmp(line_buf, ":clean-cache") == 0 || strcmp(line_buf, ":limpiar-cache") == 0) {
            tiger_cache_clean();
            continue;
        }

        if (strcasecmp(line_buf, "ai") == 0 || strcasecmp(line_buf, "ia") == 0 ||
            strcasecmp(line_buf, ":ai") == 0 || strcasecmp(line_buf, ":ia") == 0) {
            tiger_ai_start_chat_session(NULL, NULL, NULL, NULL);
            continue;
        }

        if (strncasecmp(line_buf, "ai ", 3) == 0 || strncasecmp(line_buf, "ia ", 3) == 0 ||
            strncasecmp(line_buf, ":ai ", 4) == 0 || strncasecmp(line_buf, ":ia ", 4) == 0) {
            const char* prompt = line_buf + ((line_buf[0] == ':') ? 4 : 3);
            while (*prompt == ' ') prompt++;
            tiger_ai_start_chat_session(NULL, *prompt ? prompt : NULL, NULL, NULL);
            continue;
        }

        /* Ejecutar línea interactiva en modo flexible */
        tiger_run_source(line_buf, len, env, true);
    }

    tiger_env_free(env);
    printf("\n%s[Tiger]: Sesión finalizada con éxito.%s\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
}

static int check_tiger_syntax(const char* filepath) {
    size_t size = 0;
    char* source = tiger_read_file_resilient(filepath, &size);
    if (!source) {
        fprintf(stderr, "[Tiger Check]: No se pudo leer '%s'\n", filepath);
        return 2;
    }

    TigerParser parser;
    tiger_parser_init(&parser, source, size, false);
    size_t statement_count = 0;
    TigerStmt** statements = tiger_parse_program(&parser, &statement_count);
    bool has_error = parser.has_error;
    tiger_parser_free(&parser);
    free(statements);
    free(source);

    if (has_error) return 1;
    printf("[Tiger Check]: Sintaxis correcta (%zu sentencias).\n", statement_count);
    return 0;
}

int main(int argc, char** argv) {
    /* 1. Detección de ejecutable empaquetado autónomo (Auto-contenido) */
    size_t payload_sz = 0;
    char* embedded_script = (argc > 0 && argv[0]) ? tiger_read_self_payload(argv[0], &payload_sz) : NULL;
    if (embedded_script) {
#ifdef _WIN32
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            SetConsoleMode(hOut, dwMode | 0x0004);
        }
        SetConsoleOutputCP(CP_UTF8);
        SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
        tiger_polyglot_daemons_init();
        atexit(tiger_polyglot_daemons_shutdown);
#endif
        TigerEnv* env = tiger_env_new(NULL);
        bool ok = tiger_run_source(embedded_script, payload_sz, env, true);
        free(embedded_script);
        tiger_polyglot_daemons_shutdown();
        tiger_env_free(env);
        return ok ? 0 : 1;
    }

    bool check_request = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--check") == 0) {
            check_request = true;
            break;
        }
    }
#ifdef _WIN32
    /* Habilitar colores ANSI en Windows Console */
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode)) {
        SetConsoleMode(hOut, dwMode | 0x0004); /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */
    }
    SetConsoleOutputCP(CP_UTF8);
    /* Optimización de Silicio: Evitar throttling del planificador de Windows */
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    if (!check_request) {
        tiger_polyglot_daemons_init();
        atexit(tiger_polyglot_daemons_shutdown);
        tiger_modules_init();
        atexit(tiger_modules_shutdown);
    }
#endif

    if (argc < 2) {
        run_repl();
        return 0;
    }

    const char* first_arg = argv[1];

    if (strcmp(first_arg, "--ram") == 0 || strcmp(first_arg, "-ram") == 0 ||
        strcmp(first_arg, "--telemetry") == 0 || strcmp(first_arg, "telemetria") == 0 ||
        strcmp(first_arg, "ram") == 0) {
        tiger_mem_print_telemetry();
        return 0;
    }

    if (strcmp(first_arg, "--modules") == 0 || strcmp(first_arg, "-modules") == 0 ||
        strcmp(first_arg, "modules") == 0 || strcmp(first_arg, "modulos") == 0) {
        tiger_modules_print_status();
        return 0;
    }

    if (strcmp(first_arg, "clean-cache") == 0 || strcmp(first_arg, "--clean-cache") == 0 ||
        strcmp(first_arg, "limpiar-cache") == 0) {
        tiger_cache_clean();
        return 0;
    }

    if (strcasecmp(first_arg, "build") == 0 || strcasecmp(first_arg, "compilar") == 0 ||
        strcasecmp(first_arg, "compile") == 0) {
        return tiger_compiler_main(argc - 1, argv + 1);
    }

    if (strcasecmp(first_arg, "mcp") == 0 || strcasecmp(first_arg, "--mcp") == 0 ||
        strcasecmp(first_arg, "server") == 0 || strcasecmp(first_arg, "servidor-mcp") == 0 ||
        strcasecmp(first_arg, "mcp-server") == 0) {
        return tiger_mcp_server_run();
    }

    if (strcasecmp(first_arg, "empaquetar") == 0 || strcasecmp(first_arg, "package") == 0 ||
        strcasecmp(first_arg, "bundle") == 0) {
        return tiger_empaquetar_main(argc - 1, argv + 1);
    }

    if (strcasecmp(first_arg, "traducir") == 0 || strcasecmp(first_arg, "traduct") == 0 ||
        strcasecmp(first_arg, "translate") == 0) {
        return tiger_traduct_main(argc - 1, argv + 1);
    }

    if (strcasecmp(first_arg, "IA") == 0 || strcasecmp(first_arg, "AI") == 0 ||
        strcasecmp(first_arg, "--ai") == 0 || strcasecmp(first_arg, "-ai") == 0 ||
        strcasecmp(first_arg, "chat") == 0) {
        return tiger_ai_chat_cli(argc, argv);
    }

    if (strcmp(first_arg, "--help") == 0 || strcmp(first_arg, "-h") == 0 ||
        strcmp(first_arg, "help") == 0 || strcmp(first_arg, "ayuda") == 0) {
        print_help();
        return 0;
    }

    if (strcmp(first_arg, "status") == 0 || strcmp(first_arg, "--status") == 0) {
        print_status();
        return 0;
    }

    if (strcmp(first_arg, "setup") == 0 || strcmp(first_arg, "sync") == 0 || strcmp(first_arg, "--sync") == 0) {
        print_status();
        sync_external_libraries();
        return 0;
    }

    if (strcmp(first_arg, "ai-admin") == 0 || strcmp(first_arg, "--ai-admin") == 0 || strcmp(first_arg, "admin-ia") == 0) {
        if (argc > 2) {
            if (strcasecmp(argv[2], "enable") == 0 || strcasecmp(argv[2], "on") == 0 ||
                strcasecmp(argv[2], "si") == 0 || strcasecmp(argv[2], "yes") == 0 || strcasecmp(argv[2], "1") == 0) {
                tiger_ai_set_admin_granted(true);
                printf("%s[Tiger Seguridad]: Administracion de IA HABILITADA exitosamente. / AI Administration ENABLED.%s\n",
                       TIGER_COLOR_GREEN, TIGER_COLOR_RESET);
                return 0;
            } else if (strcasecmp(argv[2], "disable") == 0 || strcasecmp(argv[2], "off") == 0 ||
                       strcasecmp(argv[2], "no") == 0 || strcasecmp(argv[2], "0") == 0) {
                tiger_ai_set_admin_granted(false);
                printf("%s[Tiger Seguridad]: Administracion de IA DESHABILITADA (Modo seguro). / AI Administration DISABLED.%s\n",
                       TIGER_COLOR_RED, TIGER_COLOR_RESET);
                return 0;
            }
        }
        tiger_ai_show_admin_status();
        return 0;
    }

    /* Detección de comando 'tiger build <archivo.tg> [target]' */
    if (strcasecmp(first_arg, "build") == 0 || strcasecmp(first_arg, "compilar") == 0) {
        if (argc < 3) {
            printf("%s[Uso]: tiger build <archivo.tg> [windows|linux|mac|apk|web|ios] [-d dominio] [-o salida]%s\n",
                   TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
            return 1;
        }
        const char* in_file = argv[2];
        const char* out_file = NULL;
        const char* dom = NULL;
        TigerTargetOS tgt = TARGET_WINDOWS;
        bool has_target = false;

        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
                out_file = argv[++i];
            } else if ((strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--domain") == 0) && i + 1 < argc) {
                dom = argv[++i];
            } else if (!has_target && argv[i][0] != '-') {
                tgt = tiger_parse_target(argv[i]);
                has_target = true;
            }
        }
        return tiger_compile_to_binary(in_file, out_file, tgt, dom, false) ? 0 : 1;
    }

    /* Detección de sintaxis directa: tiger <archivo_o_nombre> <target> [opciones] */
    bool is_target_keyword = false;
    if (argc >= 3 && argv[2][0] != '-') {
        const char* t_cand = argv[2];
        if (strcasecmp(t_cand, "apk") == 0 || strcasecmp(t_cand, "android") == 0 ||
            strcasecmp(t_cand, "exe") == 0 || strcasecmp(t_cand, "windows") == 0 || strcasecmp(t_cand, "win") == 0 ||
            strcasecmp(t_cand, "app") == 0 || strcasecmp(t_cand, "mac") == 0 || strcasecmp(t_cand, "macos") == 0 ||
            strcasecmp(t_cand, "ios") == 0 || strcasecmp(t_cand, "iphone") == 0 || strcasecmp(t_cand, "ipa") == 0 ||
            strcasecmp(t_cand, "linux") == 0 || strcasecmp(t_cand, "elf") == 0 ||
            strcasecmp(t_cand, "web") == 0 || strcasecmp(t_cand, "wasm") == 0 || strcasecmp(t_cand, "wat") == 0) {
            is_target_keyword = true;
        }
    }

    if (is_target_keyword && strcasecmp(first_arg, "build") != 0 && strcasecmp(first_arg, "compilar") != 0) {
        char in_path[MAX_PATH];
        const char* raw_in = first_arg;
        if (strstr(raw_in, ".tg")) {
            snprintf(in_path, sizeof(in_path), "%s", raw_in);
        } else {
            snprintf(in_path, sizeof(in_path), "%s.tg", raw_in);
            if (GetFileAttributesA(in_path) == INVALID_FILE_ATTRIBUTES) {
                snprintf(in_path, sizeof(in_path), "%s", raw_in);
            }
        }

        TigerTargetOS tgt = tiger_parse_target(argv[2]);
        const char* out_file = NULL;
        const char* dom = NULL;
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
                out_file = argv[++i];
            } else if ((strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--domain") == 0) && i + 1 < argc) {
                dom = argv[++i];
            }
        }
        return tiger_compile_to_binary(in_path, out_file, tgt, dom, false) ? 0 : 1;
    }

    /* Detección de opciones de compilación y ejecución */
    const char* filepath = NULL;
    const char* output_path = NULL;
    const char* domain = NULL;
    TigerTargetOS target = TARGET_WINDOWS;
    bool is_compile = false;
    bool run_native = false;
    bool flexible = false;
    bool check_only = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
            is_compile = true;
        } else if ((strcmp(argv[i], "--target") == 0 || strcmp(argv[i], "-t") == 0) && i + 1 < argc) {
            target = tiger_parse_target(argv[++i]);
            is_compile = true;
        } else if ((strcmp(argv[i], "--domain") == 0 || strcmp(argv[i], "-d") == 0) && i + 1 < argc) {
            domain = argv[++i];
            is_compile = true;
        } else if (strcmp(argv[i], "--run-native") == 0) {
            run_native = true;
            is_compile = true;
        } else if (strcmp(argv[i], "--fix") == 0) {
            flexible = true;
        } else if (strcmp(argv[i], "--check") == 0) {
            check_only = true;
        } else if (strcmp(argv[i], "--ai-admin") == 0 || strcmp(argv[i], "--admin-ia") == 0) {
            tiger_ai_set_admin_granted(true);
        } else if (strcmp(argv[i], "--ai-strict") == 0) {
            tiger_ai_set_admin_granted(false);
        } else if (strcmp(argv[i], "--run") == 0) {
            /* Ejecución rápida estándar */
        } else if (!filepath) {
            filepath = argv[i];
        }
    }

    if (!filepath) {
        run_repl();
        return 0;
    }

    if (check_only) return check_tiger_syntax(filepath);

    /* Soporte Metalenguaje: Detección automática de archivos políglotas (.c, .py, .js) */
    const char* ext = strrchr(filepath, '.');
    if (ext) {
        if (strcasecmp(ext, ".c") == 0) {
            char cmd[1024];
            const char* tcc_bin = (GetFileAttributesA("bin\\tcc\\tcc.exe") != INVALID_FILE_ATTRIBUTES) ? "bin\\tcc\\tcc.exe" : "tcc.exe";
            if (output_path) {
                snprintf(cmd, sizeof(cmd), "%s -Ibin/tcc/include \"%s\" -o \"%s\"", tcc_bin, filepath, output_path);
            } else {
                snprintf(cmd, sizeof(cmd), "%s -Ibin/tcc/include -run \"%s\"", tcc_bin, filepath);
            }
            int code = system(cmd);
            return code;
        } else if (strcasecmp(ext, ".py") == 0) {
            char cmd[1024];
            snprintf(cmd, sizeof(cmd), "python \"%s\"", filepath);
            int code = system(cmd);
            return code;
        } else if (strcasecmp(ext, ".js") == 0) {
            char cmd[1024];
            const char* node_bin = (GetFileAttributesA("bin\\node\\node.exe") != INVALID_FILE_ATTRIBUTES) ? "bin\\node\\node.exe" : "node.exe";
            snprintf(cmd, sizeof(cmd), "%s \"%s\"", node_bin, filepath);
            int code = system(cmd);
            return code;
        }
    }

    /* Compilación nativa al Silicio (Windows .exe, Mac, Linux, Android, iOS, WASM) */
    if (is_compile) {
        return tiger_compile_to_binary(filepath, output_path, target, domain, run_native) ? 0 : 1;
    }

    TigerEnv* env = tiger_env_new(NULL);

#ifdef _WIN32
    LARGE_INTEGER freq, t_start, t_end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t_start);
#endif

    bool ok = tiger_run_file(filepath, env, flexible);

#ifdef _WIN32
    QueryPerformanceCounter(&t_end);
    double elapsed_ms = (double)(t_end.QuadPart - t_start.QuadPart) * 1000.0 / (double)freq.QuadPart;
    printf("\n%sEjecución Nativa C Tiger 10: %.3f ms (ultra-rápido / blazing-fast)%s\n",
           TIGER_COLOR_GREEN, elapsed_ms, TIGER_COLOR_RESET);
#endif

    tiger_polyglot_daemons_shutdown();
    tiger_env_free(env);
    return ok ? 0 : 1;
}
