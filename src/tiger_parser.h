#ifndef TIGER_PARSER_H
#define TIGER_PARSER_H

#include "tiger.h"
#include "tiger_ast.h"
#include "tiger_lexer.h"

typedef struct {
    TigerLexer lexer;
    TigerToken current;
    TigerToken previous;
    bool has_error;
    bool panic_mode;
    bool flexible;
} TigerParser;

void tiger_parser_init(TigerParser* parser, const char* source, size_t length, bool flexible);
TigerStmt** tiger_parse_program(TigerParser* parser, size_t* out_stmt_count);
void tiger_parser_free(TigerParser* parser);

#endif /* TIGER_PARSER_H */
