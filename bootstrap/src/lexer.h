#ifndef CPM_LEXER_H
#define CPM_LEXER_H

#include "../diag.h"
#include "../token.h"

typedef struct {
    Source src;
    Arena *a;     /* string literals + error text are allocated here */
    size_t pos;   /* current byte offset */
    size_t line;  /* current 1-based line */
    size_t col;   /* current 1-based byte column */
} Lexer;

void lex_init(Lexer *lx, Source src, Arena *a);

/* Produce the next token. On malformed input returns TT_ERROR and records a
 * diagnostic (scanner continues). */
Token lex_next(Lexer *lx, Diag *d);

#endif /* CPM_LEXER_H */