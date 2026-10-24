#include "tiger_parser.h"

typedef enum {
    PREC_NONE = 0,
    PREC_ASSIGN,     /* <-, =, +=, -= */
    PREC_OR,         /* ||, O, OR */
    PREC_AND,        /* &&, Y, AND */
    PREC_EQUALITY,   /* ==, != */
    PREC_COMPARISON, /* <, <=, >, >= */
    PREC_TERM,       /* +, - */
    PREC_FACTOR,     /* *, /, % */
    PREC_UNARY,      /* !, -, + */
    PREC_CALL,       /* (), [], ., | */
    PREC_PRIMARY
} Precedence;

static void advance_p(TigerParser* p) {
    tiger_token_free(&p->previous);
    p->previous = p->current;

    for (;;) {
        p->current = tiger_lexer_next_token(&p->lexer);
        if (p->current.type != TOK_ERROR) break;
        fprintf(stderr, "[Error Sintáctico / Syntax Error L%d:C%d]: Carácter no reconocido '%s'\n",
                p->current.line, p->current.column, p->current.text);
        p->has_error = true;
    }
}

static bool check(TigerParser* p, TigerTokenType type) {
    return p->current.type == type;
}

static bool match(TigerParser* p, TigerTokenType type) {
    if (!check(p, type)) return false;
    advance_p(p);
    return true;
}

static void consume(TigerParser* p, TigerTokenType type, const char* message) {
    if (p->current.type == type) {
        advance_p(p);
        return;
    }
    fprintf(stderr, "[Error Sintáctico / Syntax Error L%d:C%d]: %s (encontrado '%s')\n",
            p->current.line, p->current.column, message, p->current.text ? p->current.text : "");
    p->has_error = true;
    p->panic_mode = true;
}

static bool consume_period_flexible(TigerParser* p) {
    if (match(p, TOK_COMMA)) return true; /* Subproceso terminado en coma ',' */
    if (match(p, TOK_DOT) || match(p, TOK_SEMICOLON)) return false; /* Proceso principal en punto '.' */
    if (!p->flexible) {
        /* En modo estricto se avisa si falta el punto */
        if (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            consume(p, TOK_DOT, "Se esperaba '.' (proceso) o ',' (subproceso)");
        }
    }
    return false;
}

/* ========================================================================= */
/* PARSER DE EXPRESIONES (PRATT PARSER)                                      */
/* ========================================================================= */

static TigerExpr* parse_expression(TigerParser* p);
static TigerExpr* parse_precedence(TigerParser* p, Precedence prec);

static Precedence get_precedence(TigerTokenType type) {
    switch (type) {
        case TOK_OR: return PREC_OR;
        case TOK_AND: return PREC_AND;
        case TOK_EQ:
        case TOK_NEQ: return PREC_EQUALITY;
        case TOK_LT:
        case TOK_LE:
        case TOK_GT:
        case TOK_GE: return PREC_COMPARISON;
        case TOK_PLUS:
        case TOK_MINUS: return PREC_TERM;
        case TOK_STAR:
        case TOK_SLASH:
        case TOK_PERCENT: return PREC_FACTOR;
        case TOK_LPAREN:
        case TOK_LBRACKET:
        case TOK_DOT: return PREC_CALL;
        default: return PREC_NONE;
    }
}

static TigerExpr* parse_primary(TigerParser* p) {
    int line = p->current.line;

    if (match(p, TOK_INT)) {
        return tiger_expr_literal(tiger_val_int(p->previous.i_val), line);
    }
    if (match(p, TOK_FLOAT)) {
        return tiger_expr_literal(tiger_val_float(p->previous.f_val), line);
    }
    if (match(p, TOK_STRING)) {
        return tiger_expr_literal(tiger_val_str(p->previous.text), line);
    }
    if (match(p, TOK_BOOL_TRUE)) {
        return tiger_expr_literal(tiger_val_bool(true), line);
    }
    if (match(p, TOK_BOOL_FALSE)) {
        return tiger_expr_literal(tiger_val_bool(false), line);
    }

    /* Entrada interactiva por teclado: ENTRADA(...) o ENTRADA "mensaje" o ENTRADA */
    if (match(p, TOK_ENTRADA)) {
        TigerExpr* prompt = NULL;
        if (match(p, TOK_LPAREN)) {
            if (!check(p, TOK_RPAREN)) {
                prompt = parse_expression(p);
            }
            consume(p, TOK_RPAREN, "Se esperaba ')' tras ENTRADA(...)");
        } else if (check(p, TOK_STRING)) {
            advance_p(p);
            prompt = tiger_expr_literal(tiger_val_str(p->previous.text), line);
        }
        return tiger_expr_input(prompt, line);
    }

    if (match(p, TOK_IDENT)) {
        char* name = strdup(p->previous.text);

        /* Si es una llamada a función: identificador(...) */
        if (match(p, TOK_LPAREN)) {
            TigerExpr* callee = tiger_expr_var(name, line);
            free(name);

            size_t cap = 4;
            size_t argc = 0;
            TigerExpr** args = (TigerExpr**)malloc(cap * sizeof(TigerExpr*));

            if (!check(p, TOK_RPAREN)) {
                do {
                    if (argc >= cap) {
                        cap *= 2;
                        args = (TigerExpr**)realloc(args, cap * sizeof(TigerExpr*));
                    }
                    args[argc++] = parse_expression(p);
                } while (match(p, TOK_PIPE) || match(p, TOK_COMMA));
            }
            consume(p, TOK_RPAREN, "Se esperaba ')' al final de los argumentos");
            return tiger_expr_call(callee, args, argc, line);
        }

        TigerExpr* var_expr = tiger_expr_var(name, line);
        free(name);
        return var_expr;
    }

    /* Agrupación con paréntesis: (expr) */
    if (match(p, TOK_LPAREN)) {
        TigerExpr* expr = parse_expression(p);
        consume(p, TOK_RPAREN, "Se esperaba ')' de cierre de expresión");
        return expr;
    }

    /* Operador unario: !, -, + */
    if (match(p, TOK_NOT) || match(p, TOK_MINUS) || match(p, TOK_PLUS)) {
        TigerTokenType op = p->previous.type;
        TigerExpr* right = parse_precedence(p, PREC_UNARY);
        return tiger_expr_unary(op, right, line);
    }

    /* Arreglos literales: [e1, e2, ...] */
    if (match(p, TOK_LBRACKET)) {
        size_t cap = 4;
        size_t count = 0;
        TigerExpr** elems = (TigerExpr**)malloc(cap * sizeof(TigerExpr*));

        if (!check(p, TOK_RBRACKET)) {
            do {
                if (count >= cap) {
                    cap *= 2;
                    elems = (TigerExpr**)realloc(elems, cap * sizeof(TigerExpr*));
                }
                elems[count++] = parse_expression(p);
            } while (match(p, TOK_COMMA) || match(p, TOK_PIPE));
        }
        consume(p, TOK_RBRACKET, "Se esperaba ']' de cierre de arreglo");
        TigerExpr* arr = (TigerExpr*)malloc(sizeof(TigerExpr));
        arr->type = EXPR_ARRAY_LIT;
        arr->line = line;
        arr->hash = 0;
        arr->as.array_lit.elements = elems;
        arr->as.array_lit.count = count;
        return arr;
    }

    /* Diccionarios literales: { "clave": valor, ... } */
    if (match(p, TOK_LBRACE)) {
        size_t cap = 4;
        size_t count = 0;
        char** keys = (char**)malloc(cap * sizeof(char*));
        TigerExpr** vals = (TigerExpr**)malloc(cap * sizeof(TigerExpr*));

        if (!check(p, TOK_RBRACE)) {
            do {
                if (check(p, TOK_RBRACE)) break;
                char* key = NULL;
                if (match(p, TOK_STRING) || match(p, TOK_IDENT)) {
                    key = strdup(p->previous.text);
                } else {
                    key = strdup("clave");
                }
                consume(p, TOK_COLON, "Se esperaba ':' entre clave y valor del diccionario");
                TigerExpr* val = parse_expression(p);

                if (count >= cap) {
                    cap *= 2;
                    keys = (char**)realloc(keys, cap * sizeof(char*));
                    vals = (TigerExpr**)realloc(vals, cap * sizeof(TigerExpr*));
                }
                keys[count] = key;
                vals[count] = val;
                count++;
            } while (match(p, TOK_COMMA) || match(p, TOK_PIPE));
        }
        consume(p, TOK_RBRACE, "Se esperaba '}' de cierre de diccionario");
        TigerExpr* dict_expr = (TigerExpr*)malloc(sizeof(TigerExpr));
        dict_expr->type = EXPR_DICT_LIT;
        dict_expr->line = line;
        dict_expr->hash = 0;
        dict_expr->as.dict_lit.keys = keys;
        dict_expr->as.dict_lit.values = vals;
        dict_expr->as.dict_lit.count = count;
        return dict_expr;
    }

    /* Invocación de Asistente IA como expresión: ASISTENTE("api" | "model" | "prompt") */
    if (match(p, TOK_ASISTENTE)) {
        char* name = strdup("ASISTENTE");
        if (match(p, TOK_LPAREN)) {
            TigerExpr* callee = tiger_expr_var(name, line);
            free(name);
            size_t cap = 4, argc = 0;
            TigerExpr** args = (TigerExpr**)malloc(cap * sizeof(TigerExpr*));
            if (!check(p, TOK_RPAREN)) {
                do {
                    if (argc >= cap) {
                        cap *= 2;
                        args = (TigerExpr**)realloc(args, cap * sizeof(TigerExpr*));
                    }
                    args[argc++] = parse_expression(p);
                } while (match(p, TOK_PIPE) || match(p, TOK_COMMA));
            }
            consume(p, TOK_RPAREN, "Se esperaba ')' tras ASISTENTE(...)");
            return tiger_expr_call(callee, args, argc, line);
        }
        TigerExpr* var_expr = tiger_expr_var(name, line);
        free(name);
        return var_expr;
    }

    fprintf(stderr, "[Error Sintáctico L%d]: Expresión inválida cerca de '%s'\n",
            p->current.line, p->current.text ? p->current.text : "");
    p->has_error = true;
    advance_p(p);
    return tiger_expr_literal(tiger_val_nil(), line);
}

static TigerExpr* parse_precedence(TigerParser* p, Precedence prec) {
    TigerExpr* left = parse_primary(p);

    while (prec <= get_precedence(p->current.type)) {
        if (p->current.type == TOK_DOT) {
            TigerLexer lookahead = p->lexer;
            TigerToken next_t = tiger_lexer_next_token(&lookahead);
            /* Detector Inteligente: Solo es acceso a miembro si el identificador esta en la misma linea pegado al punto */
            bool is_member = (next_t.type == TOK_IDENT && next_t.line == p->current.line);
            tiger_token_free(&next_t);
            if (!is_member) break; /* Es terminador '.' de fin de sentencia, no acceso a miembro */
        }

        TigerTokenType op = p->current.type;
        advance_p(p);

        /* Acceso a miembros con '.' o llamada a métodos */
        if (op == TOK_DOT && check(p, TOK_IDENT)) {
            advance_p(p);
            char* member = strdup(p->previous.text);
            left = tiger_expr_member(left, member, p->previous.line);
            free(member);
            continue;
        }

        /* Acceso a índices: left[idx] */
        if (op == TOK_LBRACKET) {
            TigerExpr* idx = parse_expression(p);
            consume(p, TOK_RBRACKET, "Se esperaba ']' de cierre de índice");
            left = tiger_expr_index(left, idx, p->previous.line);
            continue;
        }

        /* Llamada a función / método encadenada: left(...) */
        if (op == TOK_LPAREN) {
            size_t cap = 4, argc = 0;
            TigerExpr** args = (TigerExpr**)malloc(cap * sizeof(TigerExpr*));
            if (!check(p, TOK_RPAREN)) {
                do {
                    if (argc >= cap) {
                        cap *= 2;
                        args = (TigerExpr**)realloc(args, cap * sizeof(TigerExpr*));
                    }
                    args[argc++] = parse_expression(p);
                } while (match(p, TOK_PIPE) || match(p, TOK_COMMA));
            }
            consume(p, TOK_RPAREN, "Se esperaba ')' al final de argumentos");
            left = tiger_expr_call(left, args, argc, p->previous.line);
            continue;
        }

        /* Operador binario estándar */
        Precedence next_prec = (Precedence)(get_precedence(op) + 1);
        TigerExpr* right = parse_precedence(p, next_prec);
        left = tiger_expr_binary(op, left, right, p->previous.line);
    }

    return left;
}

static TigerExpr* parse_expression(TigerParser* p) {
    return parse_precedence(p, PREC_ASSIGN);
}

/* ========================================================================= */
/* PARSER DE SENTENCIAS Y BLOQUES                                            */
/* ========================================================================= */

static TigerStmt* parse_statement(TigerParser* p);

static TigerStmt** parse_block(TigerParser* p, size_t* out_count) {
    consume(p, TOK_LBRACE, "Se esperaba '{' para iniciar bloque");

    size_t cap = 8;
    size_t count = 0;
    TigerStmt** stmts = (TigerStmt**)malloc(cap * sizeof(TigerStmt*));

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        TigerStmt* s = parse_statement(p);
        if (s) {
            if (count >= cap) {
                cap *= 2;
                stmts = (TigerStmt**)realloc(stmts, cap * sizeof(TigerStmt*));
            }
            stmts[count++] = s;
        }
    }

    consume(p, TOK_RBRACE, "Se esperaba '}' para cerrar bloque");
    *out_count = count;
    return stmts;
}

/* Extraer el contenido crudo de un bloque políglota: TIPO <lang> { ... }. */
static char* extract_raw_polyglot_block(TigerParser* p) {
    if (!check(p, TOK_LBRACE)) {
        consume(p, TOK_LBRACE, "Se esperaba '{' para iniciar bloque políglota");
        return strdup("");
    }

    /* p->current es TOK_LBRACE. El contenido empieza inmediatamente después del '{' */
    size_t start_pos = p->current.offset + 1;
    const char* src = p->lexer.source;
    p->lexer.cursor = start_pos;
    int brace_depth = 1;

    while (p->lexer.cursor < p->lexer.length && brace_depth > 0) {
        char c = src[p->lexer.cursor++];
        if (c == '{') brace_depth++;
        else if (c == '}') brace_depth--;
        else if (c == '\n') {
            p->lexer.line++;
            p->lexer.column = 1;
        }
    }

    size_t end_pos = (p->lexer.cursor > 0) ? (p->lexer.cursor - 1) : 0; /* Sin la llave '}' */
    size_t len = (end_pos > start_pos) ? (end_pos - start_pos) : 0;
    char* raw = (char*)malloc(len + 1);
    if (len > 0) memcpy(raw, src + start_pos, len);
    raw[len] = '\0';

    /* Liberar token '{' y cargar el siguiente token después de '}' */
    tiger_token_free(&p->current);
    p->current = tiger_lexer_next_token(&p->lexer);
    return raw;
}

static TigerStmt* parse_statement(TigerParser* p) {
    int line = p->current.line;

    /* 1. Declaración de Variables: VAR [TIPO] name [= init]. o [TIPO] VAR name [= init]. */
    TigerValType type_req = VAL_NIL;
    bool is_typed_prefix = false;

    if (check(p, TOK_KW_ENT) || check(p, TOK_KW_FLOT) || check(p, TOK_KW_CAD) || check(p, TOK_KW_BOOL) || check(p, TOK_KW_PUNT)) {
        TigerTokenType t = p->current.type;
        if (t == TOK_KW_ENT) type_req = VAL_INT;
        else if (t == TOK_KW_FLOT) type_req = VAL_FLOAT;
        else if (t == TOK_KW_CAD) type_req = VAL_STR;
        else if (t == TOK_KW_BOOL) type_req = VAL_BOOL;
        else if (t == TOK_KW_PUNT) type_req = VAL_PTR;
        advance_p(p);
        is_typed_prefix = true;
    }

    if (match(p, TOK_VAR) || match(p, TOK_CONST) || is_typed_prefix) {
        bool is_const = (p->previous.type == TOK_CONST);

        /* Si vino 'VAR ENT x' o similar */
        if (!is_typed_prefix && (check(p, TOK_KW_ENT) || check(p, TOK_KW_FLOT) || check(p, TOK_KW_CAD) || check(p, TOK_KW_BOOL) || check(p, TOK_KW_PUNT))) {
            TigerTokenType t = p->current.type;
            if (t == TOK_KW_ENT) type_req = VAL_INT;
            else if (t == TOK_KW_FLOT) type_req = VAL_FLOAT;
            else if (t == TOK_KW_CAD) type_req = VAL_STR;
            else if (t == TOK_KW_BOOL) type_req = VAL_BOOL;
            else if (t == TOK_KW_PUNT) type_req = VAL_PTR;
            advance_p(p);
        }

        consume(p, TOK_IDENT, "Se esperaba el nombre de la variable");
        char* name = strdup(p->previous.text);

        TigerExpr* init = NULL;
        if (match(p, TOK_ASSIGN) || match(p, TOK_ARROW_L)) {
            init = parse_expression(p);
        }

        bool is_sub = consume_period_flexible(p);
        TigerStmt* s = tiger_stmt_var_decl(name, is_const, type_req, init, line);
        s->is_subprocess = is_sub;
        free(name);
        return s;
    }

    /* 2. IMPRIMIR / PRINT */
    if (match(p, TOK_IMPRIMIR)) {
        TigerExpr* expr = parse_expression(p);
        bool is_sub = consume_period_flexible(p);
        TigerStmt* s = tiger_stmt_print(expr, true, line);
        s->is_subprocess = is_sub;
        return s;
    }

    /* ENTRADA / INPUT / LEER: ENTRADA "prompt" -> [VAR] [TIPO] destino. */
    if (match(p, TOK_ENTRADA)) {
        TigerExpr* prompt = NULL;
        if (match(p, TOK_LPAREN)) {
            if (!check(p, TOK_RPAREN)) {
                prompt = parse_expression(p);
            }
            consume(p, TOK_RPAREN, "Se esperaba ')' tras ENTRADA(...)");
        } else if (check(p, TOK_STRING)) {
            advance_p(p);
            prompt = tiger_expr_literal(tiger_val_str(p->previous.text), line);
        }

        consume(p, TOK_ARROW_R, "Se esperaba '->' despues de ENTRADA");

        bool is_var = match(p, TOK_VAR);
        TigerValType type_req = VAL_NIL;
        if (match(p, TOK_KW_ENT)) type_req = VAL_INT;
        else if (match(p, TOK_KW_FLOT)) type_req = VAL_FLOAT;
        else if (match(p, TOK_KW_CAD)) type_req = VAL_STR;
        else if (match(p, TOK_KW_BOOL)) type_req = VAL_BOOL;

        if (is_var && type_req == VAL_NIL) {
            if (match(p, TOK_KW_ENT)) type_req = VAL_INT;
            else if (match(p, TOK_KW_FLOT)) type_req = VAL_FLOAT;
            else if (match(p, TOK_KW_CAD)) type_req = VAL_STR;
            else if (match(p, TOK_KW_BOOL)) type_req = VAL_BOOL;
        }

        consume(p, TOK_IDENT, "Se esperaba el nombre de la variable destino");
        char* name = strdup(p->previous.text);
        bool is_sub = consume_period_flexible(p);

        TigerExpr* in_expr = tiger_expr_input(prompt, line);
        /* En Tiger, la inyección ENTRADA -> destino declara o asigna la variable */
        TigerStmt* s = tiger_stmt_var_decl(name, false, type_req, in_expr, line);
        s->is_subprocess = is_sub;
        free(name);
        return s;
    }

    /* 3. SI / IF */
    if (match(p, TOK_SI)) {
        TigerExpr* cond = parse_expression(p);
        size_t then_c = 0;
        TigerStmt** then_b = parse_block(p, &then_c);

        size_t else_c = 0;
        TigerStmt** else_b = NULL;
        if (match(p, TOK_SINO)) {
            else_b = parse_block(p, &else_c);
        }
        consume_period_flexible(p);
        return tiger_stmt_if(cond, then_b, then_c, else_b, else_c, line);
    }

    /* 4. MIENTRAS / WHILE / CICLO / LOOP / FOR / PARA */
    if (match(p, TOK_MIENTRAS) || match(p, TOK_CICLO)) {
        TigerExpr* cond = parse_expression(p);
        size_t body_c = 0;
        TigerStmt** body = parse_block(p, &body_c);
        consume_period_flexible(p);
        return tiger_stmt_while(cond, body, body_c, line);
    }

    /* 5. RETORNAR / RETURN */
    if (match(p, TOK_RETORNAR)) {
        TigerExpr* expr = NULL;
        if (!check(p, TOK_DOT) && !check(p, TOK_SEMICOLON) && !check(p, TOK_RBRACE)) {
            expr = parse_expression(p);
        }
        consume_period_flexible(p);
        return tiger_stmt_return(expr, line);
    }

    /* 6. ROMPER / BREAK y CONTINUAR / CONTINUE */
    if (match(p, TOK_ROMPER)) {
        consume_period_flexible(p);
        return tiger_stmt_break(line);
    }
    if (match(p, TOK_CONTINUAR)) {
        consume_period_flexible(p);
        return tiger_stmt_continue(line);
    }

    /* 7. FUNCIONES Y COMANDOS: FN / COMANDO nombre(param1 | param2) { body }. */
    if (match(p, TOK_FN) || match(p, TOK_COMANDO)) {
        consume(p, TOK_IDENT, "Se esperaba el nombre de la función");
        char* name = strdup(p->previous.text);

        consume(p, TOK_LPAREN, "Se esperaba '(' después del nombre de función");
        size_t pcap = 4, pcount = 0;
        TigerParam* params = (TigerParam*)malloc(pcap * sizeof(TigerParam));

        if (!check(p, TOK_RPAREN)) {
            do {
                TigerValType p_type = VAL_NIL;
                if (check(p, TOK_KW_ENT) || check(p, TOK_KW_FLOT) || check(p, TOK_KW_CAD) || check(p, TOK_KW_BOOL) || check(p, TOK_KW_PUNT)) {
                    TigerTokenType pt = p->current.type;
                    if (pt == TOK_KW_ENT) p_type = VAL_INT;
                    else if (pt == TOK_KW_FLOT) p_type = VAL_FLOAT;
                    else if (pt == TOK_KW_CAD) p_type = VAL_STR;
                    else if (pt == TOK_KW_BOOL) p_type = VAL_BOOL;
                    else if (pt == TOK_KW_PUNT) p_type = VAL_PTR;
                    advance_p(p);
                }
                consume(p, TOK_IDENT, "Se esperaba el nombre del parámetro");
                if (pcount >= pcap) {
                    pcap *= 2;
                    params = (TigerParam*)realloc(params, pcap * sizeof(TigerParam));
                }
                params[pcount].name = strdup(p->previous.text);
                params[pcount].type_req = p_type;
                pcount++;
            } while (match(p, TOK_PIPE) || match(p, TOK_COMMA));
        }
        consume(p, TOK_RPAREN, "Se esperaba ')' después de los parámetros");

        size_t body_c = 0;
        TigerStmt** body = parse_block(p, &body_c);
        consume_period_flexible(p);

        TigerStmt* s = tiger_stmt_func(name, params, pcount, body, body_c, VAL_NIL, line);
        free(name);
        return s;
    }

    /* 8. MACROS DE CERO COSTE: MACRO nombre(param) { cuerpo } o MACRO nombre = expr. */
    if (match(p, TOK_MACRO)) {
        consume(p, TOK_IDENT, "Se esperaba el nombre de la macro");
        char* name = strdup(p->previous.text);
        char* param = NULL;
        if (match(p, TOK_LPAREN)) {
            if (match(p, TOK_IDENT)) param = strdup(p->previous.text);
            consume(p, TOK_RPAREN, "Se esperaba ')' tras parámetro de macro");
        }
        TigerExpr* expr = NULL;
        if (match(p, TOK_ASSIGN) || match(p, TOK_ARROW_L)) {
            expr = parse_expression(p);
        } else if (check(p, TOK_LBRACE)) {
            size_t bc = 0;
            TigerStmt** body = parse_block(p, &bc);
            expr = (bc > 0 && body[0]->type == STMT_EXPR) ? body[0]->as.expr : tiger_expr_literal(tiger_val_nil(), line);
        }
        consume_period_flexible(p);
        TigerStmt* s = tiger_stmt_macro(name, param, expr, line);
        free(name);
        if (param) free(param);
        return s;
    }

    /* 9. CLASES Y MOLDES: CLASE/MOLDE Nombre { ... } */
    if (match(p, TOK_CLASE) || match(p, TOK_MOLDE)) {
        consume(p, TOK_IDENT, "Se esperaba el nombre de la clase o molde");
        char* name = strdup(p->previous.text);
        consume(p, TOK_LBRACE, "Se esperaba '{' para iniciar definición");

        size_t fcap = 4, fcount = 0;
        TigerParam* fields = (TigerParam*)malloc(fcap * sizeof(TigerParam));
        TigerExpr** inits = (TigerExpr**)malloc(fcap * sizeof(TigerExpr*));

        size_t mcap = 4, mcount = 0;
        TigerStmt** methods = (TigerStmt**)malloc(mcap * sizeof(TigerStmt*));

        while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            if (match(p, TOK_CRE)) {
                TigerValType ftype = VAL_NIL;
                if (match(p, TOK_KW_ENT)) ftype = VAL_INT;
                else if (match(p, TOK_KW_FLOT)) ftype = VAL_FLOAT;
                else if (match(p, TOK_KW_CAD)) ftype = VAL_STR;
                else if (match(p, TOK_KW_BOOL)) ftype = VAL_BOOL;
                else if (match(p, TOK_KW_PUNT)) ftype = VAL_PTR;

                consume(p, TOK_IDENT, "Nombre del campo");
                char* fname = strdup(p->previous.text);
                TigerExpr* finit = NULL;
                if (match(p, TOK_ASSIGN) || match(p, TOK_ARROW_L)) {
                    finit = parse_expression(p);
                }
                consume_period_flexible(p);

                if (fcount >= fcap) {
                    fcap *= 2;
                    fields = (TigerParam*)realloc(fields, fcap * sizeof(TigerParam));
                    inits = (TigerExpr**)realloc(inits, fcap * sizeof(TigerExpr*));
                }
                fields[fcount].name = fname;
                fields[fcount].type_req = ftype;
                inits[fcount] = finit;
                fcount++;
                continue;
            }
            if (match(p, TOK_FN) || match(p, TOK_METODO) || match(p, TOK_COMANDO)) {
                consume(p, TOK_IDENT, "Nombre del método");
                char* mname = strdup(p->previous.text);
                consume(p, TOK_LPAREN, "Se esperaba '('");
                size_t pcap = 4, pcount = 0;
                TigerParam* params = (TigerParam*)malloc(pcap * sizeof(TigerParam));
                if (!check(p, TOK_RPAREN)) {
                    do {
                        TigerValType p_type = VAL_NIL;
                        if (check(p, TOK_KW_ENT) || check(p, TOK_KW_FLOT) || check(p, TOK_KW_CAD) || check(p, TOK_KW_BOOL) || check(p, TOK_KW_PUNT)) {
                            TigerTokenType pt = p->current.type;
                            if (pt == TOK_KW_ENT) p_type = VAL_INT;
                            else if (pt == TOK_KW_FLOT) p_type = VAL_FLOAT;
                            else if (pt == TOK_KW_CAD) p_type = VAL_STR;
                            else if (pt == TOK_KW_BOOL) p_type = VAL_BOOL;
                            else if (pt == TOK_KW_PUNT) p_type = VAL_PTR;
                            advance_p(p);
                        }
                        consume(p, TOK_IDENT, "Parámetro");
                        if (pcount >= pcap) {
                            pcap *= 2;
                            params = (TigerParam*)realloc(params, pcap * sizeof(TigerParam));
                        }
                        params[pcount].name = strdup(p->previous.text);
                        params[pcount].type_req = p_type;
                        pcount++;
                    } while (match(p, TOK_PIPE) || match(p, TOK_COMMA));
                }
                consume(p, TOK_RPAREN, "Se esperaba ')'");
                size_t body_c = 0;
                TigerStmt** body = parse_block(p, &body_c);
                consume_period_flexible(p);

                if (mcount >= mcap) {
                    mcap *= 2;
                    methods = (TigerStmt**)realloc(methods, mcap * sizeof(TigerStmt*));
                }
                methods[mcount++] = tiger_stmt_func(mname, params, pcount, body, body_c, VAL_NIL, line);
                free(mname);
                continue;
            }
            advance_p(p);
        }
        consume(p, TOK_RBRACE, "Se esperaba '}' de cierre de clase");
        consume_period_flexible(p);

        TigerStmt* s = (TigerStmt*)malloc(sizeof(TigerStmt));
        s->type = STMT_CLASS_DECL;
        s->line = line;
        s->is_subprocess = false;
        s->hash = tiger_hash64(name);
        s->as.class_decl.name = name;
        s->as.class_decl.fields = fields;
        s->as.class_decl.field_inits = inits;
        s->as.class_decl.field_count = fcount;
        s->as.class_decl.methods = methods;
        s->as.class_decl.method_count = mcount;
        return s;
    }

    /* 10. MOLDES E INSTANCIAS DE CLASES SIN PALABRA RESERVADA: Nombre { CRE ... } */
    if (check(p, TOK_IDENT)) {
        TigerLexer look = p->lexer;
        TigerToken tok2 = tiger_lexer_next_token(&look);
        if (tok2.type == TOK_LBRACE) {
            TigerToken tok3 = tiger_lexer_next_token(&look);
            bool is_unreserved_class = (tok3.type == TOK_CRE || tok3.type == TOK_FN || tok3.type == TOK_METODO || tok3.type == TOK_COMANDO);
            tiger_token_free(&tok3);
            tiger_token_free(&tok2);

            if (is_unreserved_class) {
                advance_p(p);
                char* name = strdup(p->previous.text);
                consume(p, TOK_LBRACE, "Se esperaba '{'");

                size_t fcap = 4, fcount = 0;
                TigerParam* fields = (TigerParam*)malloc(fcap * sizeof(TigerParam));
                TigerExpr** inits = (TigerExpr**)malloc(fcap * sizeof(TigerExpr*));
                size_t mcap = 4, mcount = 0;
                TigerStmt** methods = (TigerStmt**)malloc(mcap * sizeof(TigerStmt*));

                while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
                    if (match(p, TOK_CRE)) {
                        TigerValType ftype = VAL_NIL;
                        if (match(p, TOK_KW_ENT)) ftype = VAL_INT;
                        else if (match(p, TOK_KW_FLOT)) ftype = VAL_FLOAT;
                        else if (match(p, TOK_KW_CAD)) ftype = VAL_STR;
                        else if (match(p, TOK_KW_BOOL)) ftype = VAL_BOOL;
                        else if (match(p, TOK_KW_PUNT)) ftype = VAL_PTR;

                        consume(p, TOK_IDENT, "Nombre del campo");
                        char* fname = strdup(p->previous.text);
                        TigerExpr* finit = NULL;
                        if (match(p, TOK_ASSIGN) || match(p, TOK_ARROW_L)) {
                            finit = parse_expression(p);
                        }
                        consume_period_flexible(p);

                        if (fcount >= fcap) {
                            fcap *= 2;
                            fields = (TigerParam*)realloc(fields, fcap * sizeof(TigerParam));
                            inits = (TigerExpr**)realloc(inits, fcap * sizeof(TigerExpr*));
                        }
                        fields[fcount].name = fname;
                        fields[fcount].type_req = ftype;
                        inits[fcount] = finit;
                        fcount++;
                        continue;
                    }
                    if (match(p, TOK_FN) || match(p, TOK_METODO) || match(p, TOK_COMANDO)) {
                        consume(p, TOK_IDENT, "Nombre del método");
                        char* mname = strdup(p->previous.text);
                        consume(p, TOK_LPAREN, "Se esperaba '('");
                        size_t pcap = 4, pcount = 0;
                        TigerParam* params = (TigerParam*)malloc(pcap * sizeof(TigerParam));
                        if (!check(p, TOK_RPAREN)) {
                            do {
                                TigerValType p_type = VAL_NIL;
                                if (check(p, TOK_KW_ENT) || check(p, TOK_KW_FLOT) || check(p, TOK_KW_CAD) || check(p, TOK_KW_BOOL) || check(p, TOK_KW_PUNT)) {
                                    TigerTokenType pt = p->current.type;
                                    if (pt == TOK_KW_ENT) p_type = VAL_INT;
                                    else if (pt == TOK_KW_FLOT) p_type = VAL_FLOAT;
                                    else if (pt == TOK_KW_CAD) p_type = VAL_STR;
                                    else if (pt == TOK_KW_BOOL) p_type = VAL_BOOL;
                                    else if (pt == TOK_KW_PUNT) p_type = VAL_PTR;
                                    advance_p(p);
                                }
                                consume(p, TOK_IDENT, "Parámetro");
                                if (pcount >= pcap) {
                                    pcap *= 2;
                                    params = (TigerParam*)realloc(params, pcap * sizeof(TigerParam));
                                }
                                params[pcount].name = strdup(p->previous.text);
                                params[pcount].type_req = p_type;
                                pcount++;
                            } while (match(p, TOK_PIPE) || match(p, TOK_COMMA));
                        }
                        consume(p, TOK_RPAREN, "Se esperaba ')'");
                        size_t body_c = 0;
                        TigerStmt** body = parse_block(p, &body_c);
                        consume_period_flexible(p);

                        if (mcount >= mcap) {
                            mcap *= 2;
                            methods = (TigerStmt**)realloc(methods, mcap * sizeof(TigerStmt*));
                        }
                        methods[mcount++] = tiger_stmt_func(mname, params, pcount, body, body_c, VAL_NIL, line);
                        free(mname);
                        continue;
                    }
                    advance_p(p);
                }
                consume(p, TOK_RBRACE, "Se esperaba '}'");
                consume_period_flexible(p);

                TigerStmt* s = (TigerStmt*)malloc(sizeof(TigerStmt));
                s->type = STMT_CLASS_DECL;
                s->line = line;
                s->is_subprocess = false;
                s->hash = tiger_hash64(name);
                s->as.class_decl.name = name;
                s->as.class_decl.fields = fields;
                s->as.class_decl.field_inits = inits;
                s->as.class_decl.field_count = fcount;
                s->as.class_decl.methods = methods;
                s->as.class_decl.method_count = mcount;
                return s;
            }
        } else {
            tiger_token_free(&tok2);
        }
    }

    /* 11. COPILOTO MCP / ASISTENTE: ASISTENTE("api" | "model" | "prompt") [-> destino] */
    if (match(p, TOK_ASISTENTE)) {
        char* api_key = NULL;
        char* model = NULL;
        char* prompt = NULL;
        if (match(p, TOK_LPAREN)) {
            if (match(p, TOK_STRING)) api_key = strdup(p->previous.text);
            if (match(p, TOK_PIPE) || match(p, TOK_COMMA)) {
                if (match(p, TOK_STRING)) model = strdup(p->previous.text);
            }
            if (match(p, TOK_PIPE) || match(p, TOK_COMMA)) {
                if (match(p, TOK_STRING)) prompt = strdup(p->previous.text);
            }
            consume(p, TOK_RPAREN, "Se esperaba ')' tras ASISTENTE(...)");
        }
        char* target_var = NULL;
        if (match(p, TOK_ARROW_R)) {
            if (match(p, TOK_IDENT)) target_var = strdup(p->previous.text);
        }
        consume_period_flexible(p);
        TigerStmt* s = tiger_stmt_assistant(api_key, model, prompt, target_var, line);
        if (api_key) free(api_key);
        if (model) free(model);
        if (prompt) free(prompt);
        if (target_var) free(target_var);
        return s;
    }

    /* 8. METALENGUAJE POLÍGLOTA: TIPO <lang> { ... } [-> destino]. */
    if (match(p, TOK_TIPO)) {
        consume(p, TOK_IDENT, "Se esperaba el lenguaje del bloque (PY, C, CPP, JS, HTML, JSON, NOSQL, ASM)");
        char* lang = strdup(p->previous.text);
        char* raw_code = extract_raw_polyglot_block(p);

        char* target_var = NULL;
        if (match(p, TOK_ARROW_R)) {
            if (match(p, TOK_IDENT)) {
                target_var = strdup(p->previous.text);
            }
        }

        consume_period_flexible(p);

        TigerStmt* s = tiger_stmt_polyglot(lang, raw_code, target_var, line);
        free(lang);
        free(raw_code);
        if (target_var) free(target_var);
        return s;
    }

    /* 9. MÓDULOS Y C INTEROP: USAR "archivo.tg". / USAR C "libreria.h". / USAR ASISTENTE / USAR EN ASM. */
    if (match(p, TOK_USAR)) {
        if (match(p, TOK_ASISTENTE)) {
            char* api_key = NULL;
            char* model = NULL;
            char* prompt = NULL;
            if (check(p, TOK_IDENT) && strcasecmp(p->current.text, "API") == 0) {
                advance_p(p);
                if (match(p, TOK_LPAREN)) {
                    if (match(p, TOK_STRING)) api_key = strdup(p->previous.text);
                    if (match(p, TOK_PIPE) || match(p, TOK_COMMA)) {
                        if (match(p, TOK_STRING)) model = strdup(p->previous.text);
                    }
                    if (match(p, TOK_PIPE) || match(p, TOK_COMMA)) {
                        if (match(p, TOK_STRING)) prompt = strdup(p->previous.text);
                    }
                    consume(p, TOK_RPAREN, "Se esperaba ')' tras API(...)");
                }
            }
            consume_period_flexible(p);
            TigerStmt* s = tiger_stmt_assistant(api_key, model, prompt, NULL, line);
            if (api_key) free(api_key);
            if (model) free(model);
            if (prompt) free(prompt);
            return s;
        }

        bool is_c = false;
        if (check(p, TOK_IDENT) && (strcasecmp(p->current.text, "C") == 0 || strcasecmp(p->current.text, "CPP") == 0)) {
            advance_p(p);
            is_c = true;
        } else if (check(p, TOK_IDENT) && strcasecmp(p->current.text, "EN") == 0) {
            advance_p(p); /* Consumir 'EN' */
            /* USAR EN ASM / WAT / C */
            while (!check(p, TOK_DOT) && !check(p, TOK_SEMICOLON) && !check(p, TOK_EOF)) advance_p(p);
            consume_period_flexible(p);
            return NULL; /* Directiva de destino ya registrada */
        }

        if (match(p, TOK_STRING) || match(p, TOK_IDENT)) {
            char* path = strdup(p->previous.text);
            consume_period_flexible(p);
            TigerStmt* s = tiger_stmt_use(path, is_c, line);
            free(path);
            return s;
        }
    }

    /* 10. Asignación: target = expr. o target <- expr. o invocación de función */
    TigerExpr* expr = parse_expression(p);

    if (match(p, TOK_ASSIGN) || match(p, TOK_ARROW_L) ||
        match(p, TOK_PLUS_ASSIGN) || match(p, TOK_MINUS_ASSIGN) ||
        match(p, TOK_STAR_ASSIGN) || match(p, TOK_SLASH_ASSIGN)) {
        TigerTokenType op = p->previous.type;
        TigerExpr* value = parse_expression(p);
        bool is_sub = consume_period_flexible(p);
        TigerStmt* s = tiger_stmt_assign(expr, op, value, line);
        s->is_subprocess = is_sub;
        return s;
    }

    bool is_sub = consume_period_flexible(p);
    TigerStmt* s = tiger_stmt_expr(expr, line);
    s->is_subprocess = is_sub;
    return s;
}

void tiger_parser_init(TigerParser* parser, const char* source, size_t length, bool flexible) {
    tiger_lexer_init(&parser->lexer, source, length);
    parser->current.text = NULL;
    parser->previous.text = NULL;
    parser->has_error = false;
    parser->panic_mode = false;
    parser->flexible = flexible;
    advance_p(parser);
}

TigerStmt** tiger_parse_program(TigerParser* parser, size_t* out_stmt_count) {
    size_t cap = 16;
    size_t count = 0;
    TigerStmt** stmts = (TigerStmt**)malloc(cap * sizeof(TigerStmt*));

    while (!check(parser, TOK_EOF)) {
        TigerStmt* s = parse_statement(parser);
        if (s) {
            if (count >= cap) {
                cap *= 2;
                stmts = (TigerStmt**)realloc(stmts, cap * sizeof(TigerStmt*));
            }
            stmts[count++] = s;
        }
        if (parser->panic_mode) {
            /* Sincronización hasta el próximo punto o fin de línea */
            while (!check(parser, TOK_DOT) && !check(parser, TOK_EOF)) {
                advance_p(parser);
            }
            if (check(parser, TOK_DOT)) advance_p(parser);
            parser->panic_mode = false;
        }
    }

    *out_stmt_count = count;
    return stmts;
}

void tiger_parser_free(TigerParser* parser) {
    tiger_token_free(&parser->current);
    tiger_token_free(&parser->previous);
}
