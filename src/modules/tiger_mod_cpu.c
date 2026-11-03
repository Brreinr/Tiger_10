#include "tiger.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#define TIGER_MOD_EXPORT __declspec(dllexport)
#else
#define TIGER_MOD_EXPORT __attribute__((visibility("default")))
#endif

/* ========================================================================= */
/* TIGER 10.2: MODULO COMPARTIDO CPU PARALELO (tiger_mod_cpu.dll)            */
/* Cargado perezosamente en RAM solo al invocar tareas concurrentes masivas. */
/* ========================================================================= */

TIGER_MOD_EXPORT const char* tiger_mod_cpu_version(void) {
    return "TigerCPU-Parallel-Dynamic-10.2";
}

TIGER_MOD_EXPORT bool tiger_mod_cpu_init(void) {
    return true;
}

TIGER_MOD_EXPORT int tiger_mod_cpu_cores(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
#else
    return 4;
#endif
}

TIGER_MOD_EXPORT void tiger_mod_cpu_simd_add_i64(int64_t* dst, const int64_t* a, const int64_t* b, size_t count) {
    size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        dst[i + 0] = a[i + 0] + b[i + 0];
        dst[i + 1] = a[i + 1] + b[i + 1];
        dst[i + 2] = a[i + 2] + b[i + 2];
        dst[i + 3] = a[i + 3] + b[i + 3];
    }
    for (; i < count; i++) {
        dst[i] = a[i] + b[i];
    }
}

#ifdef _WIN32
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)hinstDLL;
    (void)lpvReserved;
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hinstDLL);
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
#endif

