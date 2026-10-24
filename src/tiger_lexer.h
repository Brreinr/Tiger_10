#ifndef TIGER_LEXER_H
#define TIGER_LEXER_H

#include "tiger.h"

typedef struct {
    const char* source;
    size_t length;
    size_t cursor;
    int line;
    int column;
} TigerLexer;

void tiger_lexer_init(TigerLexer* lexer, const char* source, size_t length);
TigerToken tiger_lexer_next_token(TigerLexer* lexer);
void tiger_token_free(TigerToken* tok);
const char* tiger_token_type_name(TigerTokenType type);

#endif /* TIGER_LEXER_H */
