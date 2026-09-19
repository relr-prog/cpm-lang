#ifndef CPM_TOKEN_H
#define CPM_TOKEN_H

#include "util/util.h"

typedef enum {
    TT_EOF = 0,

    TT_IDENT,
    TT_INT,   /* decimal integer literal */
    TT_FLOAT, /* float literal */
    TT_STRING, /* "(escaped) string literal, no raw newline */

    TT_FN, TT_VAR, TT_RETURN, TT_IF, TT_ELSE, TT_WHILE, TT_TRUE, TT_FALSE,

    TT_I8, TT_I16, TT_I32, TT_I64, TT_U8, TT_U16, TT_U32, TT_U64,
    TT_F32, TT_F64, TT_BOOL, TT_STR, TT_VOID,

    TT_LPAREN, TT_RPAREN, TT_LBRACE, TT_RBRACE, TT_COMMA, TT_COLON, TT_SEMI,
    TT_ARROW,

    TT_EQ, TT_EQEQ, TT_BANGEQ, TT_LT, TT_LTEQ, TT_GT, TT_GTEQ,
    TT_PLUS, TT_MINUS, TT_STAR, TT_SLASH, TT_PERCENT, TT_AMPAMP, TT_OROR,
    TT_BANG,

    TT_ERROR, /* lexer failure; token.text holds short reason */
} TokenKind;

typedef struct {
    TokenKind kind;
    size_t    pos;    /* byte offset in source */
    size_t    len;    /* byte length */
    size_t    line;   /* 1-based */
    size_t    col;    /* 1-based byte column */

    union {
        uint64_t ival;  /* TT_INT  */
        double   fval;  /* TT_FLOAT */
        SV       text;  /* TT_IDENT */
        SV       str;   /* TT_STRING (decoded, arena-owned) */
    } v;
} Token;

const char *tok_kind_name(TokenKind k);

#endif /* CPM_TOKEN_H */