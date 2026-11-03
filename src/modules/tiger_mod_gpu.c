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
/* TIGER 10.2: MODULO COMPARTIDO GPU SHADERS Y COMPUTO (tiger_mod_gpu.dll)   */
/* Cargado perezosamente en RAM solo al invocar aceleracion de GPU.          */
/* ========================================================================= */

TIGER_MOD_EXPORT const char* tiger_mod_gpu_version(void) {
    return "TigerGPU-Dynamic-10.2";
}

TIGER_MOD_EXPORT bool tiger_mod_gpu_init(void) {
    return true;
}

TIGER_MOD_EXPORT const char* tiger_mod_gpu_adapter(void) {
#ifdef _WIN32
    static char buf[128] = "GPU Acelerada Direct GDI";
    DISPLAY_DEVICEA dd;
    ZeroMemory(&dd, sizeof(dd));
    dd.cb = sizeof(dd);
    if (EnumDisplayDevicesA(NULL, 0, &dd, 0)) {
        snprintf(buf, sizeof(buf), "%s", dd.DeviceString);
    }
    return buf;
#else
    return "GPU Standard Display Pipeline";
#endif
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

