#include "tiger_lexer.h"

void tiger_lexer_init(TigerLexer* lexer, const char* source, size_t length) {
    lexer->source = source;
    lexer->length = length;
    lexer->cursor = 0;
    lexer->line = 1;
    lexer->column = 1;
}

static inline char peek(TigerLexer* lexer) {
    if (lexer->cursor >= lexer->length) return '\0';
    return lexer->source[lexer->cursor];
}

static inline char peek_next(TigerLexer* lexer) {
    if (lexer->cursor + 1 >= lexer->length) return '\0';
    return lexer->source[lexer->cursor + 1];
}

static inline char advance(TigerLexer* lexer) {
    if (lexer->cursor >= lexer->length) return '\0';
    char c = lexer->source[lexer->cursor++];
    if (c == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }
    return c;
}

static inline bool is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (unsigned char)c >= 0x80;
}

static inline bool is_ident_part(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static TigerToken make_token(TigerLexer* lexer, TigerTokenType type, const char* start, size_t len, int line, int col) {
    TigerToken t;
    t.type = type;
    t.line = line;
    t.column = col;
    t.length = len;
    t.offset = (size_t)(start - lexer->source);
    t.i_val = 0;
    t.text = (char*)malloc(len + 1);
    memcpy(t.text, start, len);
    t.text[len] = '\0';
    return t;
}

void tiger_token_free(TigerToken* tok) {
    if (tok && tok->text) {
        free(tok->text);
        tok->text = NULL;
    }
}

/* Palabras clave válidas en minúsculas (estándar inglés) */
static bool is_english_kw_lower(const char* text, size_t len) {
    static const char* const eng_kws[] = {
        "if", "else", "while", "for", "loop", "break", "continue", "return",
        "var", "let", "const", "fn", "func", "def", "print", "echo",
        "input", "use", "import", "true", "false", "null", "nil",
        "int", "float", "str", "bool", "class", "macro", "type",
        "and", "or", "not", "is", "assistant", NULL
    };
    for (int i = 0; eng_kws[i]; i++) {
        if (strlen(eng_kws[i]) == len && strncmp(text, eng_kws[i], len) == 0) {
            return true;
        }
    }
    return false;
}

/* Comparación inteligente: en español las keywords son MAYÚSCULAS (protege 'entrada', 'y', etc.) */
static bool match_kw(const char* text, size_t len, const char* kw) {
    if (len != strlen(kw)) return false;

    /* 1. Coincidencia exacta */
    if (strncmp(text, kw, len) == 0) return true;

    /* 2. Si el texto del código está en MAYÚSCULAS completas, coincide con la keyword */
    bool all_upper = true;
    for (size_t i = 0; i < len; i++) {
        if (islower((unsigned char)text[i])) {
            all_upper = false;
            break;
        }
    }
    if (all_upper) {
        for (size_t i = 0; i < len; i++) {
            if (toupper((unsigned char)text[i]) != toupper((unsigned char)kw[i])) return false;
        }
        return true;
    }

    /* 3. Si está en minúsculas pero es una keyword estándar en inglés */
    if (is_english_kw_lower(text, len)) {
        for (size_t i = 0; i < len; i++) {
            if (toupper((unsigned char)text[i]) != toupper((unsigned char)kw[i])) return false;
        }
        return true;
    }

    /* En español, palabras en minúsculas como 'entrada', 'salida', 'y', 'o', 'no', 'para', 'tipo' son identificadores */
    return false;
}

/* ========================================================================= */
/* TABLA DE SALTOS SIN CONDICIONALES PARA LAS >30 KEYWORDS BILINGÜES         */
/* ========================================================================= */

typedef struct {
    const char* name;
    size_t len;
    TigerTokenType type;
} TigerJumpEntry;

#define KW_JUMP_TABLE_SIZE 256
#define KW_JUMP_TABLE_MASK (KW_JUMP_TABLE_SIZE - 1)

static TigerJumpEntry g_kw_jump_table[KW_JUMP_TABLE_SIZE];
static bool g_kw_jump_initialized = false;

static inline uint32_t kw_hash(const char* s, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint32_t)(unsigned char)toupper((unsigned char)s[i]);
        h *= 16777619u;
    }
    return h;
}

static void register_kw_jump(const char* name, TigerTokenType type) {
    size_t len = strlen(name);
    uint32_t h = kw_hash(name, len);
    size_t slot = (size_t)(h & KW_JUMP_TABLE_MASK);

    for (size_t i = 0; i < KW_JUMP_TABLE_SIZE; i++) {
        size_t idx = (slot + i) & KW_JUMP_TABLE_MASK;
        if (g_kw_jump_table[idx].name == NULL || strcmp(g_kw_jump_table[idx].name, name) == 0) {
            g_kw_jump_table[idx].name = name;
            g_kw_jump_table[idx].len = len;
            g_kw_jump_table[idx].type = type;
            return;
        }
    }
}

static void init_keyword_jump_table(void) {
    if (g_kw_jump_initialized) return;
    memset(g_kw_jump_table, 0, sizeof(g_kw_jump_table));

    /* 1. Variables y Constantes */
    register_kw_jump("VAR", TOK_VAR);
    register_kw_jump("VARIABLE", TOK_VAR);
    register_kw_jump("LET", TOK_VAR);
    register_kw_jump("CONST", TOK_CONST);
    register_kw_jump("CONSTANTE", TOK_CONST);
    register_kw_jump("CONSTANT", TOK_CONST);

    /* 2. Funciones y Comandos */
    register_kw_jump("FN", TOK_FN);
    register_kw_jump("FUNCTION", TOK_FN);
    register_kw_jump("FUNC", TOK_FN);
    register_kw_jump("DEF", TOK_FN);
    register_kw_jump("FUNCION", TOK_FN);
    register_kw_jump("COMANDO", TOK_COMANDO);
    register_kw_jump("COMMAND", TOK_COMANDO);

    /* 3. Control de Flujo */
    register_kw_jump("SI", TOK_SI);
    register_kw_jump("IF", TOK_SI);
    register_kw_jump("SINO", TOK_SINO);
    register_kw_jump("ELSE", TOK_SINO);
    register_kw_jump("MIENTRAS", TOK_MIENTRAS);
    register_kw_jump("WHILE", TOK_MIENTRAS);
    register_kw_jump("CICLO", TOK_CICLO);
    register_kw_jump("LOOP", TOK_CICLO);
    register_kw_jump("FOR", TOK_CICLO);
    register_kw_jump("PARA", TOK_CICLO);
    register_kw_jump("ROMPER", TOK_ROMPER);
    register_kw_jump("BREAK", TOK_ROMPER);
    register_kw_jump("CONTINUAR", TOK_CONTINUAR);
    register_kw_jump("CONTINUE", TOK_CONTINUAR);
    register_kw_jump("RETORNAR", TOK_RETORNAR);
    register_kw_jump("RETURN", TOK_RETORNAR);
    register_kw_jump("REGRESAR", TOK_RETORNAR);

    /* 4. E/S */
    register_kw_jump("IMPRIMIR", TOK_IMPRIMIR);
    register_kw_jump("PRINT", TOK_IMPRIMIR);
    register_kw_jump("MOSTRAR", TOK_IMPRIMIR);
    register_kw_jump("ECHO", TOK_IMPRIMIR);
    register_kw_jump("ENTRADA", TOK_ENTRADA);
    register_kw_jump("INPUT", TOK_ENTRADA);
    register_kw_jump("LEER", TOK_ENTRADA);

    /* 5. Módulos y Metaprogramación */
    register_kw_jump("USAR", TOK_USAR);
    register_kw_jump("USE", TOK_USAR);
    register_kw_jump("IMPORT", TOK_USAR);
    register_kw_jump("IMPORTAR", TOK_USAR);
    register_kw_jump("TIPO", TOK_TIPO);
    register_kw_jump("TYPE", TOK_TIPO);
    register_kw_jump("MACRO", TOK_MACRO);
    register_kw_jump("BCI", TOK_BCI);
    register_kw_jump("BRAIN", TOK_BCI);
    register_kw_jump("INTERFAZ", TOK_BCI);

    /* 6. Asistente IA / Copiloto MCP */
    register_kw_jump("ASISTENTE", TOK_ASISTENTE);
    register_kw_jump("ASSISTANT", TOK_ASISTENTE);
    register_kw_jump("ASISSTANT", TOK_ASISTENTE);
    register_kw_jump("IA", TOK_ASISTENTE);
    register_kw_jump("AI", TOK_ASISTENTE);
    register_kw_jump("INI", TOK_INI);
    register_kw_jump("INICIO", TOK_INI);
    register_kw_jump("BEGIN", TOK_INI);
    register_kw_jump("START", TOK_INI);
    register_kw_jump("FIN", TOK_FIN);
    register_kw_jump("END", TOK_FIN);

    /* 7. POO (Clases y Moldes) */
    register_kw_jump("CLASE", TOK_CLASE);
    register_kw_jump("CLASS", TOK_CLASE);
    register_kw_jump("OBJETO", TOK_CLASE);
    register_kw_jump("OBJECT", TOK_CLASE);
    register_kw_jump("MOLDE", TOK_MOLDE);
    register_kw_jump("MOLD", TOK_MOLDE);
    register_kw_jump("CRE", TOK_CRE);
    register_kw_jump("CAMPO", TOK_CRE);
    register_kw_jump("FIELD", TOK_CRE);
    register_kw_jump("PROP", TOK_CRE);
    register_kw_jump("PROPERTY", TOK_CRE);
    register_kw_jump("PROPIEDAD", TOK_CRE);
    register_kw_jump("ATTR", TOK_CRE);

    /* 8. Tipos de Datos Bilingües */
    register_kw_jump("ENT", TOK_KW_ENT);
    register_kw_jump("INT", TOK_KW_ENT);
    register_kw_jump("FLOT", TOK_KW_FLOT);
    register_kw_jump("FLOAT", TOK_KW_FLOT);
    register_kw_jump("DEC", TOK_KW_FLOT);
    register_kw_jump("REAL", TOK_KW_FLOT);
    register_kw_jump("CAD", TOK_KW_CAD);
    register_kw_jump("STR", TOK_KW_CAD);
    register_kw_jump("TEXTO", TOK_KW_CAD);
    register_kw_jump("STRING", TOK_KW_CAD);
    register_kw_jump("BOOL", TOK_KW_BOOL);
    register_kw_jump("BOOLEANO", TOK_KW_BOOL);
    register_kw_jump("PUNT", TOK_KW_PUNT);
    register_kw_jump("PUNTC", TOK_KW_PUNT);
    register_kw_jump("POINTC", TOK_KW_PUNT);

    /* 9. Booleanos y Nulo */
    register_kw_jump("VERDADERO", TOK_BOOL_TRUE);
    register_kw_jump("TRUE", TOK_BOOL_TRUE);
    register_kw_jump("CIERTO", TOK_BOOL_TRUE);
    register_kw_jump("FALSO", TOK_BOOL_FALSE);
    register_kw_jump("FALSE", TOK_BOOL_FALSE);
    register_kw_jump("NULO", TOK_NIL);
    register_kw_jump("NIL", TOK_NIL);
    register_kw_jump("NULL", TOK_NIL);

    /* 10. Operadores Lógicos */
    register_kw_jump("Y", TOK_AND);
    register_kw_jump("AND", TOK_AND);
    register_kw_jump("O", TOK_OR);
    register_kw_jump("OR", TOK_OR);
    register_kw_jump("NO", TOK_NOT);
    register_kw_jump("NOT", TOK_NOT);

    g_kw_jump_initialized = true;
}

/* Clasificación bilingüe ultra-rápida sin condicionales mediante tabla de saltos */
static TigerTokenType check_keyword(const char* text, size_t len) {
    if (!g_kw_jump_initialized) init_keyword_jump_table();
    uint32_t h = kw_hash(text, len);
    size_t slot = (size_t)(h & KW_JUMP_TABLE_MASK);

    for (size_t i = 0; i < 8; i++) {
        size_t idx = (slot + i) & KW_JUMP_TABLE_MASK;
        if (g_kw_jump_table[idx].name == NULL) break;
        if (g_kw_jump_table[idx].len == len && match_kw(text, len, g_kw_jump_table[idx].name)) {
            return g_kw_jump_table[idx].type;
        }
    }

    return TOK_IDENT;
}

TigerToken tiger_lexer_next_token(TigerLexer* lexer) {
    while (lexer->cursor < lexer->length) {
        char c = peek(lexer);

        /* Saltar espacios en blanco */
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lexer);
            continue;
        }

        /* Comentarios: # hasta fin de línea */
        if (c == '#') {
            while (lexer->cursor < lexer->length && peek(lexer) != '\n') {
                advance(lexer);
            }
            continue;
        }

        int start_line = lexer->line;
        int start_col = lexer->column;
        const char* start_ptr = &lexer->source[lexer->cursor];

        /* Cadenas de texto: "..." o '...' */
        if (c == '"' || c == '\'') {
            char quote = advance(lexer);
            size_t cap = 32;
            size_t str_len = 0;
            char* buf = (char*)malloc(cap);

            while (lexer->cursor < lexer->length && peek(lexer) != quote) {
                char ch = advance(lexer);
                if (ch == '\\' && lexer->cursor < lexer->length) {
                    char esc = advance(lexer);
                    switch (esc) {
                        case 'n': ch = '\n'; break;
                        case 'r': ch = '\r'; break;
                        case 't': ch = '\t'; break;
                        case '\\': ch = '\\'; break;
                        case '"': ch = '"'; break;
                        case '\'': ch = '\''; break;
                        default: ch = esc; break;
                    }
                }
                if (str_len + 1 >= cap) {
                    cap *= 2;
                    buf = (char*)realloc(buf, cap);
                }
                buf[str_len++] = ch;
            }

            if (peek(lexer) == quote) advance(lexer); /* Consumir cierre */
            buf[str_len] = '\0';

            TigerToken t;
            t.type = TOK_STRING;
            t.text = buf;
            t.length = str_len;
            t.offset = (size_t)(start_ptr - lexer->source);
            t.line = start_line;
            t.column = start_col;
            t.i_val = 0;
            return t;
        }

        /* Números: 0xHex o decimal/flotante con detector que protege decimales */
        if ((c >= '0' && c <= '9') || (c == '.' && isdigit((unsigned char)peek_next(lexer)))) {
            bool is_hex = false;
            bool is_float = (c == '.');

            if (c == '.' && isdigit((unsigned char)peek_next(lexer))) {
                advance(lexer); /* Consumir '.' */
                while (isdigit((unsigned char)peek(lexer))) advance(lexer);
            } else if (c == '0' && (peek_next(lexer) == 'x' || peek_next(lexer) == 'X')) {
                is_hex = true;
                advance(lexer); advance(lexer); /* 0x */
                while (isxdigit((unsigned char)peek(lexer))) advance(lexer);
            } else {
                while (isdigit((unsigned char)peek(lexer))) advance(lexer);
                /* IMPORTANTE: Sólo es flotante si después del '.' viene un dígito (ej: 3.14). */
                /* Protege decimales de confundirse con el terminador de sentencia. */
                if (peek(lexer) == '.' && isdigit((unsigned char)peek_next(lexer))) {
                    is_float = true;
                    advance(lexer); /* Consumir '.' */
                    while (isdigit((unsigned char)peek(lexer))) advance(lexer);
                }
            }

            size_t len = (size_t)(&lexer->source[lexer->cursor] - start_ptr);
            TigerToken t = make_token(lexer, is_float ? TOK_FLOAT : TOK_INT, start_ptr, len, start_line, start_col);
            if (is_hex) {
                t.i_val = (int64_t)strtoll(t.text, NULL, 16);
            } else if (is_float) {
                t.f_val = strtod(t.text, NULL);
            } else {
                t.i_val = (int64_t)strtoll(t.text, NULL, 10);
            }
            return t;
        }

        /* Identificadores y Palabras Clave (con detector inteligente que protege archivos) */
        if (is_ident_start(c)) {
            while (lexer->cursor < lexer->length && is_ident_part(peek(lexer))) {
                advance(lexer);
            }

            /* Detector inteligente de archivos: Protege nombres como archivo.tg, libreria.h */
            if (peek(lexer) == '.') {
                size_t look = lexer->cursor + 1;
                size_t ext_start = look;
                while (look < lexer->length && (isalnum((unsigned char)lexer->source[look]) || lexer->source[look] == '_')) {
                    look++;
                }
                size_t ext_len = look - ext_start;
                if (ext_len >= 1 && ext_len <= 5) {
                    char ext[8];
                    for (size_t k = 0; k < ext_len && k < 7; k++) {
                        ext[k] = (char)tolower((unsigned char)lexer->source[ext_start + k]);
                    }
                    ext[ext_len] = '\0';
                    if (strcmp(ext, "tg") == 0 || strcmp(ext, "h") == 0 || strcmp(ext, "c") == 0 ||
                        strcmp(ext, "py") == 0 || strcmp(ext, "js") == 0 || strcmp(ext, "json") == 0 ||
                        strcmp(ext, "txt") == 0 || strcmp(ext, "html") == 0 || strcmp(ext, "nosql") == 0) {
                        advance(lexer); /* Consumir '.' */
                        while (lexer->cursor < look) advance(lexer);
                    }
                }
            }

            size_t len = (size_t)(&lexer->source[lexer->cursor] - start_ptr);
            TigerTokenType type = check_keyword(start_ptr, len);
            return make_token(lexer, type, start_ptr, len, start_line, start_col);
        }

        /* Operadores de 3 caracteres */
        if (c == '=' && peek_next(lexer) == '=') {
            if (lexer->cursor + 2 < lexer->length && lexer->source[lexer->cursor + 2] == '=') {
                advance(lexer); advance(lexer); advance(lexer);
                return make_token(lexer, TOK_EQ, start_ptr, 3, start_line, start_col);
            }
        }

        /* Operadores de 2 caracteres */
        char c2 = peek_next(lexer);
        if (c == '<' && c2 == '-') {
            advance(lexer); advance(lexer);
            return make_token(lexer, TOK_ARROW_L, start_ptr, 2, start_line, start_col);
        }
        if (c == '-' && c2 == '>') {
            advance(lexer); advance(lexer);
            return make_token(lexer, TOK_ARROW_R, start_ptr, 2, start_line, start_col);
        }
        if (c == '+' && c2 == '=') {
            advance(lexer); advance(lexer);
            return make_token(lexer, TOK_PLUS_ASSIGN, start_ptr, 2, start_line, start_col);
        }
        if (c == '-' && c2 == '=') {
            advance(lexer); advance(lexer);
            return make_token(lexer, TOK_MINUS_ASSIGN, start_ptr, 2, start_line, start_col);
        }
        if (c == '*' && c2 == '=') {
            advance(lexer); advance(lexer);
            return make_token(lexer, TOK_STAR_ASSIGN, start_ptr, 2, start_line, start_col);
        }
        if (c == '/' && c2 == '=') {
            advance(lexer); advance(lexer);
            return make_token(lexer, TOK_SLASH_ASSIGN, start_ptr, 2, start_line, start_col);
        }
        if (c == '=' && c2 == '=') {
            advance(lexer); advance(lexer);
            return make_token(lexer, TOK_EQ, start_ptr, 2, start_line, start_col);
        }
        if (c == '!' && c2 == '=') {
            advance(lexer); advance(lexer);
            return make_token(lexer, TOK_NEQ, start_ptr, 2, start_line, start_col);
        }
        if (c == '<' && c2 == '=') {
            advance(lexer); advance(lexer);
            return make_token(lexer, TOK_LE, start_ptr, 2, start_line, start_col);
        }
        if (c == '>' && c2 == '=') {
            advance(lexer); advance(lexer);
            return make_token(lexer, TOK_GE, start_ptr, 2, start_line, start_col);
        }
        if (c == '&' && c2 == '&') {
            advance(lexer); advance(lexer);
            return make_token(lexer, TOK_AND, start_ptr, 2, start_line, start_col);
        }
        if (c == '|' && c2 == '|') {
            advance(lexer); advance(lexer);
            return make_token(lexer, TOK_OR, start_ptr, 2, start_line, start_col);
        }

        /* Operadores de 1 caracter */
        advance(lexer);
        switch (c) {
            case '.': return make_token(lexer, TOK_DOT, start_ptr, 1, start_line, start_col);
            case '|': return make_token(lexer, TOK_PIPE, start_ptr, 1, start_line, start_col);
            case '=': return make_token(lexer, TOK_ASSIGN, start_ptr, 1, start_line, start_col);
            case '+': return make_token(lexer, TOK_PLUS, start_ptr, 1, start_line, start_col);
            case '-': return make_token(lexer, TOK_MINUS, start_ptr, 1, start_line, start_col);
            case '*': return make_token(lexer, TOK_STAR, start_ptr, 1, start_line, start_col);
            case '/': return make_token(lexer, TOK_SLASH, start_ptr, 1, start_line, start_col);
            case '%': return make_token(lexer, TOK_PERCENT, start_ptr, 1, start_line, start_col);
            case '<': return make_token(lexer, TOK_LT, start_ptr, 1, start_line, start_col);
            case '>': return make_token(lexer, TOK_GT, start_ptr, 1, start_line, start_col);
            case '!': return make_token(lexer, TOK_NOT, start_ptr, 1, start_line, start_col);
            case '(': return make_token(lexer, TOK_LPAREN, start_ptr, 1, start_line, start_col);
            case ')': return make_token(lexer, TOK_RPAREN, start_ptr, 1, start_line, start_col);
            case '{': return make_token(lexer, TOK_LBRACE, start_ptr, 1, start_line, start_col);
            case '}': return make_token(lexer, TOK_RBRACE, start_ptr, 1, start_line, start_col);
            case '[': return make_token(lexer, TOK_LBRACKET, start_ptr, 1, start_line, start_col);
            case ']': return make_token(lexer, TOK_RBRACKET, start_ptr, 1, start_line, start_col);
            case ',': return make_token(lexer, TOK_COMMA, start_ptr, 1, start_line, start_col);
            case ':': return make_token(lexer, TOK_COLON, start_ptr, 1, start_line, start_col);
            case ';': return make_token(lexer, TOK_SEMICOLON, start_ptr, 1, start_line, start_col);
            default:
                return make_token(lexer, TOK_ERROR, start_ptr, 1, start_line, start_col);
        }
    }

    TigerToken eof_tok;
    eof_tok.type = TOK_EOF;
    eof_tok.text = strdup("");
    eof_tok.length = 0;
    eof_tok.offset = lexer->cursor;
    eof_tok.line = lexer->line;
    eof_tok.column = lexer->column;
    eof_tok.i_val = 0;
    return eof_tok;
}

const char* tiger_token_type_name(TigerTokenType type) {
    switch (type) {
        case TOK_EOF: return "FIN_DE_ARCHIVO";
        case TOK_INT: return "ENTERO";
        case TOK_FLOAT: return "FLOTANTE";
        case TOK_STRING: return "CADENA";
        case TOK_IDENT: return "IDENTIFICADOR";
        case TOK_VAR: return "VAR";
        case TOK_CONST: return "CONST";
        case TOK_FN: return "FN";
        case TOK_COMANDO: return "COMANDO";
        case TOK_SI: return "SI";
        case TOK_SINO: return "SINO";
        case TOK_MIENTRAS: return "MIENTRAS";
        case TOK_CICLO: return "CICLO";
        case TOK_IMPRIMIR: return "IMPRIMIR";
        case TOK_ENTRADA: return "ENTRADA";
        case TOK_USAR: return "USAR";
        case TOK_CLASE: return "CLASE";
        case TOK_MOLDE: return "MOLDE";
        case TOK_MACRO: return "MACRO";
        case TOK_BCI: return "BCI";
        case TOK_ASISTENTE: return "ASISTENTE";
        case TOK_DOT: return ".";
        case TOK_PIPE: return "|";
        case TOK_ARROW_L: return "<-";
        default: return "TOKEN";
    }
}
