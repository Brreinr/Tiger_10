#include <stddef.h>
#include <stdarg.h>

/* Simbolo obligatorio en Windows cuando se usa punto flotante */
int _fltused = 0x9875;

/* Mapeos a msvcrt */
extern char* _strdup(const char* s);
char* strdup(const char* s) {
    return _strdup(s);
}

extern int _vsnprintf(char* s, size_t n, const char* format, va_list arg);
int snprintf(char* s, size_t n, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int res = _vsnprintf(s, n, format, args);
    va_end(args);
    return res;
}

extern double _hypot(double x, double y);
double hypot(double x, double y) {
    return _hypot(x, y);
}

/* Helper de verificacion de pila */
void __chkstk(void) {}

/* Punto de entrada estándar para inicializar argc y argv desde msvcrt */
extern void __getmainargs(int *argc, char ***argv, char ***envp, int dowildcard, void *startinfo);
extern int main(int argc, char **argv);
extern void exit(int status);

typedef struct {
    int newmode;
} _STARTUPINFO;

#ifndef TIGER_NO_MAIN_STARTUP
void mainCRTStartup(void) {
    int argc = 0;
    char **argv = NULL;
    char **envp = NULL;
    _STARTUPINFO si = {0};
    __getmainargs(&argc, &argv, &envp, 0, &si);
    int ret = main(argc, argv);
    exit(ret);
}
#endif

int __stdcall _DllMainCRTStartup(void* hinstDLL, unsigned long fdwReason, void* lpvReserved) {
    (void)hinstDLL;
    (void)fdwReason;
    (void)lpvReserved;
    return 1;
}

