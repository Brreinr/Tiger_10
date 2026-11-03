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
/* TIGER 10.2: MODULO COMPARTIDO COPILOTO IA Y MCP (tiger_mod_ai.dll)        */
/* Cargado perezosamente en RAM solo al invocar comandos de IA o servidor MCP */
/* ========================================================================= */

TIGER_MOD_EXPORT const char* tiger_mod_ai_version(void) {
    return "TigerAI-MCP-Dynamic-10.2";
}

TIGER_MOD_EXPORT bool tiger_mod_ai_init(void) {
    printf("[TigerMod AI]: Submodulo dinamico de Copiloto IA y servidor MCP cargado en RAM.\n");
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

