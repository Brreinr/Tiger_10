#ifndef TIGER_AST_H
#define TIGER_AST_H

#include "tiger.h"

typedef enum {
    EXPR_LITERAL,
    EXPR_VAR,
    EXPR_UNARY,
    EXPR_BINARY,
    EXPR_CALL,
    EXPR_MEMBER,
    EXPR_INDEX,
    EXPR_ARRAY_LIT,
    EXPR_DICT_LIT,
    EXPR_INPUT
} ExprType;

typedef struct TigerExpr TigerExpr;

struct TigerExpr {
    ExprType type;
    int line;
    uint64_t hash; /* Hash FNV-1a de 64 bits precalculado para acceso directo al silicio */
    union {
        TigerValue literal_val;
        char* var_name;
        struct {
            TigerTokenType op;
            TigerExpr* right;
        } unary;
        struct {
            TigerTokenType op;
            TigerExpr* left;
            TigerExpr* right;
        } binary;
        struct {
            TigerExpr* callee;
            TigerExpr** args;
            size_t arg_count;
        } call;
        struct {
            TigerExpr* target;
            char* member_name;
        } member;
        struct {
            TigerExpr* target;
            TigerExpr* index;
        } index;
        struct {
            TigerExpr** elements;
            size_t count;
        } array_lit;
        struct {
            char** keys;
            TigerExpr** values;
            size_t count;
        } dict_lit;
        struct {
            TigerExpr* prompt; /* Opcional: mensaje de entrada por teclado */
        } input;
    } as;
};

typedef enum {
    STMT_EXPR,
    STMT_VAR_DECL,
    STMT_ASSIGN,
    STMT_PRINT,
    STMT_IF,
    STMT_WHILE,
    STMT_RETURN,
    STMT_BREAK,
    STMT_CONTINUE,
    STMT_FUNC_DECL,
    STMT_CLASS_DECL,
    STMT_USE,
    STMT_POLYGLOT,
    STMT_BLOCK,
    STMT_MACRO,
    STMT_ASSISTANT
} StmtType;

typedef struct TigerStmt TigerStmt;

/* Parámetro de función */
typedef struct {
    char* name;
    TigerValType type_req;
} TigerParam;

/* Definición de clase */
typedef struct {
    char* name;
    TigerParam* fields;
    TigerExpr** field_inits;
    size_t field_count;
    TigerStmt** methods;
    size_t method_count;
} TigerClassDef;

struct TigerStmt {
    StmtType type;
    int line;
    bool is_subprocess; /* true si es subproceso terminado en coma ',', false si es proceso en punto '.' */
    uint64_t hash;      /* Hash FNV-1a de 64 bits para declaraciones y asignaciones directas */
    union {
        TigerExpr* expr;
        struct {
            char* name;
            bool is_const;
            TigerValType type_req;
            TigerExpr* init;
        } var_decl;
        struct {
            TigerExpr* target; /* EXPR_VAR, EXPR_MEMBER, EXPR_INDEX */
            TigerTokenType op; /* TOK_ASSIGN, TOK_ARROW_L, TOK_PLUS_ASSIGN, etc. */
            TigerExpr* value;
        } assign;
        struct {
            TigerExpr* expr;
            bool newline;
        } print;
        struct {
            TigerExpr* condition;
            TigerStmt** then_stmts;
            size_t then_count;
            TigerStmt** else_stmts;
            size_t else_count;
        } if_stmt;
        struct {
            TigerExpr* condition;
            TigerStmt** body;
            size_t body_count;
        } while_stmt;
        struct {
            TigerExpr* expr;
        } return_stmt;
        struct {
            char* name;
            TigerParam* params;
            size_t param_count;
            TigerStmt** body;
            size_t body_count;
            TigerValType return_type;
        } func_decl;
        TigerClassDef class_decl;
        struct {
            char* module_path;
            bool is_c_header; /* USAR C "..." */
        } use_stmt;
        struct {
            char* lang; /* "C", "CPP", "PY", "JS", "HTML", "JSON", "NOSQL" */
            char* code;
            char* target_var; /* Redirección opcional -> var */
        } polyglot;
        struct {
            TigerStmt** stmts;
            size_t count;
        } block;
        struct {
            char* name;
            char* param_name;
            TigerExpr* body_expr;
        } macro_decl;
        struct {
            char* api_key;
            char* model;
            char* prompt;
            char* target_var;
        } assistant;
    } as;
};

/* Constructores de AST */
TigerExpr* tiger_expr_literal(TigerValue v, int line);
TigerExpr* tiger_expr_var(const char* name, int line);
TigerExpr* tiger_expr_unary(TigerTokenType op, TigerExpr* right, int line);
TigerExpr* tiger_expr_binary(TigerTokenType op, TigerExpr* left, TigerExpr* right, int line);
TigerExpr* tiger_expr_call(TigerExpr* callee, TigerExpr** args, size_t argc, int line);
TigerExpr* tiger_expr_member(TigerExpr* target, const char* member, int line);
TigerExpr* tiger_expr_index(TigerExpr* target, TigerExpr* idx, int line);
TigerExpr* tiger_expr_input(TigerExpr* prompt, int line);
void tiger_expr_free(TigerExpr* expr);

TigerStmt* tiger_stmt_expr(TigerExpr* expr, int line);
TigerStmt* tiger_stmt_var_decl(const char* name, bool is_const, TigerValType type_req, TigerExpr* init, int line);
TigerStmt* tiger_stmt_assign(TigerExpr* target, TigerTokenType op, TigerExpr* value, int line);
TigerStmt* tiger_stmt_print(TigerExpr* expr, bool newline, int line);
TigerStmt* tiger_stmt_if(TigerExpr* cond, TigerStmt** then_b, size_t then_c, TigerStmt** else_b, size_t else_c, int line);
TigerStmt* tiger_stmt_while(TigerExpr* cond, TigerStmt** body, size_t count, int line);
TigerStmt* tiger_stmt_return(TigerExpr* expr, int line);
TigerStmt* tiger_stmt_break(int line);
TigerStmt* tiger_stmt_continue(int line);
TigerStmt* tiger_stmt_func(const char* name, TigerParam* params, size_t param_c, TigerStmt** body, size_t body_c, TigerValType ret_t, int line);
TigerStmt* tiger_stmt_polyglot(const char* lang, const char* code, const char* target_var, int line);
TigerStmt* tiger_stmt_use(const char* path, bool is_c, int line);
TigerStmt* tiger_stmt_macro(const char* name, const char* param, TigerExpr* body_expr, int line);
TigerStmt* tiger_stmt_assistant(const char* api_key, const char* model, const char* prompt, const char* target_var, int line);
void tiger_stmt_free(TigerStmt* stmt);

#endif /* TIGER_AST_H */
