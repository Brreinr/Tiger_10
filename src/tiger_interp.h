#ifndef TIGER_INTERP_H
#define TIGER_INTERP_H

#include "tiger.h"
#include "tiger_ast.h"

typedef enum {
    EXEC_OK,
    EXEC_RETURN,
    EXEC_BREAK,
    EXEC_CONTINUE,
    EXEC_ERROR
} ExecResult;

typedef struct {
    ExecResult status;
    TigerValue return_val;
} ExecSignal;

TigerValue tiger_eval_expr(TigerEnv* env, TigerExpr* expr);
ExecSignal tiger_exec_stmt(TigerEnv* env, TigerStmt* stmt);
ExecSignal tiger_exec_block(TigerEnv* env, TigerStmt** stmts, size_t count);
bool tiger_run_source(const char* source, size_t length, TigerEnv* env, bool flexible);
bool tiger_run_file(const char* filepath, TigerEnv* env, bool flexible);

#endif /* TIGER_INTERP_H */

