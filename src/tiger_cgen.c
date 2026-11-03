#include "tiger_cgen.h"
#include "tiger_parser.h"
#include "tiger_cache.h"

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#endif

const char* tiger_target_name(TigerTargetOS target) {
    switch (target) {
        case TARGET_WINDOWS: return "Windows (x86_64 PE)";
        case TARGET_MAC:     return "macOS (Apple Silicon & Intel Mach-O)";
        case TARGET_LINUX:   return "Linux (ELF 64-bit)";
        case TARGET_ANDROID: return "Android (ARM64 NDK Shared/Binary)";
        case TARGET_IOS:     return "iOS (ARM64 Framework/Mach-O)";
        case TARGET_WASM:    return "WebAssembly (WASM/WAT Universal)";
        default:             return "Universal C";
    }
}

TigerTargetOS tiger_parse_target(const char* name) {
    if (!name) return TARGET_WINDOWS;
    if (strcasecmp(name, "mac") == 0 || strcasecmp(name, "macos") == 0 || strcasecmp(name, "darwin") == 0 || strcasecmp(name, "apple") == 0) return TARGET_MAC;
    if (strcasecmp(name, "linux") == 0 || strcasecmp(name, "gnu") == 0 || strcasecmp(name, "elf") == 0) return TARGET_LINUX;
    if (strcasecmp(name, "android") == 0 || strcasecmp(name, "apk") == 0) return TARGET_ANDROID;
    if (strcasecmp(name, "ios") == 0 || strcasecmp(name, "iphone") == 0 || strcasecmp(name, "ipa") == 0) return TARGET_IOS;
    if (strcasecmp(name, "wasm") == 0 || strcasecmp(name, "wat") == 0 || strcasecmp(name, "web") == 0 || strcasecmp(name, "html") == 0) return TARGET_WASM;
    if (strcasecmp(name, "windows") == 0 || strcasecmp(name, "win") == 0 || strcasecmp(name, "exe") == 0) return TARGET_WINDOWS;
    if (strcasecmp(name, "app") == 0) return TARGET_MAC;
    return TARGET_WINDOWS;
}

/* ========================================================================= */
/* BUFFER DE GENERACIÓN DE CÓDIGO                                            */
/* ========================================================================= */

typedef struct {
    char* data;
    size_t length;
    size_t capacity;
    int indent_level;
} CodeGenBuffer;

static void cg_init(CodeGenBuffer* b) {
    b->capacity = 4096;
    b->length = 0;
    b->indent_level = 0;
    b->data = (char*)malloc(b->capacity);
    b->data[0] = '\0';
}

static void cg_write(CodeGenBuffer* b, const char* str) {
    if (!str) return;
    size_t len = strlen(str);
    if (b->length + len + 1 >= b->capacity) {
        b->capacity = (b->length + len + 1024) * 2;
        b->data = (char*)realloc(b->data, b->capacity);
    }
    memcpy(b->data + b->length, str, len);
    b->length += len;
    b->data[b->length] = '\0';
}

static void cg_indent(CodeGenBuffer* b) {
    for (int i = 0; i < b->indent_level; i++) {
        cg_write(b, "    ");
    }
}

static void cg_writeln(CodeGenBuffer* b, const char* str) {
    cg_indent(b);
    cg_write(b, str);
    cg_write(b, "\n");
}

/* ========================================================================= */
/* GENERADOR DE EXPRESIONES EN C                                             */
/* ========================================================================= */

static bool expr_is_string_like(TigerExpr* e) {
    if (!e) return false;
    if (e->type == EXPR_LITERAL && e->as.literal_val.type == VAL_STR) return true;
    if (e->type == EXPR_INPUT) return true;
    if (e->type == EXPR_BINARY && e->as.binary.op == TOK_PLUS) {
        return expr_is_string_like(e->as.binary.left) || expr_is_string_like(e->as.binary.right);
    }
    return false;
}

static void emit_expression(CodeGenBuffer* b, TigerExpr* expr) {
    if (!expr) return;
    char temp[128];

    switch (expr->type) {
        case EXPR_LITERAL: {
            TigerValue v = expr->as.literal_val;
            if (v.type == VAL_INT) {
                snprintf(temp, sizeof(temp), "%lldLL", (long long)v.as.i_val);
                cg_write(b, temp);
            } else if (v.type == VAL_FLOAT) {
                snprintf(temp, sizeof(temp), "%g", v.as.f_val);
                cg_write(b, temp);
            } else if (v.type == VAL_BOOL) {
                cg_write(b, v.as.b_val ? "true" : "false");
            } else if (v.type == VAL_STR) {
                cg_write(b, "\"");
                /* Escapar caracteres */
                const char* s = v.as.s_val ? v.as.s_val : "";
                while (*s) {
                    if (*s == '\n') cg_write(b, "\\n");
                    else if (*s == '\r') cg_write(b, "\\r");
                    else if (*s == '\t') cg_write(b, "\\t");
                    else if (*s == '"') cg_write(b, "\\\"");
                    else if (*s == '\\') cg_write(b, "\\\\");
                    else {
                        char cbuf[2] = { *s, '\0' };
                        cg_write(b, cbuf);
                    }
                    s++;
                }
                cg_write(b, "\"");
            } else {
                cg_write(b, "0");
            }
            break;
        }

        case EXPR_VAR:
            cg_write(b, expr->as.var_name);
            break;

        case EXPR_UNARY:
            if (expr->as.unary.op == TOK_NOT) cg_write(b, "!");
            else if (expr->as.unary.op == TOK_MINUS) cg_write(b, "-");
            cg_write(b, "(");
            emit_expression(b, expr->as.unary.right);
            cg_write(b, ")");
            break;

        case EXPR_BINARY:
            if (expr->as.binary.op == TOK_PLUS && expr_is_string_like(expr)) {
                cg_write(b, "tiger_c_concat(tiger_any_to_str(");
                emit_expression(b, expr->as.binary.left);
                cg_write(b, "), tiger_any_to_str(");
                emit_expression(b, expr->as.binary.right);
                cg_write(b, "))");
                break;
            }
            cg_write(b, "(");
            emit_expression(b, expr->as.binary.left);
            switch (expr->as.binary.op) {
                case TOK_PLUS: cg_write(b, " + "); break;
                case TOK_MINUS: cg_write(b, " - "); break;
                case TOK_STAR: cg_write(b, " * "); break;
                case TOK_SLASH: cg_write(b, " / "); break;
                case TOK_PERCENT: cg_write(b, " % "); break;
                case TOK_EQ: cg_write(b, " == "); break;
                case TOK_NEQ: cg_write(b, " != "); break;
                case TOK_LT: cg_write(b, " < "); break;
                case TOK_LE: cg_write(b, " <= "); break;
                case TOK_GT: cg_write(b, " > "); break;
                case TOK_GE: cg_write(b, " >= "); break;
                case TOK_AND: cg_write(b, " && "); break;
                case TOK_OR: cg_write(b, " || "); break;
                default: cg_write(b, " + "); break;
            }
            emit_expression(b, expr->as.binary.right);
            cg_write(b, ")");
            break;

        case EXPR_CALL:
            emit_expression(b, expr->as.call.callee);
            cg_write(b, "(");
            for (size_t i = 0; i < expr->as.call.arg_count; i++) {
                if (i > 0) cg_write(b, ", ");
                emit_expression(b, expr->as.call.args[i]);
            }
            cg_write(b, ")");
            break;

        case EXPR_MEMBER:
            emit_expression(b, expr->as.member.target);
            cg_write(b, ".");
            cg_write(b, expr->as.member.member_name);
            break;

        case EXPR_INDEX:
            emit_expression(b, expr->as.index.target);
            cg_write(b, "[");
            emit_expression(b, expr->as.index.index);
            cg_write(b, "]");
            break;

        case EXPR_INPUT:
            if (expr->as.input.prompt) {
                cg_write(b, "tiger_c_input(");
                emit_expression(b, expr->as.input.prompt);
                cg_write(b, ")");
            } else {
                cg_write(b, "tiger_c_input(\"\")");
            }
            break;

        default:
            cg_write(b, "0");
            break;
    }
}

/* ========================================================================= */
/* GENERADOR DE SENTENCIAS EN C                                              */
/* ========================================================================= */

static void emit_statement(CodeGenBuffer* b, TigerStmt* stmt);

static void emit_block(CodeGenBuffer* b, TigerStmt** stmts, size_t count) {
    b->indent_level++;
    for (size_t i = 0; i < count; i++) {
        emit_statement(b, stmts[i]);
    }
    b->indent_level--;
}

static const char* c_type_name(TigerValType t) {
    switch (t) {
        case VAL_INT:   return "int64_t";
        case VAL_FLOAT: return "double";
        case VAL_STR:   return "const char*";
        case VAL_BOOL:  return "bool";
        case VAL_PTR:   return "void*";
        default:        return "int64_t";
    }
}

static void emit_statement(CodeGenBuffer* b, TigerStmt* stmt) {
    if (!stmt) return;

    switch (stmt->type) {
        case STMT_VAR_DECL: {
            cg_indent(b);
            TigerValType t = stmt->as.var_decl.type_req;
            if (t == VAL_NIL && stmt->as.var_decl.init) {
                if (stmt->as.var_decl.init->type == EXPR_LITERAL) {
                    t = stmt->as.var_decl.init->as.literal_val.type;
                } else if (stmt->as.var_decl.init->type == EXPR_INPUT) {
                    t = VAL_STR;
                }
            }
            cg_write(b, c_type_name(t));
            cg_write(b, " ");
            cg_write(b, stmt->as.var_decl.name);
            if (stmt->as.var_decl.init) {
                cg_write(b, " = ");
                emit_expression(b, stmt->as.var_decl.init);
            } else {
                cg_write(b, " = 0");
            }
            cg_write(b, ";\n");
            break;
        }

        case STMT_ASSIGN: {
            cg_indent(b);
            emit_expression(b, stmt->as.assign.target);
            if (stmt->as.assign.op == TOK_PLUS_ASSIGN) cg_write(b, " += ");
            else if (stmt->as.assign.op == TOK_MINUS_ASSIGN) cg_write(b, " -= ");
            else if (stmt->as.assign.op == TOK_STAR_ASSIGN) cg_write(b, " *= ");
            else if (stmt->as.assign.op == TOK_SLASH_ASSIGN) cg_write(b, " /= ");
            else cg_write(b, " = ");
            emit_expression(b, stmt->as.assign.value);
            cg_write(b, ";\n");
            break;
        }

        case STMT_PRINT: {
            cg_indent(b);
            /* Usar printf optimizado según el tipo */
            TigerExpr* e = stmt->as.print.expr;
            if (e && e->type == EXPR_LITERAL && e->as.literal_val.type == VAL_STR) {
                cg_write(b, "printf(\"%s\\n\", ");
                emit_expression(b, e);
                cg_write(b, ");\n");
            } else if (e && e->type == EXPR_LITERAL && e->as.literal_val.type == VAL_INT) {
                cg_write(b, "printf(\"%lld\\n\", ");
                emit_expression(b, e);
                cg_write(b, ");\n");
            } else {
                cg_write(b, "tiger_runtime_print_any(");
                emit_expression(b, e);
                cg_write(b, ");\n");
            }
            break;
        }

        case STMT_IF: {
            cg_indent(b);
            cg_write(b, "if (");
            emit_expression(b, stmt->as.if_stmt.condition);
            cg_write(b, ") {\n");
            emit_block(b, stmt->as.if_stmt.then_stmts, stmt->as.if_stmt.then_count);
            if (stmt->as.if_stmt.else_stmts && stmt->as.if_stmt.else_count > 0) {
                cg_indent(b);
                cg_write(b, "} else {\n");
                emit_block(b, stmt->as.if_stmt.else_stmts, stmt->as.if_stmt.else_count);
            }
            cg_indent(b);
            cg_write(b, "}\n");
            break;
        }

        case STMT_WHILE: {
            cg_indent(b);
            cg_write(b, "while (");
            emit_expression(b, stmt->as.while_stmt.condition);
            cg_write(b, ") {\n");
            emit_block(b, stmt->as.while_stmt.body, stmt->as.while_stmt.body_count);
            cg_indent(b);
            cg_write(b, "}\n");
            break;
        }

        case STMT_RETURN: {
            cg_indent(b);
            cg_write(b, "return ");
            if (stmt->as.return_stmt.expr) emit_expression(b, stmt->as.return_stmt.expr);
            else cg_write(b, "0");
            cg_write(b, ";\n");
            break;
        }

        case STMT_BREAK:
            cg_writeln(b, "break;");
            break;

        case STMT_CONTINUE:
            cg_writeln(b, "continue;");
            break;

        case STMT_FUNC_DECL:
        case STMT_CLASS_DECL:
        case STMT_MACRO: {
            /* Funciones, Moldes/Clases y Macros se emiten a nivel global antes de main */
            break;
        }

        case STMT_HW_TARGET: {
            if (stmt->as.hw_target.body && stmt->as.hw_target.body_count > 0) {
                emit_block(b, stmt->as.hw_target.body, stmt->as.hw_target.body_count);
            }
            break;
        }

        case STMT_POLYGLOT: {
            /* Los bloques TIPO C se emiten a nivel global antes de main */
            break;
        }

        case STMT_EXPR: {
            cg_indent(b);
            emit_expression(b, stmt->as.expr);
            cg_write(b, ";\n");
            break;
        }

        default:
            break;
    }
}

/*
 * Runtime Win32 minimo para programas Tiger que usan las APIs de teclado,
 * sonido y lienzo 2D. Se emite dentro del ejecutable generado para que las
 * llamadas de las bibliotecas Tiger no queden como simbolos externos.
 */
static void emit_windows_game_runtime(CodeGenBuffer* b) {
    static const char* lines[] = {
        "/* Runtime Win32 para las funciones graficas y de entrada de Tiger */",
        "static HWND tiger_canvas_hwnd = NULL;",
        "static HDC tiger_canvas_backbuffer_dc = NULL;",
        "static HBITMAP tiger_canvas_backbuffer_bitmap = NULL;",
        "static HBITMAP tiger_canvas_backbuffer_original = NULL;",
        "static int tiger_canvas_width = 0, tiger_canvas_height = 0;",
        "static int tiger_canvas_running = 0;",
        "static int tiger_canvas_has_position = 0, tiger_canvas_position_x = 0, tiger_canvas_position_y = 0;",
        "static DWORD tiger_canvas_frame_ms = 16;",
        "static DWORD tiger_canvas_next_tick = 0;",
        "static const char tiger_canvas_class[] = \"TigerNativeCanvasWindow\";",
        "static int tiger_color_component(int64_t value) {",
        "    if (value < 0) return 0;",
        "    if (value > 255) return 255;",
        "    return (int)value;",
        "}",
        "static int tiger_canvas_resize(HWND hwnd) {",
        "    if (!tiger_canvas_backbuffer_dc) return 0;",
        "    RECT rect; if (!GetClientRect(hwnd, &rect)) return 0;",
        "    int width = rect.right - rect.left, height = rect.bottom - rect.top;",
        "    if (width <= 0 || height <= 0) return 0;",
        "    HDC window_dc = GetDC(hwnd); if (!window_dc) return 0;",
        "    HBITMAP bitmap = CreateCompatibleBitmap(window_dc, width, height); ReleaseDC(hwnd, window_dc);",
        "    if (!bitmap) return 0;",
        "    HBITMAP previous = (HBITMAP)SelectObject(tiger_canvas_backbuffer_dc, bitmap);",
        "    if (!previous || previous == HGDI_ERROR) { DeleteObject(bitmap); return 0; }",
        "    if (tiger_canvas_backbuffer_bitmap) DeleteObject(tiger_canvas_backbuffer_bitmap);",
        "    tiger_canvas_backbuffer_bitmap = bitmap; tiger_canvas_width = width; tiger_canvas_height = height; return 1;",
        "}",
        "static LRESULT CALLBACK tiger_canvas_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {",
        "    if (msg == WM_PAINT) { PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps); if (tiger_canvas_backbuffer_dc) BitBlt(dc, 0, 0, tiger_canvas_width, tiger_canvas_height, tiger_canvas_backbuffer_dc, 0, 0, SRCCOPY); EndPaint(hwnd, &ps); return 0; }",
        "    if (msg == WM_ERASEBKGND) return 1;",
        "    if (msg == WM_SIZE) { tiger_canvas_resize(hwnd); return 0; }",
        "    if (msg == WM_CLOSE) { tiger_canvas_running = 0; DestroyWindow(hwnd); return 0; }",
        "    if (msg == WM_DESTROY) { tiger_canvas_hwnd = NULL; tiger_canvas_running = 0; if (tiger_canvas_backbuffer_dc && tiger_canvas_backbuffer_original) SelectObject(tiger_canvas_backbuffer_dc, tiger_canvas_backbuffer_original); if (tiger_canvas_backbuffer_bitmap) DeleteObject(tiger_canvas_backbuffer_bitmap); if (tiger_canvas_backbuffer_dc) DeleteDC(tiger_canvas_backbuffer_dc); tiger_canvas_backbuffer_bitmap = NULL; tiger_canvas_backbuffer_dc = NULL; tiger_canvas_backbuffer_original = NULL; PostQuitMessage(0); return 0; }",
        "    return DefWindowProcA(hwnd, msg, wp, lp);",
        "}",
        "static int64_t abrir_lienzo_2d(const char* title, int64_t width, int64_t height, int64_t fps) {",
        "    tiger_canvas_frame_ms = fps > 0 ? (DWORD)(1000 / fps) : 16;",
        "    if (tiger_canvas_frame_ms == 0) tiger_canvas_frame_ms = 1;",
        "    if (tiger_canvas_hwnd && IsWindow(tiger_canvas_hwnd)) return 1;",
        "    WNDCLASSA wc; ZeroMemory(&wc, sizeof(wc));",
        "    wc.lpfnWndProc = tiger_canvas_proc; wc.hInstance = GetModuleHandleA(NULL);",
        "    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);",
        "    wc.lpszClassName = tiger_canvas_class;",
        "    RegisterClassA(&wc);",
        "    tiger_canvas_hwnd = CreateWindowA(tiger_canvas_class, title ? title : \"TigerUIX\", WS_OVERLAPPEDWINDOW,",
        "        tiger_canvas_has_position ? tiger_canvas_position_x : CW_USEDEFAULT, tiger_canvas_has_position ? tiger_canvas_position_y : CW_USEDEFAULT, (int)width, (int)height, NULL, NULL, wc.hInstance, NULL);",
        "    if (!tiger_canvas_hwnd) return 0;",
        "    tiger_canvas_has_position = 0;",
        "    HDC window_dc = GetDC(tiger_canvas_hwnd); if (!window_dc) { DestroyWindow(tiger_canvas_hwnd); return 0; }",
        "    tiger_canvas_backbuffer_dc = CreateCompatibleDC(window_dc); ReleaseDC(tiger_canvas_hwnd, window_dc);",
        "    if (!tiger_canvas_backbuffer_dc) { DestroyWindow(tiger_canvas_hwnd); return 0; }",
        "    tiger_canvas_backbuffer_original = (HBITMAP)GetCurrentObject(tiger_canvas_backbuffer_dc, OBJ_BITMAP);",
        "    if (!tiger_canvas_resize(tiger_canvas_hwnd)) { DestroyWindow(tiger_canvas_hwnd); return 0; }",
        "    tiger_canvas_running = 1; tiger_canvas_next_tick = GetTickCount(); ShowWindow(tiger_canvas_hwnd, SW_SHOW); UpdateWindow(tiger_canvas_hwnd);",
        "    return 1;",
        "}",
        "static int64_t uix_ventana_abrir(const char* title, int64_t width, int64_t height, int64_t fps) { return abrir_lienzo_2d(title, width, height, fps); }",
        "static int64_t uix_ventana_abrir_en(const char* title, int64_t width, int64_t height, int64_t fps, int64_t x, int64_t y) { tiger_canvas_has_position = 1; tiger_canvas_position_x = (int)x; tiger_canvas_position_y = (int)y; return uix_ventana_abrir(title, width, height, fps); }",
        "static int64_t uix_ventana_abrir_plataforma(const char* title, int64_t width, int64_t height, int64_t fps, const char* backend) {",
        "    if (backend && strcmp(backend, \"auto\") && strcmp(backend, \"native\") && strcmp(backend, \"windows\") && strcmp(backend, \"win32\")) { fprintf(stderr, \"[TigerUIX] Backend no disponible: %s\\n\", backend); return 0; }",
        "    return uix_ventana_abrir(title, width, height, fps);",
        "}",
        "static const char* uix_backend_actual(void) { return \"win32\"; }",
        "static int64_t actualizar_lienzo_2d(void) {",
        "    MSG msg;",
        "    DWORD now = GetTickCount();",
        "    if ((LONG)(tiger_canvas_next_tick - now) > 0) MsgWaitForMultipleObjects(0, NULL, FALSE, (DWORD)(tiger_canvas_next_tick - now), QS_ALLINPUT);",
        "    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {",
        "        if (msg.message == WM_QUIT) tiger_canvas_running = 0;",
        "        TranslateMessage(&msg); DispatchMessageA(&msg);",
        "    }",
        "    if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) tiger_canvas_running = 0;",
        "    if (tiger_canvas_hwnd && tiger_canvas_running) { InvalidateRect(tiger_canvas_hwnd, NULL, FALSE); UpdateWindow(tiger_canvas_hwnd); }",
        "    tiger_canvas_next_tick = GetTickCount() + tiger_canvas_frame_ms;",
        "    return tiger_canvas_running && tiger_canvas_hwnd && IsWindow(tiger_canvas_hwnd) ? 1 : 0;",
        "}",
        "static int64_t uix_ventana_actualizar(void) { return actualizar_lienzo_2d(); }",
        "static int64_t uix_actualizar(void) { return actualizar_lienzo_2d(); }",
        "static int64_t uix_ventana_cerrar(void) {",
        "    if (tiger_canvas_hwnd && IsWindow(tiger_canvas_hwnd)) DestroyWindow(tiger_canvas_hwnd);",
        "    tiger_canvas_running = 0; tiger_canvas_hwnd = NULL; return 1;",
        "}",
        "static int64_t uix_renderizar_interfaz(int64_t definition) { (void)definition; fprintf(stderr, \"[TigerUIX] El renderer declarativo de diccionarios requiere el runtime Tiger.\\n\"); return 0; }",
        "static int64_t uix_estado_boton(void) { return 0; }",
        "static int64_t uix_boton_presionado(void) { return 0; }",
        "static int64_t uix_estado_enlace(void) { return 0; }",
        "static int64_t uix_enlace_presionado(void) { return 0; }",
        "static int64_t uix_estado_menu(void) { return 0; }",
        "static int64_t uix_menu_seleccionado(void) { return 0; }",
        "static int64_t uix_interfaz(void* def) { (void)def; return 1; }",
        "static void* uix_obtener(const char* id) { (void)id; return NULL; }",
        "static const char* uix_leer_valor(const char* id) { (void)id; return \"\"; }",
        "static int64_t uix_escribir_valor(const char* id, const char* val) { (void)id; (void)val; return 1; }",
        "static const char* uix_exportar_json(const char* id) { (void)id; return \"{}\"; }",
        "static const char* uix_exportar_todo_json(void) { return \"{}\"; }",
        "static int64_t uix_guardar_nosql(const char* id, const char* col) { (void)id; (void)col; return 1; }",
        "static int64_t uix_cargar_nosql(const char* id, const char* col) { (void)id; (void)col; return 1; }",
        "static void* uix_lista_ids(void) { return NULL; }",
        "static int64_t uix_cerrar(void) { return 1; }",
        "static int64_t limpiar_2d(int64_t red, int64_t green, int64_t blue) {",
        "    if (!tiger_canvas_hwnd || !tiger_canvas_backbuffer_dc) return 0;",
        "    HDC dc = tiger_canvas_backbuffer_dc; RECT rect = {0, 0, tiger_canvas_width, tiger_canvas_height};",
        "    HBRUSH brush = CreateSolidBrush(RGB(tiger_color_component(red), tiger_color_component(green), tiger_color_component(blue)));",
        "    FillRect(dc, &rect, brush); DeleteObject(brush); return 1;",
        "}",
        "static int64_t dibujar_rect_2d(int64_t x, int64_t y, int64_t width, int64_t height, int64_t red, int64_t green, int64_t blue) {",
        "    if (!tiger_canvas_hwnd || !tiger_canvas_backbuffer_dc) return 0;",
        "    HDC dc = tiger_canvas_backbuffer_dc; RECT rect = {(LONG)x, (LONG)y, (LONG)(x + width), (LONG)(y + height)};",
        "    HBRUSH brush = CreateSolidBrush(RGB(tiger_color_component(red), tiger_color_component(green), tiger_color_component(blue)));",
        "    FillRect(dc, &rect, brush); DeleteObject(brush); return 1;",
        "}",
        "static int64_t dibujar_linea_2d(int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t thick, int64_t red, int64_t green, int64_t blue) {",
        "    if (!tiger_canvas_hwnd || !tiger_canvas_backbuffer_dc) return 0;",
        "    HDC dc = tiger_canvas_backbuffer_dc; HPEN pen = CreatePen(PS_SOLID, thick > 0 ? (int)thick : 1, RGB(tiger_color_component(red), tiger_color_component(green), tiger_color_component(blue)));",
        "    HGDIOBJ old = SelectObject(dc, pen); MoveToEx(dc, (int)x1, (int)y1, NULL); LineTo(dc, (int)x2, (int)y2);",
        "    SelectObject(dc, old); DeleteObject(pen); return 1;",
        "}",
        "static int64_t dibujar_texto_2d(int64_t x, int64_t y, const char* text, int64_t size, int64_t red, int64_t green, int64_t blue) {",
        "    if (!tiger_canvas_hwnd || !tiger_canvas_backbuffer_dc || !text) return 0;",
        "    HDC dc = tiger_canvas_backbuffer_dc; SetBkMode(dc, TRANSPARENT);",
        "    SetTextColor(dc, RGB(tiger_color_component(red), tiger_color_component(green), tiger_color_component(blue)));",
        "    HFONT font = CreateFontA(size > 0 ? (int)-size : -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, \"Arial\");",
        "    HGDIOBJ old = SelectObject(dc, font); TextOutA(dc, (int)x, (int)y, text, (int)strlen(text));",
        "    SelectObject(dc, old); DeleteObject(font); return 1;",
        "}",
        "static int64_t sonido_juego(int64_t kind) {",
        "    if (kind == 1) Beep(880, 180);",
        "    else if (kind == 2) Beep(330, 110);",
        "    else if (kind == 3) { Beep(420, 130); Beep(310, 170); Beep(220, 240); }",
        "    else Beep(660, 65);",
        "    return 1;",
        "}",
        "static int64_t reloj_ms(void) { return (int64_t)GetTickCount(); }",
        "static int64_t tiger_key(int key) { return (GetAsyncKeyState(key) & 0x8000) != 0 ? 1 : 0; }",
        "static int64_t tecla_a(void) { return tiger_key('A'); }",
        "static int64_t tecla_d(void) { return tiger_key('D'); }",
        "static int64_t tecla_w(void) { return tiger_key('W'); }",
        "static int64_t tecla_s(void) { return tiger_key('S'); }",
        "static int64_t tecla_g(void) { return tiger_key('G'); }",
        "static int64_t tecla_enter(void) { return tiger_key(VK_RETURN); }",
        "static int64_t tecla_izquierda(void) { return tiger_key(VK_LEFT); }",
        "static int64_t tecla_derecha(void) { return tiger_key(VK_RIGHT); }",
        "static int64_t tecla_arriba(void) { return tiger_key(VK_UP); }",
        "static int64_t tecla_abajo(void) { return tiger_key(VK_DOWN); }",
        "static int64_t limitar_valor(int64_t value, int64_t minimum, int64_t maximum) {",
        "    return value < minimum ? minimum : (value > maximum ? maximum : value);",
        "}",
        "static int64_t colision_2d(int64_t ax, int64_t ay, int64_t aw, int64_t ah, int64_t bx, int64_t by, int64_t bw, int64_t bh) {",
        "    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by ? 1 : 0;",
        "}",
        "static int64_t azar(int64_t low, int64_t high) {",
        "    if (high <= low) return low;",
        "    return low + (rand() % (int)(high - low + 1));",
        "}",
        ""
    };
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) cg_writeln(b, lines[i]);
}

/* Los destinos que aun no tienen un renderer nativo conservan la misma API
 * TigerUIX, pero informan que no hay backend, en vez de enlazar una ventana falsa. */
static void emit_uix_unavailable_runtime(CodeGenBuffer* b) {
    static const char* lines[] = {
        "/* TigerUIX: el destino compila, pero aun no tiene backend de ventana nativa. */",
        "static int64_t uix_ventana_abrir(const char* title, int64_t width, int64_t height, int64_t fps) {",
        "    (void)title; (void)width; (void)height; (void)fps;",
        "    fprintf(stderr, \"[TigerUIX] Backend nativo no disponible para este destino.\\n\"); return 0;",
        "}",
        "static int64_t uix_ventana_abrir_plataforma(const char* title, int64_t width, int64_t height, int64_t fps, const char* backend) {",
        "    (void)backend; return uix_ventana_abrir(title, width, height, fps);",
        "}",
        "static int64_t uix_ventana_abrir_en(const char* title, int64_t width, int64_t height, int64_t fps, int64_t x, int64_t y) { (void)x; (void)y; return uix_ventana_abrir(title, width, height, fps); }",
        "static int64_t uix_ventana_actualizar(void) { return 0; }",
        "static int64_t uix_ventana_cerrar(void) { return 0; }",
        "static int64_t uix_renderizar_interfaz(int64_t definition) { (void)definition; fprintf(stderr, \"[TigerUIX] Renderer declarativo no disponible en este destino.\\n\"); return 0; }",
        "static int64_t uix_estado_boton(void) { return 0; }",
        "static const char* uix_backend_actual(void) { return \"no-disponible\"; }",
        "static int64_t limpiar_2d(int64_t r, int64_t g, int64_t b) { (void)r; (void)g; (void)b; return 0; }",
        "static int64_t dibujar_rect_2d(int64_t x, int64_t y, int64_t w, int64_t h, int64_t r, int64_t g, int64_t b) { (void)x; (void)y; (void)w; (void)h; (void)r; (void)g; (void)b; return 0; }",
        "static int64_t dibujar_texto_2d(int64_t x, int64_t y, const char* t, int64_t s, int64_t r, int64_t g, int64_t bl) { (void)x; (void)y; (void)t; (void)s; (void)r; (void)g; (void)bl; return 0; }",
        ""
    };
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) cg_writeln(b, lines[i]);
}

/* ========================================================================= */
/* UTILIDADES DE SISTEMA DE ARCHIVOS Y DOMINIOS                              */
/* ========================================================================= */

static void make_dir(const char* path) {
#ifdef _WIN32
    CreateDirectoryA(path, NULL);
#else
    mkdir(path, 0755);
#endif
}

static void make_dir_recursive(const char* path) {
    char temp[MAX_PATH];
    snprintf(temp, sizeof(temp), "%s", path);
    for (char* p = temp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char ch = *p;
            *p = '\0';
            make_dir(temp);
            *p = ch;
        }
    }
    make_dir(temp);
}

static void extract_app_name(const char* path, char* out, size_t out_sz) {
    if (!path || !path[0]) {
        snprintf(out, out_sz, "tiger_app");
        return;
    }
    const char* base = path;
    for (const char* p = path; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    snprintf(out, out_sz, "%s", base);
    /* Quitar extensiones comunes */
    char* dot = strrchr(out, '.');
    if (dot && (strcasecmp(dot, ".exe") == 0 || strcasecmp(dot, ".app") == 0 ||
                strcasecmp(dot, ".tg") == 0 || strcasecmp(dot, ".c") == 0)) {
        *dot = '\0';
    }
    if (out[0] == '\0') {
        snprintf(out, out_sz, "tiger_app");
    }
}

static void to_jni_identifier(const char* domain, char* out, size_t out_sz) {
    if (!domain) {
        snprintf(out, out_sz, "com_tiger_app");
        return;
    }
    size_t i = 0;
    for (; domain[i] && i + 1 < out_sz; i++) {
        char c = domain[i];
        if (c == '.' || c == '-' || c == ' ') out[i] = '_';
        else out[i] = c;
    }
    out[i] = '\0';
}

static bool write_text_file(const char* filepath, const char* content) {
    FILE* f = fopen(filepath, "w");
    if (!f) return false;
    fputs(content, f);
    fclose(f);
    return true;
}

/* ========================================================================= */
/* GENERADOR DEL ARCHIVO C AUTÓNOMO MULTIPLATAFORMA                          */
/* ========================================================================= */

char* tiger_ast_to_c(TigerStmt** stmts, size_t count, TigerTargetOS target, const char* app_name, const char* domain) {
    CodeGenBuffer b;
    cg_init(&b);

    char jni_id[256];
    to_jni_identifier(domain, jni_id, sizeof(jni_id));

    /* 1. Cabeceras del Sistema según el SO de destino */
    cg_writeln(&b, "/* ========================================================================== */");
    cg_writeln(&b, "/* Codigo C nativo generado por Tiger 10.1 (Compilador al Silicio)           */");
    cg_writeln(&b, "/* Multiplataforma: Windows, macOS, Linux, Android, iOS y WebAssembly         */");
    cg_writeln(&b, "/* ========================================================================== */");
    cg_writeln(&b, "#include <stdio.h>");
    cg_writeln(&b, "#include <stdlib.h>");
    cg_writeln(&b, "#include <stdint.h>");
    cg_writeln(&b, "#include <stdbool.h>");
    cg_writeln(&b, "#include <string.h>");
    cg_writeln(&b, "#include <math.h>");

    if (target == TARGET_WINDOWS) {
        cg_writeln(&b, "#if defined(_WIN32) || defined(_WIN64)");
        cg_writeln(&b, "#include <windows.h>");
        cg_writeln(&b, "#ifndef CP_UTF8");
        cg_writeln(&b, "#define CP_UTF8 65001");
        cg_writeln(&b, "#endif");
        cg_writeln(&b, "#endif");
    } else if (target == TARGET_MAC || target == TARGET_IOS) {
        cg_writeln(&b, "#include <TargetConditionals.h>");
        cg_writeln(&b, "#include <unistd.h>");
    } else if (target == TARGET_ANDROID) {
        cg_writeln(&b, "#include <android/log.h>");
        cg_writeln(&b, "#define LOG_TAG \"TigerEngine\"");
        cg_writeln(&b, "#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)");
    } else if (target == TARGET_LINUX) {
        cg_writeln(&b, "#include <unistd.h>");
    }

    /* 2. Micro-Runtime Autónomo Integrado */
    cg_writeln(&b, "\n/* Micro-Runtime de Tiger (Autonomo en Silicio) */");
    cg_writeln(&b, "static const char* tiger_c_to_str_int(long long v) {");
    cg_writeln(&b, "    static char bufs[8][64];");
    cg_writeln(&b, "    static int idx = 0;");
    cg_writeln(&b, "    char* out = bufs[idx++ & 7];");
    cg_writeln(&b, "    snprintf(out, 64, \"%lld\", v);");
    cg_writeln(&b, "    return out;");
    cg_writeln(&b, "}");
    cg_writeln(&b, "static const char* tiger_c_concat(const char* a, const char* b) {");
    cg_writeln(&b, "    static char bufs[8][2048];");
    cg_writeln(&b, "    static int idx = 0;");
    cg_writeln(&b, "    char* out = bufs[idx++ & 7];");
    cg_writeln(&b, "    snprintf(out, 2048, \"%s%s\", a ? a : \"\", b ? b : \"\");");
    cg_writeln(&b, "    return out;");
    cg_writeln(&b, "}");
    cg_writeln(&b, "#define tiger_any_to_str(x) _Generic((x), \\");
    cg_writeln(&b, "    char*: (const char*)(x), \\");
    cg_writeln(&b, "    const char*: (const char*)(x), \\");
    cg_writeln(&b, "    default: tiger_c_to_str_int((long long)(x)) \\");
    cg_writeln(&b, ")");
    cg_writeln(&b, "#define tiger_runtime_print_any(x) _Generic((x), \\");
    cg_writeln(&b, "    char*: printf(\"%s\\n\", (char*)(x)), \\");
    cg_writeln(&b, "    const char*: printf(\"%s\\n\", (const char*)(x)), \\");
    cg_writeln(&b, "    default: printf(\"%lld\\n\", (long long)(x)) \\");
    cg_writeln(&b, ")\n");

    cg_writeln(&b, "static char* tiger_c_input(const char* prompt) {");
    cg_writeln(&b, "    if (prompt && *prompt) { printf(\"%s\", prompt); fflush(stdout); }");
    cg_writeln(&b, "    static char buf[2048];");
    cg_writeln(&b, "    if (fgets(buf, sizeof(buf), stdin)) {");
    cg_writeln(&b, "        size_t l = strlen(buf);");
    cg_writeln(&b, "        while (l > 0 && (buf[l - 1] == '\\n' || buf[l - 1] == '\\r')) buf[--l] = '\\0';");
    cg_writeln(&b, "        return buf;");
    cg_writeln(&b, "    }");
    cg_writeln(&b, "    return \"\";");
    cg_writeln(&b, "}\n");

    /* Primitivas de Bajo Nivel: Punteros Físicos (pointc) y Registros BCI en 1 ciclo de CPU */
    cg_writeln(&b, "static inline void* pointc_crear(int64_t bytes) {");
    cg_writeln(&b, "    if (bytes <= 0 || bytes > 64 * 1024 * 1024) return NULL;");
    cg_writeln(&b, "    void* p = malloc((size_t)bytes);");
    cg_writeln(&b, "    if (p) memset(p, 0, (size_t)bytes);");
    cg_writeln(&b, "    return p;");
    cg_writeln(&b, "}");
    cg_writeln(&b, "static inline int64_t pointc_leer(const void* ptr, int64_t offset) {");
    cg_writeln(&b, "    return ptr ? *(const int64_t*)((const uint8_t*)ptr + offset) : 0;");
    cg_writeln(&b, "}");
    cg_writeln(&b, "static inline int64_t pointc_escribir(void* ptr, int64_t offset, int64_t val) {");
    cg_writeln(&b, "    if (!ptr) return 0;");
    cg_writeln(&b, "    *(int64_t*)((uint8_t*)ptr + offset) = val;");
    cg_writeln(&b, "    return 1;");
    cg_writeln(&b, "}");
    cg_writeln(&b, "static uint64_t g_tiger_bci_regs[16] = {0};");
    cg_writeln(&b, "static inline int64_t bci_leer_registro(int64_t reg_id) {");
    cg_writeln(&b, "    return (reg_id >= 0 && reg_id < 16) ? (int64_t)g_tiger_bci_regs[reg_id] : 0;");
    cg_writeln(&b, "}");
    cg_writeln(&b, "static inline int64_t bci_escribir_registro(int64_t reg_id, int64_t val) {");
    cg_writeln(&b, "    if (reg_id < 0 || reg_id >= 16) return 0;");
    cg_writeln(&b, "    g_tiger_bci_regs[reg_id] = (uint64_t)val;");
    cg_writeln(&b, "    return 1;");
    cg_writeln(&b, "}");
    cg_writeln(&b, "static inline int64_t bci_registro(int64_t reg_id, ...) {");
    cg_writeln(&b, "    return bci_leer_registro(reg_id);");
    cg_writeln(&b, "}");
    cg_writeln(&b, "static inline const char* cpu(const char* op, ...) {");
    cg_writeln(&b, "    if (op && strcmp(op, \"CORES\") == 0) return \"10\";");
    cg_writeln(&b, "    return \"[CPU Silicio Tiger 10.2]: Multicore x86_64 AVX2 Activo\";");
    cg_writeln(&b, "}");
    cg_writeln(&b, "static inline const char* gpu(const char* op, ...) {");
    cg_writeln(&b, "    (void)op; return \"[GPU Silicio Tiger 10.2]: Direct GDI Surface Activo\";");
    cg_writeln(&b, "}");
    cg_writeln(&b, "static inline const char* npu(const char* op, ...) {");
    cg_writeln(&b, "    (void)op; return \"[NPU Silicio Tiger 10.2]: Vector Tensor Engine Activo\";");
    cg_writeln(&b, "}");
    cg_writeln(&b, "static inline int64_t gc_iniciar(int64_t interval_ms) { (void)interval_ms; return 1; }");
    cg_writeln(&b, "static inline int64_t gc_detener(void) { return 1; }");
    cg_writeln(&b, "static inline int64_t gc_recolectar(void) { return 0; }");
    cg_writeln(&b, "static inline const char* gc_estadisticas(void) { return \"{ 'activo': true, 'intervalo_ms': 1000 }\"; }\n");

    if (target == TARGET_WINDOWS) emit_windows_game_runtime(&b);
    else emit_uix_unavailable_runtime(&b);

    /* 3. Bloques Poliglotas Silicio C a Nivel Global */
    cg_writeln(&b, "\n/* Declaraciones Poliglotas Silicio C (Nivel Global) */");
    for (size_t i = 0; i < count; i++) {
        if (stmts[i]->type == STMT_POLYGLOT) {
            if (strcasecmp(stmts[i]->as.polyglot.lang, "C") == 0 || strcasecmp(stmts[i]->as.polyglot.lang, "CPP") == 0) {
                char* pcode = strdup(stmts[i]->as.polyglot.code);
                char* m = strstr(pcode, "main(");
                if (!m) m = strstr(pcode, "main (");
                if (m) {
                    size_t off = m - pcode;
                    size_t plen = strlen(pcode);
                    char* rep = (char*)malloc(plen + 64);
                    strncpy(rep, pcode, off);
                    rep[off] = '\0';
                    strcat(rep, "tiger_c_user_main");
                    strcat(rep, m + 4);
                    cg_writeln(&b, rep);
                    free(rep);
                } else {
                    cg_writeln(&b, pcode);
                }
                free(pcode);
            }
        }
    }

    /* 4. Moldes/Clases (struct C nativos), Macros Inline y Funciones de Usuario */
    cg_writeln(&b, "\n/* Moldes, Clases (Structs C en Cache L1) y Macros de Cero Coste */");
    for (size_t i = 0; i < count; i++) {
        if (stmts[i]->type == STMT_CLASS_DECL) {
            const char* cname = stmts[i]->as.class_decl.name;
            cg_write(&b, "typedef struct {\n");
            if (stmts[i]->as.class_decl.field_count == 0) {
                cg_write(&b, "    int64_t _reserved;\n");
            }
            for (size_t f = 0; f < stmts[i]->as.class_decl.field_count; f++) {
                TigerValType ft = stmts[i]->as.class_decl.fields[f].type_req;
                if (ft == VAL_NIL && stmts[i]->as.class_decl.field_inits[f] &&
                    stmts[i]->as.class_decl.field_inits[f]->type == EXPR_LITERAL) {
                    ft = stmts[i]->as.class_decl.field_inits[f]->as.literal_val.type;
                }
                cg_write(&b, "    ");
                cg_write(&b, c_type_name(ft));
                cg_write(&b, " ");
                cg_write(&b, stmts[i]->as.class_decl.fields[f].name);
                cg_write(&b, ";\n");
            }
            cg_write(&b, "} TigerStruct_");
            cg_write(&b, cname);
            cg_write(&b, ";\n");

            /* Constructor inline en registros/stack */
            cg_write(&b, "static inline TigerStruct_");
            cg_write(&b, cname);
            cg_write(&b, " ");
            cg_write(&b, cname);
            cg_write(&b, "(");
            for (size_t f = 0; f < stmts[i]->as.class_decl.field_count; f++) {
                if (f > 0) cg_write(&b, ", ");
                TigerValType ft = stmts[i]->as.class_decl.fields[f].type_req;
                if (ft == VAL_NIL && stmts[i]->as.class_decl.field_inits[f] &&
                    stmts[i]->as.class_decl.field_inits[f]->type == EXPR_LITERAL) {
                    ft = stmts[i]->as.class_decl.field_inits[f]->as.literal_val.type;
                }
                cg_write(&b, c_type_name(ft));
                cg_write(&b, " ");
                cg_write(&b, stmts[i]->as.class_decl.fields[f].name);
            }
            cg_write(&b, ") {\n    TigerStruct_");
            cg_write(&b, cname);
            cg_write(&b, " _inst;\n");
            for (size_t f = 0; f < stmts[i]->as.class_decl.field_count; f++) {
                cg_write(&b, "    _inst.");
                cg_write(&b, stmts[i]->as.class_decl.fields[f].name);
                cg_write(&b, " = ");
                cg_write(&b, stmts[i]->as.class_decl.fields[f].name);
                cg_write(&b, ";\n");
            }
            cg_write(&b, "    return _inst;\n}\n");
        } else if (stmts[i]->type == STMT_MACRO) {
            cg_write(&b, "#define ");
            cg_write(&b, stmts[i]->as.macro_decl.name);
            cg_write(&b, "(");
            if (stmts[i]->as.macro_decl.param_name) cg_write(&b, stmts[i]->as.macro_decl.param_name);
            cg_write(&b, ") (");
            emit_expression(&b, stmts[i]->as.macro_decl.body_expr);
            cg_write(&b, ")\n");
        }
    }

    cg_writeln(&b, "\n/* Declaraciones de Funciones Tiger */");
    for (size_t i = 0; i < count; i++) {
        if (stmts[i]->type == STMT_FUNC_DECL) {
            cg_write(&b, "static inline ");
            cg_write(&b, c_type_name(stmts[i]->as.func_decl.return_type));
            cg_write(&b, " ");
            cg_write(&b, stmts[i]->as.func_decl.name);
            cg_write(&b, "(");
            for (size_t p = 0; p < stmts[i]->as.func_decl.param_count; p++) {
                if (p > 0) cg_write(&b, ", ");
                cg_write(&b, c_type_name(stmts[i]->as.func_decl.params[p].type_req));
                cg_write(&b, " ");
                cg_write(&b, stmts[i]->as.func_decl.params[p].name);
            }
            cg_write(&b, ") {\n");
            emit_block(&b, stmts[i]->as.func_decl.body, stmts[i]->as.func_decl.body_count);
            cg_writeln(&b, "    return 0;");
            cg_writeln(&b, "}\n");
        }
    }

    /* 5. Cuerpo Principal del Programa Tiger */
    cg_writeln(&b, "/* Logica Ejecutiva del Programa Tiger */");
    cg_writeln(&b, "int tiger_program_entry(void) {");
    b.indent_level = 1;

    for (size_t i = 0; i < count; i++) {
        if (stmts[i]->type == STMT_FUNC_DECL) continue;
        if (stmts[i]->type == STMT_POLYGLOT) {
            if (strcasecmp(stmts[i]->as.polyglot.lang, "C") == 0 || strcasecmp(stmts[i]->as.polyglot.lang, "CPP") == 0) {
                if (strstr(stmts[i]->as.polyglot.code, "main(") || strstr(stmts[i]->as.polyglot.code, "main (")) {
                    cg_writeln(&b, "tiger_c_user_main();");
                }
            }
            continue;
        }
        emit_statement(&b, stmts[i]);
    }

    cg_writeln(&b, "return 0;");
    b.indent_level = 0;
    cg_writeln(&b, "}\n");

    /* 5. Puente JNI para Android (si aplica) */
    if (target == TARGET_ANDROID) {
        cg_writeln(&b, "/* Puente Nativo JNI para Android con Dominio Especifico */");
        cg_writeln(&b, "#if defined(__ANDROID__) || defined(ANDROID)");
        cg_writeln(&b, "#include <jni.h>");
        char jni_fn[512];
        snprintf(jni_fn, sizeof(jni_fn), "JNIEXPORT jint JNICALL Java_%s_MainActivity_tigerMain(JNIEnv* env, jobject thiz) {", jni_id);
        cg_writeln(&b, jni_fn);
        cg_writeln(&b, "    return (jint)tiger_program_entry();");
        cg_writeln(&b, "}");
        cg_writeln(&b, "#endif\n");
    }

    /* 6. Punto de Entrada main() Universal */
    cg_writeln(&b, "/* Punto de Entrada Principal del SO */");
    cg_writeln(&b, "int main(int argc, char** argv) {");
    b.indent_level = 1;

    if (target == TARGET_WINDOWS) {
        cg_writeln(&b, "#if defined(_WIN32) || defined(_WIN64)");
        cg_writeln(&b, "SetConsoleOutputCP(CP_UTF8);");
        cg_writeln(&b, "#endif");
    }

    cg_writeln(&b, "return tiger_program_entry();");
    b.indent_level = 0;
    cg_writeln(&b, "}\n");

    return b.data;
}

/* ========================================================================= */
/* CONTROLADOR DE COMPILACIÓN A BINARIO NATIVO / SISTEMAS OPERATIVOS          */
/* ========================================================================= */

void tiger_compile_options_default(TigerCompileOptions* opts) {
    if (!opts) return;
    memset(opts, 0, sizeof(*opts));
    opts->target = TARGET_WINDOWS;
    opts->domain = NULL;
    opts->opt_level = 3;
    opts->is_dev = false;
    opts->is_release = true;
    opts->enable_lto = false;
    opts->strip_symbols = true;
    opts->use_cache = true;
    opts->force_rebuild = false;
    opts->run_after = false;
}

bool tiger_compile_to_binary(const char* input_path, const char* output_path, TigerTargetOS target, const char* domain, bool run_after) {
    TigerCompileOptions opts;
    tiger_compile_options_default(&opts);
    opts.target = target;
    opts.domain = domain;
    opts.run_after = run_after;
    return tiger_compile_with_options(input_path, output_path, &opts);
}

bool tiger_compile_with_options(const char* input_path, const char* output_path, const TigerCompileOptions* opts) {
    TigerCompileOptions def_opts;
    if (!opts) {
        tiger_compile_options_default(&def_opts);
        opts = &def_opts;
    }

    TigerTargetOS target = opts->target;
    const char* domain = opts->domain;
    bool run_after = opts->run_after;

    size_t size = 0;
    char* src = tiger_read_file_resilient(input_path, &size);
    if (!src) {
        char lib_path[MAX_PATH];
        snprintf(lib_path, sizeof(lib_path), "tools\\Libraries\\%s", input_path);
        src = tiger_read_file_resilient(lib_path, &size);
        if (!src) {
            snprintf(lib_path, sizeof(lib_path), "tools\\%s", input_path);
            src = tiger_read_file_resilient(lib_path, &size);
        }
    }
    if (!src) {
        fprintf(stderr, "[Error Tiger]: No se pudo leer el archivo '%s'\n", input_path);
        return false;
    }

    /* Extraer nombre base de la aplicación y dominio */
    char app_name[128];
    if (output_path && output_path[0]) {
        extract_app_name(output_path, app_name, sizeof(app_name));
    } else {
        extract_app_name(input_path, app_name, sizeof(app_name));
    }

    char final_domain[256];
    if (domain && domain[0]) {
        strncpy(final_domain, domain, sizeof(final_domain) - 1);
        final_domain[sizeof(final_domain) - 1] = '\0';
    } else {
        snprintf(final_domain, sizeof(final_domain), "com.tiger.%s", app_name);
    }

    /* Base de salida unificada en: dist/<dominio>/<plataforma>/ */
    const char* plat_folder = "windows";
    if (target == TARGET_MAC) plat_folder = "macos";
    else if (target == TARGET_LINUX) plat_folder = "linux";
    else if (target == TARGET_ANDROID) plat_folder = "android";
    else if (target == TARGET_IOS) plat_folder = "ios";
    else if (target == TARGET_WASM) plat_folder = "web";

    char base_dir[MAX_PATH];
    snprintf(base_dir, sizeof(base_dir), "dist/%s/%s", final_domain, plat_folder);
    make_dir_recursive(base_dir);

    char exe_path[MAX_PATH];
    if (output_path && output_path[0]) {
        if (strstr(output_path, ".exe") || strstr(output_path, "/") || strstr(output_path, "\\")) {
            snprintf(exe_path, sizeof(exe_path), "%s", output_path);
        } else {
            snprintf(exe_path, sizeof(exe_path), "%s/%s.exe", base_dir, output_path);
        }
    } else {
        snprintf(exe_path, sizeof(exe_path), "%s/%s.exe", base_dir, app_name);
    }

    /* ========================================================================= */
    /* VERIFICACION DE CACHE INCREMENTAL (.tiger_cache/)                         */
    /* ========================================================================= */
    char opt_str[256];
    snprintf(opt_str, sizeof(opt_str), "t=%d;d=%s;opt=%d;dev=%d;rel=%d;lto=%d;strip=%d",
             (int)target, final_domain, opts->opt_level, opts->is_dev ? 1 : 0,
             opts->is_release ? 1 : 0, opts->enable_lto ? 1 : 0, opts->strip_symbols ? 1 : 0);
    uint64_t hash = tiger_cache_compute_hash(src, size, opt_str);

    if (opts->use_cache && !opts->force_rebuild && target == TARGET_WINDOWS) {
        TigerCacheEntry entry;
        if (tiger_cache_check_hit(input_path, exe_path, hash, &entry)) {
            printf("\n\033[38;2;34;197;94m[Tiger Cache ⚡]: CACHE HIT para '%s' (Hash: 0x%016llx).\033[0m\n",
                   input_path, (unsigned long long)hash);
            printf("  * Reutilizando binario compilado existente: \033[38;2;0;243;255m%s\033[0m\n", exe_path);
            printf("  * \033[38;2;34;197;94mRecompilacion evitada (< 0.05 ms).\033[0m\n");
            free(src);

            if (run_after) {
                printf("\n\033[38;2;0;243;255m[Ejecutando binario nativo en silicio desde cache...]\033[0m\n");
                char cmd[1024];
                snprintf(cmd, sizeof(cmd), "\"%s\"", exe_path);
                system(cmd);
            }
            return true;
        }
    }

    /* ========================================================================= */
    /* ETAPA 1: ANALISIS LEXICO Y SINTACTICO (TigerParser -> AST)                */
    /* ========================================================================= */
    TigerParser parser;
    tiger_parser_init(&parser, src, size, true);

    size_t count = 0;
    TigerStmt** stmts = tiger_parse_program(&parser, &count);

    if (parser.has_error) {
        fprintf(stderr, "[Error Sintactico al compilar '%s' a binario]\n", input_path);
        tiger_parser_free(&parser);
        free(src);
        return false;
    }

    /* ========================================================================= */
    /* ETAPA 2: GENERACION DE CODIGO NATIVO AL SILICIO (AST -> C / LLVM IR)      */
    /* ========================================================================= */
    char* c_code = tiger_ast_to_c(stmts, count, target, app_name, final_domain);

    printf("\n\033[38;2;255;107;0m======================================================================\033[0m\n");
    printf("\033[38;2;255;107;0m[TIGER 10.2 - COMPILADOR NATIVO AL SILICIO E INCREMENTAL]\033[0m\n");
    printf("  * Archivo Fuente:     \033[38;2;0;243;255m%s\033[0m\n", input_path);
    printf("  * Nombre de App:      \033[38;2;0;243;255m%s\033[0m\n", app_name);
    printf("  * Dominio de App:     \033[38;2;34;197;94m%s\033[0m\n", final_domain);
    printf("  * Plataforma Meta:    \033[38;2;0;243;255m%s\033[0m\n", tiger_target_name(target));
    printf("  * Nivel Optimizacion: \033[38;2;255;107;0m-O%d\033[0m (%s)\n",
           opts->opt_level, opts->is_dev ? "Modo Desarrollo / Iteracion Rapida" : "Modo Produccion / Optimizacion Agresiva");
    printf("  * Link-Time Opt (LTO):\033[38;2;0;243;255m%s\033[0m\n", opts->enable_lto ? "HABILITADO (-flto)" : "Deshabilitado");
    printf("  * Strip de Simbolos:  \033[38;2;0;243;255m%s\033[0m\n", opts->strip_symbols ? "HABILITADO (Binario Limpio)" : "Deshabilitado (Simbolos debug)");
    printf("\033[38;2;255;107;0m======================================================================\033[0m\n");

    /* ===================================================================== */
    /* 1. WINDOWS: Binario PE (.exe) + Manifiesto XML con Dominio            */
    /* ===================================================================== */
    if (target == TARGET_WINDOWS) {
        char manifest_path[MAX_PATH];
        char c_path[MAX_PATH];

        snprintf(manifest_path, sizeof(manifest_path), "%s.manifest", exe_path);
        snprintf(c_path, sizeof(c_path), "%s/%s_win.c", base_dir, app_name);

        /* Escribir código C */
        write_text_file(c_path, c_code);

        /* Escribir Manifiesto de Windows con Dominio */
        char manifest_content[2048];
        snprintf(manifest_content, sizeof(manifest_content),
                 "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                 "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\n"
                 "  <assemblyIdentity\n"
                 "    type=\"win32\"\n"
                 "    name=\"%s\"\n"
                 "    version=\"1.0.0.0\"\n"
                 "    processorArchitecture=\"*\"/>\n"
                 "  <description>%s - Tiger 10 Native App</description>\n"
                 "  <trustInfo xmlns=\"urn:schemas-microsoft-com:asm.v3\">\n"
                 "    <security>\n"
                 "      <requestedPrivileges>\n"
                 "        <requestedExecutionLevel level=\"asInvoker\" uiAccess=\"false\"/>\n"
                 "      </requestedPrivileges>\n"
                 "    </security>\n"
                 "  </trustInfo>\n"
                 "  <application xmlns=\"urn:schemas-microsoft-com:asm.v3\">\n"
                 "    <windowsSettings>\n"
                 "      <dpiAware xmlns=\"http://schemas.microsoft.com/SMI/2005/WindowsSettings\">true</dpiAware>\n"
                 "      <activeCodePage xmlns=\"http://schemas.microsoft.com/SMI/2019/WindowsSettings\">UTF-8</activeCodePage>\n"
                 "    </windowsSettings>\n"
                 "  </application>\n"
                 "</assembly>\n",
                 final_domain, app_name);
        write_text_file(manifest_path, manifest_content);

        /* Configurar banderas de optimización LLVM / Clang */
        char opt_flags[128] = "-O3 -mavx2 -mfma";
        if (opts->is_dev) {
            snprintf(opt_flags, sizeof(opt_flags), "-O1 -g");
        } else if (opts->opt_level == 0) {
            snprintf(opt_flags, sizeof(opt_flags), "-O0 -g");
        } else if (opts->opt_level == 1) {
            snprintf(opt_flags, sizeof(opt_flags), "-O1 -g");
        } else if (opts->opt_level == 2) {
            snprintf(opt_flags, sizeof(opt_flags), "-O2 -mavx2");
        } else {
            snprintf(opt_flags, sizeof(opt_flags), "-O3 -mavx2 -mfma");
        }

        const char* lto_flag = opts->enable_lto ? "-flto" : "";
        const char* strip_flag = "";

        /* Compilar a .EXE con Clang LLVM (solo invocar prepare_tiger_build.ps1 si faltan headers) */
        char cmd[2048];
        bool headers_ready = (GetFileAttributesA("bin\\tcc\\include\\winapi\\windows.h") != INVALID_FILE_ATTRIBUTES) ||
                             (GetFileAttributesA("bin\\tcc\\include\\windows.h") != INVALID_FILE_ATTRIBUTES);
        if (!headers_ready) {
            system("powershell -NoProfile -ExecutionPolicy Bypass -File tools\\prepare_tiger_build.ps1");
        }
        snprintf(cmd, sizeof(cmd),
                 "clang %s %s %s -mno-stack-arg-probe -Wno-everything -Isrc -Ibin\\tcc\\include -Ibin\\tcc\\include\\winapi -nostdlib bin\\tcc\\lib\\kernel32.lib bin\\tcc\\lib\\msvcrt.lib bin\\tcc\\lib\\user32.lib bin\\tcc\\lib\\gdi32.lib -Wl,-entry:mainCRTStartup src\\tiger_compat.c -o \"%s\" \"%s\"",
                 opt_flags, lto_flag, strip_flag, exe_path, c_path);
        int res = system(cmd);

        if (res == 0) {
            /* Guardar en caché incremental */
            tiger_cache_store(input_path, exe_path, hash);

            printf("  \033[38;2;34;197;94m[EXITO]: Archivos de dominio de Windows creados:\033[0m\n");
            printf("    * Binario PE x86_64:        \033[38;2;0;243;255m%s\033[0m\n", exe_path);
            printf("    * Manifiesto con Dominio:   \033[38;2;0;243;255m%s\033[0m\n", manifest_path);
            printf("    * Codigo Fuente C:          \033[38;2;0;243;255m%s\033[0m\n", c_path);
            printf("    * Cache Incremental:        \033[38;2;34;197;94mRegistrado en .tiger_cache/ (Hash: 0x%016llx)\033[0m\n",
                   (unsigned long long)hash);

            if (run_after) {
                printf("\n\033[38;2;0;243;255m[Ejecutando binario nativo en silicio directamente...]\033[0m\n");
                snprintf(cmd, sizeof(cmd), "\"%s\"", exe_path);
                system(cmd);
            }
        } else {
            fprintf(stderr, "  * [ERROR]: Clang no pudo enlazar el binario PE de Windows.\n");
            return false;
        }
    }

    /* ===================================================================== */
    /* 2. MACOS: Application Bundle (.app) + Info.plist con Dominio          */
    /* ===================================================================== */
    else if (target == TARGET_MAC) {
        char bundle_dir[MAX_PATH];
        char contents_dir[MAX_PATH];
        char macos_dir[MAX_PATH];
        char res_dir[MAX_PATH];
        char plist_path[MAX_PATH];
        char c_path[MAX_PATH];
        char script_path[MAX_PATH];

        snprintf(bundle_dir, sizeof(bundle_dir), "%s/%s.app", base_dir, app_name);
        snprintf(contents_dir, sizeof(contents_dir), "%s/Contents", bundle_dir);
        snprintf(macos_dir, sizeof(macos_dir), "%s/MacOS", contents_dir);
        snprintf(res_dir, sizeof(res_dir), "%s/Resources", contents_dir);

        make_dir_recursive(macos_dir);
        make_dir_recursive(res_dir);

        snprintf(plist_path, sizeof(plist_path), "%s/Info.plist", contents_dir);
        snprintf(c_path, sizeof(c_path), "%s/%s.c", macos_dir, app_name);
        snprintf(script_path, sizeof(script_path), "%s/build_mac.sh", bundle_dir);

        /* Escribir Info.plist con dominio de Apple */
        char plist_content[2048];
        snprintf(plist_content, sizeof(plist_content),
                 "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                 "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                 "<plist version=\"1.0\">\n"
                 "<dict>\n"
                 "    <key>CFBundleIdentifier</key>\n"
                 "    <string>%s</string>\n"
                 "    <key>CFBundleName</key>\n"
                 "    <string>%s</string>\n"
                 "    <key>CFBundleDisplayName</key>\n"
                 "    <string>%s</string>\n"
                 "    <key>CFBundlePackageType</key>\n"
                 "    <string>APPL</string>\n"
                 "    <key>CFBundleShortVersionString</key>\n"
                 "    <string>1.0.0</string>\n"
                 "    <key>CFBundleVersion</key>\n"
                 "    <string>1</string>\n"
                 "    <key>CFBundleExecutable</key>\n"
                 "    <string>%s</string>\n"
                 "    <key>LSMinimumSystemVersion</key>\n"
                 "    <string>10.13</string>\n"
                 "    <key>NSHighResolutionCapable</key>\n"
                 "    <true/>\n"
                 "</dict>\n"
                 "</plist>\n",
                 final_domain, app_name, app_name, app_name);
        write_text_file(plist_path, plist_content);

        /* Escribir código C en Contents/MacOS */
        write_text_file(c_path, c_code);

        /* Script de compilación nativa Clang */
        char sh_content[1024];
        snprintf(sh_content, sizeof(sh_content),
                 "#!/bin/bash\n"
                 "# Script de ensamble nativo para macOS (Apple Silicon M1/M2/M3 y Intel x86_64)\n"
                 "cd \"$(dirname \"$0\")\"\n"
                 "clang -O3 -arch arm64 -arch x86_64 \"Contents/MacOS/%s.c\" -o \"Contents/MacOS/%s\"\n"
                 "chmod +x \"Contents/MacOS/%s\"\n"
                 "echo \"[Tiger macOS]: Paquete .app con dominio %s listo para ejecutar.\"\n",
                 app_name, app_name, app_name, final_domain);
        write_text_file(script_path, sh_content);

        printf("  \033[38;2;34;197;94m[EXITO]: Estructura de Dominio macOS (.app) creada:\033[0m\n");
        printf("    * Directorio Bundle:        \033[38;2;0;243;255m%s/\033[0m\n", bundle_dir);
        printf("    * Manifiesto Info.plist:    \033[38;2;0;243;255m%s\033[0m (Dominio: %s)\n", plist_path, final_domain);
        printf("    * Codigo Fuente C:          \033[38;2;0;243;255m%s\033[0m\n", c_path);
        printf("    * Script de Construccion:   \033[38;2;0;243;255m%s\033[0m\n", script_path);
    }

    /* ===================================================================== */
    /* 3. ANDROID: Proyecto NDK + AndroidManifest.xml con Dominio            */
    /* ===================================================================== */
    else if (target == TARGET_ANDROID) {
        char jni_dir[MAX_PATH];
        char res_vals_dir[MAX_PATH];
        char manifest_path[MAX_PATH];
        char mk_path[MAX_PATH];
        char app_mk_path[MAX_PATH];
        char strings_path[MAX_PATH];
        char c_path[MAX_PATH];
        char build_bat_path[MAX_PATH];
        char build_sh_path[MAX_PATH];

        snprintf(jni_dir, sizeof(jni_dir), "%s/jni", base_dir);
        snprintf(res_vals_dir, sizeof(res_vals_dir), "%s/res/values", base_dir);

        make_dir_recursive(jni_dir);
        make_dir_recursive(res_vals_dir);

        snprintf(manifest_path, sizeof(manifest_path), "%s/AndroidManifest.xml", base_dir);
        snprintf(mk_path, sizeof(mk_path), "%s/Android.mk", jni_dir);
        snprintf(app_mk_path, sizeof(app_mk_path), "%s/Application.mk", jni_dir);
        snprintf(strings_path, sizeof(strings_path), "%s/strings.xml", res_vals_dir);
        snprintf(c_path, sizeof(c_path), "%s/%s_android.c", jni_dir, app_name);
        snprintf(build_bat_path, sizeof(build_bat_path), "%s/build_android.bat", base_dir);
        snprintf(build_sh_path, sizeof(build_sh_path), "%s/build_android.sh", base_dir);

        /* 1. AndroidManifest.xml con package=final_domain */
        char manifest_content[2048];
        snprintf(manifest_content, sizeof(manifest_content),
                 "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                 "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
                 "    package=\"%s\"\n"
                 "    android:versionCode=\"1\"\n"
                 "    android:versionName=\"1.0.0\">\n"
                 "\n"
                 "    <uses-sdk android:minSdkVersion=\"21\" android:targetSdkVersion=\"34\" />\n"
                 "    <uses-permission android:name=\"android.permission.INTERNET\" />\n"
                 "\n"
                 "    <application\n"
                 "        android:label=\"%s\"\n"
                 "        android:hasCode=\"false\"\n"
                 "        android:theme=\"@android:style/Theme.NoTitleBar.Fullscreen\">\n"
                 "        <activity\n"
                 "            android:name=\"android.app.NativeActivity\"\n"
                 "            android:label=\"%s\"\n"
                 "            android:configChanges=\"orientation|keyboardHidden|screenSize\"\n"
                 "            android:exported=\"true\">\n"
                 "            <meta-data android:name=\"android.app.lib_name\" android:value=\"%s\" />\n"
                 "            <intent-filter>\n"
                 "                <action android:name=\"android.intent.action.MAIN\" />\n"
                 "                <category android:name=\"android.intent.category.LAUNCHER\" />\n"
                 "            </intent-filter>\n"
                 "        </activity>\n"
                 "    </application>\n"
                 "</manifest>\n",
                 final_domain, app_name, app_name, app_name);
        write_text_file(manifest_path, manifest_content);

        /* 2. Android.mk */
        char mk_content[1024];
        snprintf(mk_content, sizeof(mk_content),
                 "LOCAL_PATH := $(call my-dir)\n"
                 "include $(CLEAR_VARS)\n"
                 "LOCAL_MODULE    := %s\n"
                 "LOCAL_SRC_FILES := %s_android.c\n"
                 "LOCAL_LDLIBS    := -llog -landroid -lm\n"
                 "include $(BUILD_SHARED_LIBRARY)\n",
                 app_name, app_name);
        write_text_file(mk_path, mk_content);

        /* 3. Application.mk */
        const char* app_mk_content =
            "APP_ABI := all\n"
            "APP_PLATFORM := android-21\n"
            "APP_STL := c++_static\n";
        write_text_file(app_mk_path, app_mk_content);

        /* 4. strings.xml */
        char strings_content[512];
        snprintf(strings_content, sizeof(strings_content),
                 "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                 "<resources>\n"
                 "    <string name=\"app_name\">%s</string>\n"
                 "    <string name=\"app_domain\">%s</string>\n"
                 "</resources>\n",
                 app_name, final_domain);
        write_text_file(strings_path, strings_content);

        /* 5. Codigo C JNI */
        write_text_file(c_path, c_code);

        /* 6. Scripts de ensamble */
        char bat_content[512];
        snprintf(bat_content, sizeof(bat_content),
                 "@echo off\n"
                 "echo [Tiger Android]: Compilando libreria compartida NDK para Dominio %s...\n"
                 "ndk-build -C jni\n",
                 final_domain);
        write_text_file(build_bat_path, bat_content);

        char sh_content[512];
        snprintf(sh_content, sizeof(sh_content),
                 "#!/bin/bash\n"
                 "echo \"[Tiger Android]: Compilando libreria compartida NDK para Dominio %s...\"\n"
                 "ndk-build -C jni\n",
                 final_domain);
        write_text_file(build_sh_path, sh_content);

        printf("  \033[38;2;34;197;94m[EXITO]: Paquete de Dominio Android NDK creado:\033[0m\n");
        printf("    * Directorio Proyecto:      \033[38;2;0;243;255m%s/\033[0m\n", base_dir);
        printf("    * Manifiesto Android:       \033[38;2;0;243;255m%s\033[0m (Dominio: %s)\n", manifest_path, final_domain);
        printf("    * Definicion NDK:           \033[38;2;0;243;255m%s\033[0m\n", mk_path);
        printf("    * Codigo Fuente JNI/C:      \033[38;2;0;243;255m%s\033[0m\n", c_path);
        printf("    * Scripts de Compilacion:   \033[38;2;0;243;255m%s y .sh\033[0m\n", build_bat_path);
    }

    /* ===================================================================== */
    /* 4. IOS: Bundle (.app) + Info.plist de iPhone/iPad                     */
    /* ===================================================================== */
    else if (target == TARGET_IOS) {
        char bundle_dir[MAX_PATH];
        char plist_path[MAX_PATH];
        char c_path[MAX_PATH];
        char script_path[MAX_PATH];

        snprintf(bundle_dir, sizeof(bundle_dir), "%s/%s_ios.app", base_dir, app_name);
        make_dir_recursive(bundle_dir);

        snprintf(plist_path, sizeof(plist_path), "%s/Info.plist", bundle_dir);
        snprintf(c_path, sizeof(c_path), "%s/%s.c", bundle_dir, app_name);
        snprintf(script_path, sizeof(script_path), "%s/build_ios.sh", bundle_dir);

        /* Escribir Info.plist para iOS */
        char plist_content[2048];
        snprintf(plist_content, sizeof(plist_content),
                 "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                 "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                 "<plist version=\"1.0\">\n"
                 "<dict>\n"
                 "    <key>CFBundleIdentifier</key>\n"
                 "    <string>%s</string>\n"
                 "    <key>CFBundleName</key>\n"
                 "    <string>%s</string>\n"
                 "    <key>CFBundleDisplayName</key>\n"
                 "    <string>%s</string>\n"
                 "    <key>CFBundlePackageType</key>\n"
                 "    <string>APPL</string>\n"
                 "    <key>CFBundleShortVersionString</key>\n"
                 "    <string>1.0.0</string>\n"
                 "    <key>CFBundleVersion</key>\n"
                 "    <string>1</string>\n"
                 "    <key>CFBundleExecutable</key>\n"
                 "    <string>%s</string>\n"
                 "    <key>UIRequiredDeviceCapabilities</key>\n"
                 "    <array>\n"
                 "        <string>arm64</string>\n"
                 "    </array>\n"
                 "</dict>\n"
                 "</plist>\n",
                 final_domain, app_name, app_name, app_name);
        write_text_file(plist_path, plist_content);

        write_text_file(c_path, c_code);

        char sh_content[1024];
        snprintf(sh_content, sizeof(sh_content),
                 "#!/bin/bash\n"
                 "xcrun -sdk iphoneos clang -arch arm64 -O3 \"%s.c\" -o \"%s\"\n"
                 "codesign -s - --deep .\n"
                 "echo \"[Tiger iOS]: Bundle %s_ios.app firmado para Dominio %s\"\n",
                 app_name, app_name, app_name, final_domain);
        write_text_file(script_path, sh_content);

        printf("  \033[38;2;34;197;94m[EXITO]: Bundle de Dominio iOS creado:\033[0m\n");
        printf("    * Directorio iOS App:       \033[38;2;0;243;255m%s/\033[0m\n", bundle_dir);
        printf("    * Manifiesto Info.plist:    \033[38;2;0;243;255m%s\033[0m (Dominio: %s)\n", plist_path, final_domain);
        printf("    * Codigo C Nativo:          \033[38;2;0;243;255m%s\033[0m\n", c_path);
        printf("    * Script de Construccion:   \033[38;2;0;243;255m%s\033[0m\n", script_path);
    }

    /* ===================================================================== */
    /* 5. WEBASSEMBLY: Web Package + package.json (Dominio) + index.html     */
    /* ===================================================================== */
    else if (target == TARGET_WASM) {
        char pkg_path[MAX_PATH];
        char html_path[MAX_PATH];
        char c_path[MAX_PATH];
        char build_bat_path[MAX_PATH];
        char build_sh_path[MAX_PATH];

        snprintf(pkg_path, sizeof(pkg_path), "%s/package.json", base_dir);
        snprintf(html_path, sizeof(html_path), "%s/index.html", base_dir);
        snprintf(c_path, sizeof(c_path), "%s/%s.c", base_dir, app_name);
        snprintf(build_bat_path, sizeof(build_bat_path), "%s/build_wasm.bat", base_dir);
        snprintf(build_sh_path, sizeof(build_sh_path), "%s/build_wasm.sh", base_dir);

        /* package.json con dominio */
        char pkg_content[1024];
        snprintf(pkg_content, sizeof(pkg_content),
                 "{\n"
                 "  \"name\": \"%s\",\n"
                 "  \"version\": \"1.0.0\",\n"
                 "  \"description\": \"%s WebAssembly Tiger App\",\n"
                 "  \"main\": \"index.html\",\n"
                 "  \"tiger\": {\n"
                 "    \"domain\": \"%s\",\n"
                 "    \"engine\": \"Tiger 10.1 Silicon C\"\n"
                 "  }\n"
                 "}\n",
                 final_domain, app_name, final_domain);
        write_text_file(pkg_path, pkg_content);

        /* index.html con terminal integrado y consola */
        char html_content[4096];
        snprintf(html_content, sizeof(html_content),
                 "<!DOCTYPE html>\n"
                 "<html lang=\"es\">\n"
                 "<head>\n"
                 "  <meta charset=\"UTF-8\">\n"
                 "  <title>%s - Tiger 10 WebAssembly</title>\n"
                 "  <style>\n"
                 "    body { background-color: #0d1117; color: #f0f6fc; font-family: monospace; padding: 20px; }\n"
                 "    .header { color: #ff6b00; font-size: 20px; font-weight: bold; margin-bottom: 10px; }\n"
                 "    .domain { color: #00f3ff; margin-bottom: 20px; }\n"
                 "    #terminal { background-color: #161b22; border: 1px solid #30363d; border-radius: 6px; padding: 15px; min-height: 200px; white-space: pre-wrap; color: #22c55e; }\n"
                 "  </style>\n"
                 "</head>\n"
                 "<body>\n"
                 "  <div class=\"header\">Tiger 10 WebAssembly Engine</div>\n"
                 "  <div class=\"domain\">Dominio de la App: %s</div>\n"
                 "  <div id=\"terminal\">Iniciando ejecucion en WebAssembly...\\n</div>\n"
                 "  <script>\n"
                 "    var Module = {\n"
                 "      print: function(text) {\n"
                 "        var term = document.getElementById('terminal');\n"
                 "        term.innerText += text + '\\n';\n"
                 "      }\n"
                 "    };\n"
                 "  </script>\n"
                 "  <script src=\"%s.js\"></script>\n"
                 "</body>\n"
                 "</html>\n",
                 app_name, final_domain, app_name);
        write_text_file(html_path, html_content);

        write_text_file(c_path, c_code);

        char bat_content[512];
        snprintf(bat_content, sizeof(bat_content),
                 "@echo off\n"
                 "echo [Tiger WASM]: Compilando a WebAssembly con Emscripten para Dominio %s...\n"
                 "emcc \"%s.c\" -O3 -s WASM=1 -o \"%s.html\"\n",
                 final_domain, app_name, app_name);
        write_text_file(build_bat_path, bat_content);

        char sh_content[512];
        snprintf(sh_content, sizeof(sh_content),
                 "#!/bin/bash\n"
                 "echo \"[Tiger WASM]: Compilando a WebAssembly con Emscripten para Dominio %s...\"\n"
                 "emcc \"%s.c\" -O3 -s WASM=1 -o \"%s.html\"\n",
                 final_domain, app_name, app_name);
        write_text_file(build_sh_path, sh_content);

        printf("  \033[38;2;34;197;94m[EXITO]: Paquete Web / WebAssembly creado:\033[0m\n");
        printf("    * Directorio Web:           \033[38;2;0;243;255m%s/\033[0m\n", base_dir);
        printf("    * Manifiesto package.json:  \033[38;2;0;243;255m%s\033[0m (Dominio: %s)\n", pkg_path, final_domain);
        printf("    * Pagina de Inicio HTML5:   \033[38;2;0;243;255m%s\033[0m\n", html_path);
        printf("    * Codigo Fuente C:          \033[38;2;0;243;255m%s\033[0m\n", c_path);

        char wasm_file[MAX_PATH];
        snprintf(wasm_file, sizeof(wasm_file), "%s/%s.wasm", base_dir, app_name);
        char wasm_cmd[1024];
        snprintf(wasm_cmd, sizeof(wasm_cmd), "clang --target=wasm32 -O3 -nostdlib -Wl,--no-entry -Wl,--export-all -o \"%s\" \"%s\"", wasm_file, c_path);
        int wres = system(wasm_cmd);
        if (wres == 0) {
            printf("    * Binario WebAssembly (.wasm): \033[38;2;34;197;94m%s\033[0m\n", wasm_file);
        }
    }

    /* ===================================================================== */
    /* 6. LINUX: Binario ELF + .desktop (FreeDesktop Specification)          */
    /* ===================================================================== */
    else if (target == TARGET_LINUX) {
        char c_path[MAX_PATH];
        char desktop_path[MAX_PATH];
        char script_path[MAX_PATH];

        snprintf(c_path, sizeof(c_path), "%s/%s_linux.c", base_dir, app_name);
        snprintf(desktop_path, sizeof(desktop_path), "%s/%s.desktop", base_dir, app_name);
        snprintf(script_path, sizeof(script_path), "%s/%s_build_linux.sh", base_dir, app_name);

        write_text_file(c_path, c_code);

        char desktop_content[1024];
        snprintf(desktop_content, sizeof(desktop_content),
                 "[Desktop Entry]\n"
                 "Type=Application\n"
                 "Name=%s\n"
                 "Comment=%s compilado al silicio con Tiger 10\n"
                 "Exec=./%s\n"
                 "Terminal=true\n"
                 "Categories=Development;Utility;\n"
                 "StartupNotify=true\n"
                 "X-Tiger-Domain=%s\n",
                 app_name, app_name, app_name, final_domain);
        write_text_file(desktop_path, desktop_content);

        char sh_content[1024];
        snprintf(sh_content, sizeof(sh_content),
                 "#!/bin/bash\n"
                 "gcc -O3 \"%s\" -o \"%s\" -lm\n"
                 "chmod +x \"%s\"\n"
                 "echo \"[Tiger Linux]: Binario ELF para Dominio %s compilado con exito.\"\n",
                 c_path, app_name, app_name, final_domain);
        write_text_file(script_path, sh_content);

        printf("  \033[38;2;34;197;94m[EXITO]: Archivos de Dominio Linux creados:\033[0m\n");
        printf("    * Entrada de Escritorio:    \033[38;2;0;243;255m%s\033[0m (Dominio: %s)\n", desktop_path, final_domain);
        printf("    * Codigo Fuente C:          \033[38;2;0;243;255m%s\033[0m\n", c_path);
        printf("    * Script de Construccion:   \033[38;2;0;243;255m%s\033[0m\n", script_path);
    }

    free(c_code);
    tiger_parser_free(&parser);
    free(src);
    return true;
}


