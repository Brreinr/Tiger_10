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
/* TIGER 10.2: MODULO COMPARTIDO NPU TENSOR ENGINE (tiger_mod_npu.dll)       */
/* Cargado perezosamente en RAM solo al invocar aceleracion tensorial/NPU.   */
/* ========================================================================= */

TIGER_MOD_EXPORT const char* tiger_mod_npu_version(void) {
    return "TigerNPU-Tensor-Dynamic-10.2";
}

TIGER_MOD_EXPORT bool tiger_mod_npu_init(void) {
    return true;
}

TIGER_MOD_EXPORT double tiger_mod_npu_dot(const double* a, const double* b, size_t count) {
    double sum = 0.0;
    size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        sum += (a[i + 0] * b[i + 0]) +
               (a[i + 1] * b[i + 1]) +
               (a[i + 2] * b[i + 2]) +
               (a[i + 3] * b[i + 3]);
    }
    for (; i < count; i++) {
        sum += (a[i] * b[i]);
    }
    return sum;
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

