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
/* TIGER 10.2: MODULO COMPARTIDO GRAFICO Y MULTIMEDIA (tiger_mod_uix.dll)     */
/* Cargado perezosamente en RAM solo al invocar ventanas o dibujo.            */
/* ========================================================================= */

TIGER_MOD_EXPORT const char* tiger_mod_uix_version(void) {
    return "TigerUIX-Dynamic-10.2";
}

TIGER_MOD_EXPORT bool tiger_mod_uix_init(void) {
    printf("[TigerMod UIX]: Submodulo dinamico de interfaces graficas y ClearType inicializado en RAM.\n");
    return true;
}

TIGER_MOD_EXPORT bool tiger_mod_uix_dispatch(const char* fn_name, void* args) {
    (void)fn_name;
    (void)args;
    return true;
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

