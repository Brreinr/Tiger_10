/**
 * ============================================================================
 * TIGER ENGINE — NATIVE WINDOW & GUI RUNTIME (window.h / windows.h)
 * ============================================================================
 * Permite crear y controlar ventanas gráficas nativas de Windows directamente
 * desde bloques C en Tiger con cero dependencias externas y en pocas líneas.
 *
 * Características:
 *   - Auto-enlace automático de 'user32' y 'gdi32' sin flags extra de compilador.
 *   - tiger_crear_ventana("Titulo", ancho, alto)
 *   - tiger_ventana_activa(hwnd) / tiger_esperar_cierre(hwnd)
 *   - tiger_dibujar_rect(hwnd, x, y, w, h, color)
 *   - tiger_dibujar_texto(hwnd, x, y, texto, color)
 *   - tiger_tecla_presionada(VK_CODE)
 *   - tiger_mensaje("Titulo", "Mensaje") / tiger_confirmar("Titulo", "Pregunta")
 * ============================================================================
 */

#ifndef TIGER_WINDOW_H
#define TIGER_WINDOW_H

/* Auto-enlace para TCC y MSVC */
#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")
#pragma comment(lib, "kernel32")

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------------
 * ESTRUCTURAS Y ESTADO INTERNO DE LA VENTANA
 * ---------------------------------------------------------------------------- */
static int  _tiger_wnd_registrada = 0;
static HWND _tiger_ultima_ventana = NULL;

static LRESULT CALLBACK _TigerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_ERASEBKGND:
            return 1; /* Evitar parpadeo en repintado */
        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

static void _tiger_registrar_clase(void) {
    if (_tiger_wnd_registrada) return;
    
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEXA);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = _TigerWndProc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "TigerNativeWindowClass";

    RegisterClassExA(&wc);
    _tiger_wnd_registrada = 1;
}

/* ----------------------------------------------------------------------------
 * CREACIÓN Y CICLO DE VIDA DE LA VENTANA
 * ---------------------------------------------------------------------------- */

/**
 * Crea y muestra una ventana nativa de Windows centrada en la pantalla.
 */
static inline HWND tiger_crear_ventana(const char* titulo, int ancho, int alto) {
    _tiger_registrar_clase();

    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int pos_x = (screen_w - ancho) / 2;
    int pos_y = (screen_h - alto) / 2;
    if (pos_x < 0) pos_x = 100;
    if (pos_y < 0) pos_y = 100;

    RECT rc = { 0, 0, ancho, alto };
    DWORD estilo = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
    AdjustWindowRect(&rc, estilo, FALSE);
    int real_w = rc.right - rc.left;
    int real_h = rc.bottom - rc.top;

    HWND hwnd = CreateWindowExA(
        0,
        "TigerNativeWindowClass",
        titulo ? titulo : "Tiger Window",
        estilo,
        pos_x, pos_y, real_w, real_h,
        NULL, NULL, GetModuleHandleA(NULL), NULL
    );

    if (hwnd) {
        _tiger_ultima_ventana = hwnd;
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }
    return hwnd;
}

/**
 * Procesa la cola de eventos. Retorna 1 si la ventana sigue activa, 0 si se cerró.
 */
static inline int tiger_ventana_activa(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return 0;
    
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return 0;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return IsWindow(hwnd) ? 1 : 0;
}

/**
 * Mantiene la ventana abierta esperando hasta que el usuario la cierre con la 'X'.
 */
static inline void tiger_esperar_cierre(HWND hwnd) {
    if (!hwnd) return;
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

/**
 * Cierra y destruye la ventana.
 */
static inline void tiger_cerrar_ventana(HWND hwnd) {
    if (hwnd && IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
}

/* ----------------------------------------------------------------------------
 * DIÁLOGOS Y MENSAJES NATIVOS
 * ---------------------------------------------------------------------------- */

/**
 * Muestra un cuadro de diálogo informativo nativo de Windows.
 */
static inline void tiger_mensaje(const char* titulo, const char* texto) {
    MessageBoxA(NULL, texto ? texto : "", titulo ? titulo : "Tiger", MB_OK | MB_ICONINFORMATION);
}

/**
 * Muestra un cuadro de diálogo de advertencia.
 */
static inline void tiger_alerta_dialogo(const char* titulo, const char* texto) {
    MessageBoxA(NULL, texto ? texto : "", titulo ? titulo : "Aviso Tiger", MB_OK | MB_ICONWARNING);
}

/**
 * Muestra un cuadro de diálogo de pregunta (Sí/No). Retorna 1 si dijo Sí, 0 si No.
 */
static inline int tiger_confirmar(const char* titulo, const char* texto) {
    int res = MessageBoxA(NULL, texto ? texto : "", titulo ? titulo : "Confirmar", MB_YESNO | MB_ICONQUESTION);
    return (res == IDYES) ? 1 : 0;
}

/* ----------------------------------------------------------------------------
 * DIBUJO 2D BÁSICO EN PANTALLA (GDI NATIVO)
 * ---------------------------------------------------------------------------- */

/**
 * Limpia el área del cliente de la ventana con un color sólido.
 */
static inline void tiger_limpiar_ventana(HWND hwnd, COLORREF color) {
    if (!hwnd) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC hdc = GetDC(hwnd);
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);
    ReleaseDC(hwnd, hdc);
}

/**
 * Dibuja un rectángulo coloreado en la ventana (ideal para juegos 2D, barras, botones).
 */
static inline void tiger_dibujar_rect(HWND hwnd, int x, int y, int w, int h, COLORREF color) {
    if (!hwnd) return;
    HDC hdc = GetDC(hwnd);
    RECT rc = { x, y, x + w, y + h };
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);
    ReleaseDC(hwnd, hdc);
}

/**
 * Dibuja un texto en las coordenadas especificadas con color de fuente.
 */
static inline void tiger_dibujar_texto(HWND hwnd, int x, int y, const char* texto, COLORREF color) {
    if (!hwnd || !texto) return;
    HDC hdc = GetDC(hwnd);
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    TextOutA(hdc, x, y, texto, (int)strlen(texto));
    ReleaseDC(hwnd, hdc);
}

/* ----------------------------------------------------------------------------
 * ENTRADA DE TECLADO Y RATÓN
 * ---------------------------------------------------------------------------- */

/**
 * Verifica si una tecla está siendo presionada en tiempo real.
 * Ejemplo: tiger_tecla_presionada(VK_SPACE), tiger_tecla_presionada(VK_UP), 'A', 'W'
 */
static inline int tiger_tecla_presionada(int virtual_key) {
    return (GetAsyncKeyState(virtual_key) & 0x8000) ? 1 : 0;
}

/**
 * Obtiene la coordenada X del ratón relativa a la ventana.
 */
static inline int tiger_mouse_x(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    if (hwnd) ScreenToClient(hwnd, &pt);
    return pt.x;
}

/**
 * Obtiene la coordenada Y del ratón relativa a la ventana.
 */
static inline int tiger_mouse_y(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    if (hwnd) ScreenToClient(hwnd, &pt);
    return pt.y;
}

/**
 * Limita los fotogramas por segundo (pausa en milisegundos).
 */
static inline void tiger_esperar_fps(int fps) {
    if (fps <= 0) return;
    int ms = 1000 / fps;
    Sleep(ms > 0 ? ms : 1);
}

/* ----------------------------------------------------------------------------
 * COLORES PREDEFINIDOS CONVENIENTES
 * ---------------------------------------------------------------------------- */
#define TIGER_RGB_NARANJA RGB(255, 107, 0)
#define TIGER_RGB_BLANCO  RGB(255, 255, 255)
#define TIGER_RGB_NEGRO   RGB(18, 18, 18)
#define TIGER_RGB_VERDE   RGB(45, 210, 110)
#define TIGER_RGB_ROJO    RGB(255, 65, 54)
#define TIGER_RGB_AZUL    RGB(0, 150, 255)
#define TIGER_RGB_AMARILLO RGB(255, 230, 0)

/* Alias estándar en español */
#define abrir_ventana         tiger_crear_ventana
#define ventana_activa        tiger_ventana_activa
#define esperar_cierre        tiger_esperar_cierre
#define cerrar_ventana        tiger_cerrar_ventana
#define mensaje_dialogo       tiger_mensaje
#define dibujar_rect          tiger_dibujar_rect
#define dibujar_texto         tiger_dibujar_texto
#define tecla_presionada      tiger_tecla_presionada

#ifdef __cplusplus
}
#endif

#endif /* TIGER_WINDOW_H */
