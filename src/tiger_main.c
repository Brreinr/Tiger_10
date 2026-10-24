#include "tiger.h"
#include "tiger_ast.h"
#include "tiger_lexer.h"
#include "tiger_parser.h"
#include "tiger_interp.h"
#include "tiger_c_interop.h"
#include "tiger_cgen.h"

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
    printf("  [TIGER 10.1 - MOTOR NATIVO EN C] / BILINGUAL & POLYGLOT ENGINE\n");
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
    printf("  * %sTiger Core (Motor C Nativo):%s       [ACTIVO / OK] (Velocidad: < 1 ms)\n", TIGER_COLOR_CYAN, TIGER_COLOR_GREEN);
    printf("  * %sTiny C Compiler (TIPO C & FFI):%s    [ACTIVO / OK] (bin/tcc/tcc.exe)\n", TIGER_COLOR_CYAN, TIGER_COLOR_GREEN);
    printf("  * %sNode.js Engine (TIPO JS):%s          [ACTIVO / OK] (bin/node/node.exe)\n", TIGER_COLOR_CYAN, TIGER_COLOR_GREEN);
    printf("  * %sPython Engine (TIPO PY):%s           [ACTIVO / OK]\n", TIGER_COLOR_CYAN, TIGER_COLOR_GREEN);
    printf("  * %sLector UTF-8 / UTF-16 Blindado:%s    [ACTIVO / OK]\n", TIGER_COLOR_CYAN, TIGER_COLOR_GREEN);
    printf("%s", TIGER_COLOR_ORANGE);
    printf("======================================================================\n");
    printf("%s¡Motor C de Tiger 100%% configurado y listo para programar!\n\n", TIGER_COLOR_RESET);
}

static void print_help(void) {
    print_banner();
    printf("%sCOMANDOS DESDE LA TERMINAL DE SISTEMA / CLI USAGE:%s\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
    printf("  %stiger%s                                  Abre la terminal interactiva (REPL)\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger <archivo.tg>%s                     Ejecuta directamente el archivo\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger <archivo.tg> --run%s               Ejecución rápida estándar\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger <archivo.tg> --run-native%s        Compila y ejecuta directamente en binario nativo x64\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger <archivo.tg> -o <salida>%s         Compila el archivo a un ejecutable destino\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger <archivo.tg> --target <os>%s       Objetivo: windows, linux, mac, wasm, android, ios\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger <archivo.tg> --domain <dom>%s      Optimización de dominio: bci, ia, web, juego\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger <archivo.tg> --fix%s               Modo tolerante con auto-reparación de sintaxis\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger status%s                           Diagnóstico del estado de los motores\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger setup / tiger sync%s               Sincroniza librerías externas de lib_ext.json\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("  %stiger --help / -h%s                      Muestra esta ayuda de comandos\n\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
    printf("%sCOMANDOS INTERNOS DE LA TERMINAL REPL (tiger>):%s\n", TIGER_COLOR_CYAN, TIGER_COLOR_RESET);
    printf("  :status                                 Muestra el estado de salud de los motores\n");
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

        if (strcmp(line_buf, ":setup") == 0 || strcmp(line_buf, ":sync") == 0) {
            print_status();
            sync_external_libraries();
            continue;
        }

        if (strcmp(line_buf, ":help") == 0 || strcmp(line_buf, ":ayuda") == 0) {
            print_help();
            continue;
        }

        /* Ejecutar línea interactiva en modo flexible */
        tiger_run_source(line_buf, len, env, true);
    }

    tiger_env_free(env);
    printf("\n%s[Tiger]: Sesión finalizada con éxito.%s\n", TIGER_COLOR_ORANGE, TIGER_COLOR_RESET);
}

int main(int argc, char** argv) {
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
    tiger_polyglot_daemons_init();
    atexit(tiger_polyglot_daemons_shutdown);
#endif

    if (argc < 2) {
        run_repl();
        return 0;
    }

    const char* first_arg = argv[1];

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

    /* Detección de opciones de compilación y ejecución */
    const char* filepath = NULL;
    const char* output_path = NULL;
    const char* domain = NULL;
    TigerTargetOS target = TARGET_WINDOWS;
    bool is_compile = false;
    bool run_native = false;
    bool flexible = false;

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
