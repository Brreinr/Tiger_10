/**
 * ============================================================================
 * TIGER AUTO-LINKING WRAPPER FOR WINDOWS.H
 * ============================================================================
 * Enlaza automáticamente 'user32', 'gdi32' y 'kernel32' para que cualquier
 * bloque TYPE C en Tiger pueda usar la API Win32 completa sin errores de linker.
 * ============================================================================
 */

#ifndef _TIGER_WINDOWS_WRAPPER_H
#define _TIGER_WINDOWS_WRAPPER_H

#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")
#pragma comment(lib, "kernel32")

#include <winapi/windows.h>

#endif /* _TIGER_WINDOWS_WRAPPER_H */

