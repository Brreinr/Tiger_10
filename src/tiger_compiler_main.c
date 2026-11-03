#include "tiger.h"
#include "tiger_cgen.h"
#include "tiger_cache.h"
#include "tiger_modules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#ifndef CP_UTF8
#define CP_UTF8 65001
#endif
#endif

static void print_banner(void) {
    printf("\n\033[38;2;255;107;0m  ████████╗██╗ ██████╗ ███████╗██████╗     ██████╗ ██████╗ ███╗   ███╗██████╗ \033[0m\n");
    printf("\033[38;2;255;107;0m  ╚══██╔══╝██║██╔════╝ ██╔════╝██╔══██╗   ██╔════╝██╔═══██╗████╗ ████║██╔══██╗\033[0m\n");
    printf("\033[38;2;255;107;0m     ██║   ██║██║  ███╗█████╗  ██████╔╝   ██║     ██║   ██║██╔████╔██║██████╔╝\033[0m\n");
    printf("\033[38;2;255;107;0m     ██║   ██║██║   ██║██╔══╝  ██╔══██╗   ██║     ██║   ██║██║╚██╔╝██║██╔═══╝ \033[0m\n");
    printf("\033[38;2;255;107;0m     ██║   ██║╚██████╔╝███████╗██║  ██║██╗╚██████╗╚██████╔╝██║ ╚═╝ ██║██║     \033[0m\n");
    printf("\033[38;2;255;107;0m     ╚═╝   ╚═╝ ╚═════╝ ╚══════╝╚═╝  ╚═╝╚═╝ ╚═════╝ ╚═════╝ ╚═╝     ╚═╝╚═╝     \033[0m\n");
    printf("================================================================================\n");
    printf("  🐯 TIGER 10.2: COMPILADOR DEDICADO E INCREMENTAL AL SILICIO (tiger-compiler.exe)\n");
    printf("================================================================================\n");
}

static void print_help(void) {
    print_banner();
    printf("Uso: tiger-compiler <archivo.tg> [opciones]\n\n");
    printf("Opciones de Optimizacion y Generacion:\n");
    printf("  -O0, -O1, -O2, -O3        Nivel de optimizacion LLVM (-O3 para silicio maximo)\n");
    printf("  --dev                     Modo Desarrollo: compilacion veloz (-O1), simbolos debug,\n");
    printf("                            ideal para iteracion rapida y depuracion.\n");
    printf("  --release                 Modo Produccion: optimizacion agresiva (-O3), strip de simbolos\n");
    printf("                            y maxima velocidad de ejecucion en hardware nativo.\n");
    printf("  --lto                     Link-Time Optimization (-flto) entre modulos.\n");
    printf("  --strip, -s               Elimina simbolos de depuracion en la distribucion final.\n\n");

    printf("Opciones de Destino y Salida:\n");
    printf("  -o, --salida <archivo>    Ruta del ejecutable o paquete destino\n");
    printf("  -t, --target <os>         Sistema objetivo: windows, linux, mac, apk, web, ios\n");
    printf("  -d, --domain <dom>        Dominio de aplicacion (ej: com.miempresa.app)\n");
    printf("  --run                     Ejecuta el binario tras compilar con exito\n\n");

    printf("Gestion de Cache Incremental (.tiger_cache/):\n");
    printf("  --no-cache, -f            Fuerza recompilacion ignorando el cache\n");
    printf("  --clean-cache             Vacia el cache de compilacion incremental\n");
    printf("  --stats, --ram            Muestra huella de RAM y telemetria de compilacion\n");
    printf("  -h, --help                Muestra este menu de ayuda\n\n");

    printf("Ejemplos:\n");
    printf("  tiger-compiler MiApp.tg --dev\n");
    printf("  tiger-compiler MiApp.tg --release --lto -o dist/app.exe\n");
    printf("  tiger-compiler MiApp.tg -t linux -d com.empresa.servidor\n\n");
}

int tiger_compiler_main(int argc, char** argv) {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode)) {
        SetConsoleMode(hOut, dwMode | 0x0004);
    }
    SetConsoleOutputCP(CP_UTF8);
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
#endif

    if (argc < 2) {
        print_help();
        return 0;
    }

    TigerCompileOptions opts;
    tiger_compile_options_default(&opts);

    const char* input_file = NULL;
    const char* output_file = NULL;
    bool show_stats = false;

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0 || strcmp(arg, "ayuda") == 0) {
            print_help();
            return 0;
        } else if (strcmp(arg, "--clean-cache") == 0) {
            tiger_cache_clean();
            return 0;
        } else if (strcmp(arg, "--dev") == 0) {
            opts.is_dev = true;
            opts.is_release = false;
            opts.opt_level = 1;
            opts.strip_symbols = false;
            opts.enable_lto = false;
        } else if (strcmp(arg, "--release") == 0) {
            opts.is_release = true;
            opts.is_dev = false;
            opts.opt_level = 3;
            opts.strip_symbols = true;
        } else if (strcmp(arg, "-O0") == 0) {
            opts.opt_level = 0;
        } else if (strcmp(arg, "-O1") == 0) {
            opts.opt_level = 1;
        } else if (strcmp(arg, "-O2") == 0) {
            opts.opt_level = 2;
        } else if (strcmp(arg, "-O3") == 0) {
            opts.opt_level = 3;
        } else if (strcmp(arg, "--lto") == 0) {
            opts.enable_lto = true;
        } else if (strcmp(arg, "--strip") == 0 || strcmp(arg, "-s") == 0) {
            opts.strip_symbols = true;
        } else if (strcmp(arg, "--no-cache") == 0 || strcmp(arg, "-f") == 0) {
            opts.force_rebuild = true;
        } else if (strcmp(arg, "--stats") == 0 || strcmp(arg, "--ram") == 0) {
            show_stats = true;
        } else if (strcmp(arg, "--run") == 0) {
            opts.run_after = true;
        } else if ((strcmp(arg, "-o") == 0 || strcmp(arg, "--salida") == 0) && i + 1 < argc) {
            output_file = argv[++i];
        } else if ((strcmp(arg, "-t") == 0 || strcmp(arg, "--target") == 0) && i + 1 < argc) {
            opts.target = tiger_parse_target(argv[++i]);
        } else if ((strcmp(arg, "-d") == 0 || strcmp(arg, "--domain") == 0) && i + 1 < argc) {
            opts.domain = argv[++i];
        } else if (arg[0] != '-') {
            input_file = arg;
        }
    }

    if (!input_file) {
        fprintf(stderr, "[Error]: Debes especificar el archivo .tg a compilar.\n");
        return 1;
    }

#ifdef _WIN32
    LARGE_INTEGER freq, t_start, t_end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t_start);
#endif

    bool ok = tiger_compile_with_options(input_file, output_file, &opts);

#ifdef _WIN32
    QueryPerformanceCounter(&t_end);
    double elapsed_ms = (double)(t_end.QuadPart - t_start.QuadPart) * 1000.0 / (double)freq.QuadPart;
    if (ok) {
        printf("\n\033[38;2;34;197;94m✨ [COMPILACION FINALIZADA]: Tiempo total del compilador: %.2f ms\033[0m\n", elapsed_ms);
    }
#endif

    if (show_stats) {
        tiger_mem_print_telemetry();
    }

    return ok ? 0 : 1;
}

#ifndef TIGER_BUILD_STANDALONE_LIB
int main(int argc, char** argv) {
    return tiger_compiler_main(argc, argv);
}
#endif
