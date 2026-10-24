#include "tiger_ast.h"

TigerExpr* tiger_expr_literal(TigerValue v, int line) {
    TigerExpr* e = (TigerExpr*)malloc(sizeof(TigerExpr));
    e->type = EXPR_LITERAL;
    e->line = line;
    e->hash = 0;
    e->as.literal_val = tiger_val_copy(v);
    return e;
}

TigerExpr* tiger_expr_var(const char* name, int line) {
    TigerExpr* e = (TigerExpr*)malloc(sizeof(TigerExpr));
    e->type = EXPR_VAR;
    e->line = line;
    e->hash = tiger_hash64(name);
    e->as.var_name = strdup(name);
    return e;
}

TigerExpr* tiger_expr_unary(TigerTokenType op, TigerExpr* right, int line) {
    /* Plegado de constantes unarias en tiempo de compilación (0 ciclos CPU en ejecución) */
    if (right && right->type == EXPR_LITERAL) {
        if (op == TOK_MINUS) {
            if (right->as.literal_val.type == VAL_INT) {
                right->as.literal_val.as.i_val = -right->as.literal_val.as.i_val;
                return right;
            } else if (right->as.literal_val.type == VAL_FLOAT) {
                right->as.literal_val.as.f_val = -right->as.literal_val.as.f_val;
                return right;
            }
        } else if (op == TOK_NOT) {
            bool b = !tiger_val_is_truthy(right->as.literal_val);
            tiger_expr_free(right);
            return tiger_expr_literal(tiger_val_bool(b), line);
        }
    }

    TigerExpr* e = (TigerExpr*)malloc(sizeof(TigerExpr));
    e->type = EXPR_UNARY;
    e->line = line;
    e->hash = 0;
    e->as.unary.op = op;
    e->as.unary.right = right;
    return e;
}

TigerExpr* tiger_expr_binary(TigerTokenType op, TigerExpr* left, TigerExpr* right, int line) {
    /* Plegado de constantes binarias en tiempo de compilación (Constant Folding) */
    if (left && right && left->type == EXPR_LITERAL && right->type == EXPR_LITERAL) {
        TigerValue lv = left->as.literal_val;
        TigerValue rv = right->as.literal_val;

        /* Plegado de enteros de 64 bits */
        if (lv.type == VAL_INT && rv.type == VAL_INT) {
            int64_t a = lv.as.i_val;
            int64_t b = rv.as.i_val;
            bool folded = true;
            TigerValue res;

            switch (op) {
                case TOK_PLUS: res = tiger_val_int(a + b); break;
                case TOK_MINUS: res = tiger_val_int(a - b); break;
                case TOK_STAR: res = tiger_val_int(a * b); break;
                case TOK_SLASH:
                    if (b != 0) res = tiger_val_int(a / b);
                    else folded = false;
                    break;
                case TOK_PERCENT:
                    if (b != 0) res = tiger_val_int(a % b);
                    else folded = false;
                    break;
                case TOK_EQ: res = tiger_val_bool(a == b); break;
                case TOK_NEQ: res = tiger_val_bool(a != b); break;
                case TOK_LT: res = tiger_val_bool(a < b); break;
                case TOK_LE: res = tiger_val_bool(a <= b); break;
                case TOK_GT: res = tiger_val_bool(a > b); break;
                case TOK_GE: res = tiger_val_bool(a >= b); break;
                default: folded = false; break;
            }

            if (folded) {
                tiger_expr_free(left);
                tiger_expr_free(right);
                return tiger_expr_literal(res, line);
            }
        }

        /* Plegado de números reales / flotantes */
        if ((lv.type == VAL_FLOAT || lv.type == VAL_INT) &&
            (rv.type == VAL_FLOAT || rv.type == VAL_INT) &&
            (lv.type == VAL_FLOAT || rv.type == VAL_FLOAT)) {
            double a = (lv.type == VAL_FLOAT) ? lv.as.f_val : (double)lv.as.i_val;
            double b = (rv.type == VAL_FLOAT) ? rv.as.f_val : (double)rv.as.i_val;
            bool folded = true;
            TigerValue res;

            switch (op) {
                case TOK_PLUS: res = tiger_val_float(a + b); break;
                case TOK_MINUS: res = tiger_val_float(a - b); break;
                case TOK_STAR: res = tiger_val_float(a * b); break;
                case TOK_SLASH:
                    if (b != 0.0) res = tiger_val_float(a / b);
                    else folded = false;
                    break;
                case TOK_EQ: res = tiger_val_bool(a == b); break;
                case TOK_NEQ: res = tiger_val_bool(a != b); break;
                case TOK_LT: res = tiger_val_bool(a < b); break;
                case TOK_LE: res = tiger_val_bool(a <= b); break;
                case TOK_GT: res = tiger_val_bool(a > b); break;
                case TOK_GE: res = tiger_val_bool(a >= b); break;
                default: folded = false; break;
            }

            if (folded) {
                tiger_expr_free(left);
                tiger_expr_free(right);
                return tiger_expr_literal(res, line);
            }
        }

        /* Plegado de cadenas de texto (concatenación estática inmediata) */
        if (lv.type == VAL_STR && rv.type == VAL_STR) {
            if (op == TOK_PLUS) {
                size_t l1 = strlen(lv.as.s_val);
                size_t l2 = strlen(rv.as.s_val);
                char* joined = (char*)malloc(l1 + l2 + 1);
                memcpy(joined, lv.as.s_val, l1);
                memcpy(joined + l1, rv.as.s_val, l2 + 1);
                TigerValue res = tiger_val_str_take(joined);
                tiger_expr_free(left);
                tiger_expr_free(right);
                return tiger_expr_literal(res, line);
            } else if (op == TOK_EQ || op == TOK_NEQ) {
                bool eq = (strcmp(lv.as.s_val, rv.as.s_val) == 0);
                TigerValue res = tiger_val_bool(op == TOK_EQ ? eq : !eq);
                tiger_expr_free(left);
                tiger_expr_free(right);
                return tiger_expr_literal(res, line);
            }
        }

        /* Plegado de booleanos */
        if (lv.type == VAL_BOOL && rv.type == VAL_BOOL) {
            bool a = lv.as.b_val;
            bool b = rv.as.b_val;
            bool folded = true;
            TigerValue res;

            switch (op) {
                case TOK_AND: res = tiger_val_bool(a && b); break;
                case TOK_OR: res = tiger_val_bool(a || b); break;
                case TOK_EQ: res = tiger_val_bool(a == b); break;
                case TOK_NEQ: res = tiger_val_bool(a != b); break;
                default: folded = false; break;
            }

            if (folded) {
                tiger_expr_free(left);
                tiger_expr_free(right);
                return tiger_expr_literal(res, line);
            }
        }
    }

    TigerExpr* e = (TigerExpr*)malloc(sizeof(TigerExpr));
    e->type = EXPR_BINARY;
    e->line = line;
    e->hash = 0;
    e->as.binary.op = op;
    e->as.binary.left = left;
    e->as.binary.right = right;
    return e;
}

TigerExpr* tiger_expr_call(TigerExpr* callee, TigerExpr** args, size_t argc, int line) {
    TigerExpr* e = (TigerExpr*)malloc(sizeof(TigerExpr));
    e->type = EXPR_CALL;
    e->line = line;
    e->hash = 0;
    e->as.call.callee = callee;
    e->as.call.args = args;
    e->as.call.arg_count = argc;
    return e;
}

TigerExpr* tiger_expr_member(TigerExpr* target, const char* member, int line) {
    TigerExpr* e = (TigerExpr*)malloc(sizeof(TigerExpr));
    e->type = EXPR_MEMBER;
    e->line = line;
    e->hash = tiger_hash64(member);
    e->as.member.target = target;
    e->as.member.member_name = strdup(member);
    return e;
}

TigerExpr* tiger_expr_index(TigerExpr* target, TigerExpr* idx, int line) {
    TigerExpr* e = (TigerExpr*)malloc(sizeof(TigerExpr));
    e->type = EXPR_INDEX;
    e->line = line;
    e->hash = 0;
    e->as.index.target = target;
    e->as.index.index = idx;
    return e;
}

TigerExpr* tiger_expr_input(TigerExpr* prompt, int line) {
    TigerExpr* e = (TigerExpr*)malloc(sizeof(TigerExpr));
    e->type = EXPR_INPUT;
    e->line = line;
    e->hash = 0;
    e->as.input.prompt = prompt;
    return e;
}

void tiger_expr_free(TigerExpr* expr) {
    if (!expr) return;
    switch (expr->type) {
        case EXPR_LITERAL:
            tiger_val_free(&expr->as.literal_val);
            break;
        case EXPR_VAR:
            free(expr->as.var_name);
            break;
        case EXPR_UNARY:
            tiger_expr_free(expr->as.unary.right);
            break;
        case EXPR_BINARY:
            tiger_expr_free(expr->as.binary.left);
            tiger_expr_free(expr->as.binary.right);
            break;
        case EXPR_CALL:
            tiger_expr_free(expr->as.call.callee);
            for (size_t i = 0; i < expr->as.call.arg_count; i++) {
                tiger_expr_free(expr->as.call.args[i]);
            }
            if (expr->as.call.args) free(expr->as.call.args);
            break;
        case EXPR_MEMBER:
            tiger_expr_free(expr->as.member.target);
            free(expr->as.member.member_name);
            break;
        case EXPR_INDEX:
            tiger_expr_free(expr->as.index.target);
            tiger_expr_free(expr->as.index.index);
            break;
        case EXPR_INPUT:
            if (expr->as.input.prompt) tiger_expr_free(expr->as.input.prompt);
            break;
        default:
            break;
    }
    free(expr);
}

static TigerStmt* alloc_stmt(StmtType type, int line) {
    TigerStmt* s = (TigerStmt*)malloc(sizeof(TigerStmt));
    s->type = type;
    s->line = line;
    s->is_subprocess = false;
    s->hash = 0;
    return s;
}

TigerStmt* tiger_stmt_expr(TigerExpr* expr, int line) {
    TigerStmt* s = alloc_stmt(STMT_EXPR, line);
    s->as.expr = expr;
    return s;
}

TigerStmt* tiger_stmt_var_decl(const char* name, bool is_const, TigerValType type_req, TigerExpr* init, int line) {
    TigerStmt* s = alloc_stmt(STMT_VAR_DECL, line);
    s->hash = tiger_hash64(name);
    s->as.var_decl.name = strdup(name);
    s->as.var_decl.is_const = is_const;
    s->as.var_decl.type_req = type_req;
    s->as.var_decl.init = init;
    return s;
}

TigerStmt* tiger_stmt_assign(TigerExpr* target, TigerTokenType op, TigerExpr* value, int line) {
    TigerStmt* s = alloc_stmt(STMT_ASSIGN, line);
    s->as.assign.target = target;
    s->as.assign.op = op;
    s->as.assign.value = value;
    return s;
}

TigerStmt* tiger_stmt_print(TigerExpr* expr, bool newline, int line) {
    TigerStmt* s = alloc_stmt(STMT_PRINT, line);
    s->as.print.expr = expr;
    s->as.print.newline = newline;
    return s;
}

TigerStmt* tiger_stmt_if(TigerExpr* cond, TigerStmt** then_b, size_t then_c, TigerStmt** else_b, size_t else_c, int line) {
    TigerStmt* s = alloc_stmt(STMT_IF, line);
    s->as.if_stmt.condition = cond;
    s->as.if_stmt.then_stmts = then_b;
    s->as.if_stmt.then_count = then_c;
    s->as.if_stmt.else_stmts = else_b;
    s->as.if_stmt.else_count = else_c;
    return s;
}

TigerStmt* tiger_stmt_while(TigerExpr* cond, TigerStmt** body, size_t count, int line) {
    TigerStmt* s = alloc_stmt(STMT_WHILE, line);
    s->as.while_stmt.condition = cond;
    s->as.while_stmt.body = body;
    s->as.while_stmt.body_count = count;
    return s;
}

TigerStmt* tiger_stmt_return(TigerExpr* expr, int line) {
    TigerStmt* s = alloc_stmt(STMT_RETURN, line);
    s->as.return_stmt.expr = expr;
    return s;
}

TigerStmt* tiger_stmt_break(int line) {
    return alloc_stmt(STMT_BREAK, line);
}

TigerStmt* tiger_stmt_continue(int line) {
    return alloc_stmt(STMT_CONTINUE, line);
}

TigerStmt* tiger_stmt_func(const char* name, TigerParam* params, size_t param_c, TigerStmt** body, size_t body_c, TigerValType ret_t, int line) {
    TigerStmt* s = alloc_stmt(STMT_FUNC_DECL, line);
    s->as.func_decl.name = strdup(name);
    s->as.func_decl.params = params;
    s->as.func_decl.param_count = param_c;
    s->as.func_decl.body = body;
    s->as.func_decl.body_count = body_c;
    s->as.func_decl.return_type = ret_t;
    return s;
}

TigerStmt* tiger_stmt_polyglot(const char* lang, const char* code, const char* target_var, int line) {
    TigerStmt* s = alloc_stmt(STMT_POLYGLOT, line);
    s->as.polyglot.lang = strdup(lang);
    s->as.polyglot.code = strdup(code);
    s->as.polyglot.target_var = target_var ? strdup(target_var) : NULL;
    return s;
}

TigerStmt* tiger_stmt_use(const char* path, bool is_c, int line) {
    TigerStmt* s = alloc_stmt(STMT_USE, line);
    s->as.use_stmt.module_path = strdup(path);
    s->as.use_stmt.is_c_header = is_c;
    return s;
}

TigerStmt* tiger_stmt_macro(const char* name, const char* param, TigerExpr* body_expr, int line) {
    TigerStmt* s = alloc_stmt(STMT_MACRO, line);
    s->as.macro_decl.name = strdup(name);
    s->as.macro_decl.param_name = param ? strdup(param) : NULL;
    s->as.macro_decl.body_expr = body_expr;
    return s;
}

TigerStmt* tiger_stmt_assistant(const char* api_key, const char* model, const char* prompt, const char* target_var, int line) {
    TigerStmt* s = alloc_stmt(STMT_ASSISTANT, line);
    s->as.assistant.api_key = api_key ? strdup(api_key) : NULL;
    s->as.assistant.model = model ? strdup(model) : NULL;
    s->as.assistant.prompt = prompt ? strdup(prompt) : NULL;
    s->as.assistant.target_var = target_var ? strdup(target_var) : NULL;
    return s;
}

void tiger_stmt_free(TigerStmt* stmt) {
    if (!stmt) return;
    switch (stmt->type) {
        case STMT_EXPR:
            tiger_expr_free(stmt->as.expr);
            break;
        case STMT_VAR_DECL:
            free(stmt->as.var_decl.name);
            tiger_expr_free(stmt->as.var_decl.init);
            break;
        case STMT_ASSIGN:
            tiger_expr_free(stmt->as.assign.target);
            tiger_expr_free(stmt->as.assign.value);
            break;
        case STMT_PRINT:
            tiger_expr_free(stmt->as.print.expr);
            break;
        case STMT_IF:
            tiger_expr_free(stmt->as.if_stmt.condition);
            for (size_t i = 0; i < stmt->as.if_stmt.then_count; i++) tiger_stmt_free(stmt->as.if_stmt.then_stmts[i]);
            if (stmt->as.if_stmt.then_stmts) free(stmt->as.if_stmt.then_stmts);
            for (size_t i = 0; i < stmt->as.if_stmt.else_count; i++) tiger_stmt_free(stmt->as.if_stmt.else_stmts[i]);
            if (stmt->as.if_stmt.else_stmts) free(stmt->as.if_stmt.else_stmts);
            break;
        case STMT_WHILE:
            tiger_expr_free(stmt->as.while_stmt.condition);
            for (size_t i = 0; i < stmt->as.while_stmt.body_count; i++) tiger_stmt_free(stmt->as.while_stmt.body[i]);
            if (stmt->as.while_stmt.body) free(stmt->as.while_stmt.body);
            break;
        case STMT_RETURN:
            tiger_expr_free(stmt->as.return_stmt.expr);
            break;
        case STMT_FUNC_DECL:
            free(stmt->as.func_decl.name);
            for (size_t i = 0; i < stmt->as.func_decl.param_count; i++) free(stmt->as.func_decl.params[i].name);
            if (stmt->as.func_decl.params) free(stmt->as.func_decl.params);
            for (size_t i = 0; i < stmt->as.func_decl.body_count; i++) tiger_stmt_free(stmt->as.func_decl.body[i]);
            if (stmt->as.func_decl.body) free(stmt->as.func_decl.body);
            break;
        case STMT_POLYGLOT:
            free(stmt->as.polyglot.lang);
            free(stmt->as.polyglot.code);
            if (stmt->as.polyglot.target_var) free(stmt->as.polyglot.target_var);
            break;
        case STMT_USE:
            free(stmt->as.use_stmt.module_path);
            break;
        case STMT_MACRO:
            free(stmt->as.macro_decl.name);
            if (stmt->as.macro_decl.param_name) free(stmt->as.macro_decl.param_name);
            tiger_expr_free(stmt->as.macro_decl.body_expr);
            break;
        case STMT_ASSISTANT:
            if (stmt->as.assistant.api_key) free(stmt->as.assistant.api_key);
            if (stmt->as.assistant.model) free(stmt->as.assistant.model);
            if (stmt->as.assistant.prompt) free(stmt->as.assistant.prompt);
            if (stmt->as.assistant.target_var) free(stmt->as.assistant.target_var);
            break;
        default:
            break;
    }
    free(stmt);
}
