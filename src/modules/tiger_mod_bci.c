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
/* TIGER 10.2: MODULO COMPARTIDO BCI Y NEURO-TELEMETRIA (tiger_mod_bci.dll)   */
/* Cargado perezosamente en RAM solo al invocar biosensores o telemetria BCI  */
/* ========================================================================= */

TIGER_MOD_EXPORT const char* tiger_mod_bci_version(void) {
    return "TigerBCI-Neuro-Dynamic-10.2";
}

static uint64_t g_mod_bci_regs[16] = {0};

TIGER_MOD_EXPORT bool tiger_mod_bci_init(void) {
    return true;
}

TIGER_MOD_EXPORT uint64_t tiger_mod_bci_read_reg(int reg) {
    if (reg >= 0 && reg < 16) return g_mod_bci_regs[reg];
    return 0;
}

TIGER_MOD_EXPORT void tiger_mod_bci_write_reg(int reg, uint64_t val) {
    if (reg >= 0 && reg < 16) g_mod_bci_regs[reg] = val;
}

TIGER_MOD_EXPORT void tiger_mod_bci_stream(void* dst, const void* src, size_t bytes) {
    if (!dst || !src || bytes == 0) return;
    memcpy(dst, src, bytes);
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

