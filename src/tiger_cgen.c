#include "tiger_cgen.h"
#include "tiger_parser.h"

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
    if (strcasecmp(name, "linux") == 0 || strcasecmp(name, "gnu") == 0) return TARGET_LINUX;
    if (strcasecmp(name, "android") == 0 || strcasecmp(name, "apk") == 0) return TARGET_ANDROID;
    if (strcasecmp(name, "ios") == 0 || strcasecmp(name, "iphone") == 0) return TARGET_IOS;
    if (strcasecmp(name, "wasm") == 0 || strcasecmp(name, "wat") == 0 || strcasecmp(name, "web") == 0) return TARGET_WASM;
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

        case STMT_FUNC_DECL: {
            /* Las funciones se emiten a nivel global antes de main */
            break;
        }

        case STMT_POLYGLOT: {
            if (strcasecmp(stmt->as.polyglot.lang, "C") == 0 || strcasecmp(stmt->as.polyglot.lang, "CPP") == 0) {
                cg_writeln(b, "/* Bloque TIPO C nativo inyectado directamente */");
                cg_writeln(b, "{");
                b->indent_level++;
                cg_writeln(b, stmt->as.polyglot.code);
                b->indent_level--;
                cg_writeln(b, "}");
            }
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

    /* 3. Funciones definidas por el usuario a nivel global */
    cg_writeln(&b, "\n/* Declaraciones de Funciones Tiger */");
    for (size_t i = 0; i < count; i++) {
        if (stmts[i]->type == STMT_FUNC_DECL) {
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

    /* 4. Cuerpo Principal del Programa Tiger */
    cg_writeln(&b, "/* Logica Ejecutiva del Programa Tiger */");
    cg_writeln(&b, "int tiger_program_entry(void) {");
    b.indent_level = 1;

    for (size_t i = 0; i < count; i++) {
        if (stmts[i]->type != STMT_FUNC_DECL) {
            emit_statement(&b, stmts[i]);
        }
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

bool tiger_compile_to_binary(const char* input_path, const char* output_path, TigerTargetOS target, const char* domain, bool run_after) {
    size_t size = 0;
    char* src = tiger_read_file_resilient(input_path, &size);
    if (!src) {
        fprintf(stderr, "[Error Tiger]: No se pudo leer el archivo '%s'\n", input_path);
        return false;
    }

    TigerParser parser;
    tiger_parser_init(&parser, src, size, true);

    size_t count = 0;
    TigerStmt** stmts = tiger_parse_program(&parser, &count);

    if (parser.has_error) {
        tiger_parser_free(&parser);
        free(src);
        return false;
    }

    /* Extraer nombre base de la aplicación y dominio */
    char app_name[128];
    extract_app_name(output_path, app_name, sizeof(app_name));

    char final_domain[256];
    if (domain && domain[0]) {
        strncpy(final_domain, domain, sizeof(final_domain) - 1);
        final_domain[sizeof(final_domain) - 1] = '\0';
    } else {
        snprintf(final_domain, sizeof(final_domain), "com.tiger.%s", app_name);
    }

    /* Generar C autónomo para el SO de destino */
    char* c_code = tiger_ast_to_c(stmts, count, target, app_name, final_domain);

    printf("\n\033[38;2;255;107;0m======================================================================\033[0m\n");
    printf("\033[38;2;255;107;0m[TIGER 10.1 - GENERADOR NATIVO AL SILICIO Y DOMINIOS]\033[0m\n");
    printf("  * Archivo Fuente:   \033[38;2;0;243;255m%s\033[0m\n", input_path);
    printf("  * Nombre de App:    \033[38;2;0;243;255m%s\033[0m\n", app_name);
    printf("  * Dominio de App:   \033[38;2;34;197;94m%s\033[0m\n", final_domain);
    printf("  * Plataforma Meta:  \033[38;2;0;243;255m%s\033[0m\n", tiger_target_name(target));
    printf("\033[38;2;255;107;0m======================================================================\033[0m\n");

    /* ===================================================================== */
    /* 1. WINDOWS: Binario PE (.exe) + Manifiesto XML con Dominio            */
    /* ===================================================================== */
    if (target == TARGET_WINDOWS) {
        char exe_path[MAX_PATH];
        char manifest_path[MAX_PATH];
        char c_path[MAX_PATH];

        if (output_path && output_path[0]) {
            if (strstr(output_path, ".exe")) {
                snprintf(exe_path, sizeof(exe_path), "%s", output_path);
            } else {
                snprintf(exe_path, sizeof(exe_path), "%s.exe", output_path);
            }
        } else {
            snprintf(exe_path, sizeof(exe_path), "%s.exe", app_name);
        }

        snprintf(manifest_path, sizeof(manifest_path), "%s.manifest", exe_path);
        snprintf(c_path, sizeof(c_path), "%s_win.c", app_name);

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

        /* Compilar a .EXE con TCC */
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "bin\\tcc\\tcc.exe -o \"%s\" \"%s\"", exe_path, c_path);
        int res = system(cmd);
        if (res != 0) {
            snprintf(cmd, sizeof(cmd), "tcc.exe -o \"%s\" \"%s\"", exe_path, c_path);
            res = system(cmd);
        }

        if (res == 0) {
            printf("  \033[38;2;34;197;94m[EXITO]: Archivos de dominio de Windows creados:\033[0m\n");
            printf("    * Binario PE x86_64:        \033[38;2;0;243;255m%s\033[0m\n", exe_path);
            printf("    * Manifiesto con Dominio:   \033[38;2;0;243;255m%s\033[0m\n", manifest_path);
            printf("    * Codigo Fuente C:          \033[38;2;0;243;255m%s\033[0m\n", c_path);

            if (run_after) {
                printf("\n\033[38;2;0;243;255m[Ejecutando binario nativo en silicio directamentemente...]\033[0m\n");
                snprintf(cmd, sizeof(cmd), "\"%s\"", exe_path);
                system(cmd);
            }
        } else {
            fprintf(stderr, "  * [ERROR]: Fallo al ensamblar el binario PE con TCC.\n");
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

        snprintf(bundle_dir, sizeof(bundle_dir), "%s.app", app_name);
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
        char proj_dir[MAX_PATH];
        char jni_dir[MAX_PATH];
        char res_vals_dir[MAX_PATH];
        char manifest_path[MAX_PATH];
        char mk_path[MAX_PATH];
        char app_mk_path[MAX_PATH];
        char strings_path[MAX_PATH];
        char c_path[MAX_PATH];
        char build_bat_path[MAX_PATH];
        char build_sh_path[MAX_PATH];

        snprintf(proj_dir, sizeof(proj_dir), "%s_android", app_name);
        snprintf(jni_dir, sizeof(jni_dir), "%s/jni", proj_dir);
        snprintf(res_vals_dir, sizeof(res_vals_dir), "%s/res/values", proj_dir);

        make_dir_recursive(jni_dir);
        make_dir_recursive(res_vals_dir);

        snprintf(manifest_path, sizeof(manifest_path), "%s/AndroidManifest.xml", proj_dir);
        snprintf(mk_path, sizeof(mk_path), "%s/Android.mk", jni_dir);
        snprintf(app_mk_path, sizeof(app_mk_path), "%s/Application.mk", jni_dir);
        snprintf(strings_path, sizeof(strings_path), "%s/strings.xml", res_vals_dir);
        snprintf(c_path, sizeof(c_path), "%s/%s_android.c", jni_dir, app_name);
        snprintf(build_bat_path, sizeof(build_bat_path), "%s/build_android.bat", proj_dir);
        snprintf(build_sh_path, sizeof(build_sh_path), "%s/build_android.sh", proj_dir);

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
        printf("    * Directorio Proyecto:      \033[38;2;0;243;255m%s/\033[0m\n", proj_dir);
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

        snprintf(bundle_dir, sizeof(bundle_dir), "%s_ios.app", app_name);
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
        char web_dir[MAX_PATH];
        char pkg_path[MAX_PATH];
        char html_path[MAX_PATH];
        char c_path[MAX_PATH];
        char build_bat_path[MAX_PATH];
        char build_sh_path[MAX_PATH];

        snprintf(web_dir, sizeof(web_dir), "%s_web", app_name);
        make_dir_recursive(web_dir);

        snprintf(pkg_path, sizeof(pkg_path), "%s/package.json", web_dir);
        snprintf(html_path, sizeof(html_path), "%s/index.html", web_dir);
        snprintf(c_path, sizeof(c_path), "%s/%s.c", web_dir, app_name);
        snprintf(build_bat_path, sizeof(build_bat_path), "%s/build_wasm.bat", web_dir);
        snprintf(build_sh_path, sizeof(build_sh_path), "%s/build_wasm.sh", web_dir);

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
        printf("    * Directorio Web:           \033[38;2;0;243;255m%s/\033[0m\n", web_dir);
        printf("    * Manifiesto package.json:  \033[38;2;0;243;255m%s\033[0m (Dominio: %s)\n", pkg_path, final_domain);
        printf("    * Pagina de Inicio HTML5:   \033[38;2;0;243;255m%s\033[0m\n", html_path);
        printf("    * Codigo Fuente C:          \033[38;2;0;243;255m%s\033[0m\n", c_path);
    }

    /* ===================================================================== */
    /* 6. LINUX: Binario ELF + .desktop (FreeDesktop Specification)          */
    /* ===================================================================== */
    else if (target == TARGET_LINUX) {
        char c_path[MAX_PATH];
        char desktop_path[MAX_PATH];
        char script_path[MAX_PATH];

        snprintf(c_path, sizeof(c_path), "%s_linux.c", app_name);
        snprintf(desktop_path, sizeof(desktop_path), "%s.desktop", app_name);
        snprintf(script_path, sizeof(script_path), "%s_build_linux.sh", app_name);

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


