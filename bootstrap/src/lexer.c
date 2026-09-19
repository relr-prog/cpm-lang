#define _POSIX_C_SOURCE 200809L
#include "lexer.h"

#include <ctype.h>

void lex_init(Lexer *lx, Source src, Arena *a) {
    lx->src = src;
    lx->a = a;
    lx->pos = 0;
    lx->line = 1;
    lx->col = 1;
}

static Token make_tok(Lexer *lx, TokenKind k, size_t start, size_t len) {
    Token t;
    t.kind = k;
    t.pos = start;
    t.len = len;
    t.line = lx->line;
    t.col = lx->col - len;
    memset(&t.v, 0, sizeof(t.v));
    return t;
}

static void advance(Lexer *lx, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (lx->pos < lx->src.len && lx->src.src[lx->pos] == '\n') {
            lx->line++;
            lx->col = 1;
        } else {
            lx->col++;
        }
        lx->pos++;
    }
}

static char peek(Lexer *lx, size_t off) {
    if (lx->pos + off >= lx->src.len) return '\0';
    return lx->src.src[lx->pos + off];
}

static bool is_ident_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}
static bool is_ident_cont(char c) {
    return isalnum((unsigned char)c) || c == '_';
}
static bool is_digit(char c) { return c >= '0' && c <= '9'; }

static TokenKind keyword(SV s) {
    struct { const char *kw; TokenKind k; } map[] = {
        { "fn", TT_FN },     { "var", TT_VAR },   { "return", TT_RETURN },
        { "if", TT_IF },     { "else", TT_ELSE }, { "while", TT_WHILE },
        { "true", TT_TRUE }, { "false", TT_FALSE },
        { "i8", TT_I8 },     { "i16", TT_I16 },   { "i32", TT_I32 },
        { "i64", TT_I64 },   { "u8", TT_U8 },     { "u16", TT_U16 },
        { "u32", TT_U32 },   { "u64", TT_U64 },
        { "f32", TT_F32 },   { "f64", TT_F64 },
        { "bool", TT_BOOL }, { "void", TT_VOID },
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (sv_eq_cstr(s, map[i].kw)) return map[i].k;
    }
    return TT_IDENT;
}

static Token lex_number(Lexer *lx, Diag *d, size_t start) {
    (void)d;
    size_t i = 0;
    while (is_digit(peek(lx, i))) i++;
    bool is_float = false;
    if (peek(lx, i) == '.') {
        is_float = true;
        i++;
        if (!is_digit(peek(lx, i))) {
            /* "123." -- allow trailing dot like C does */
        } else {
            while (is_digit(peek(lx, i))) i++;
        }
    }
    if ((peek(lx, i) == 'e' || peek(lx, i) == 'E')) {
        size_t j = i + 1;
        if (peek(lx, j) == '+' || peek(lx, j) == '-') j++;
        if (is_digit(peek(lx, j))) {
            is_float = true;
            i = j;
            while (is_digit(peek(lx, i))) i++;
        }
    }

    Token t = make_tok(lx, is_float ? TT_FLOAT : TT_INT, start, i);
    if (is_float) {
        char buf[64];
        size_t n = i < sizeof(buf) ? i : sizeof(buf) - 1;
        memcpy(buf, lx->src.src + start, n);
        buf[n] = '\0';
        t.v.fval = strtod(buf, NULL);
    } else {
        uint64_t val = 0;
        for (size_t c = 0; c < i; c++) {
            val = val * 10 + (uint64_t)(lx->src.src[start + c] - '0');
        }
        t.v.ival = val;
    }
    advance(lx, i);
    return t;
}

static Token lex_ident(Lexer *lx, size_t start) {
    size_t i = 0;
    while (is_ident_cont(peek(lx, i))) i++;
    Token t = make_tok(lx, TT_IDENT, start, i);
    t.v.text = sv_sub(sv_from_cstr(lx->src.src), start, i);
    t.kind = keyword(t.v.text);
    advance(lx, i);
    return t;
}

static Token lex_string(Lexer *lx, Diag *d, size_t start) {
    advance(lx, 1); /* opening quote */
    size_t cap = 32;
    char *buf = arena_alloc(lx->a, cap, 1);
    size_t n = 0;
    bool closed = false;

    for (;;) {
        if (lx->pos >= lx->src.len || peek(lx, 0) == '\n') break;
        char c = peek(lx, 0);
        if (c == '"') {
            advance(lx, 1);
            closed = true;
            break;
        }
        if (c == '\\') {
            advance(lx, 1);
            char e = peek(lx, 0);
            advance(lx, 1);
            char out;
            switch (e) {
            case 'n': out = '\n'; break;
            case 't': out = '\t'; break;
            case 'r': out = '\r'; break;
            case '0': out = '\0'; break;
            case '\\': out = '\\'; break;
            case '"': out = '"'; break;
            default:
                if (e == '\0' || e == '\n') { advance(lx, 0); }
                diag_error(d, start, "unknown escape sequence '\\%c'", e);
                out = e;
                break;
            }
            if (n + 1 >= cap) {
                cap *= 2;
                char *nb = arena_alloc(lx->a, cap, 1);
                memcpy(nb, buf, n);
                buf = nb;
            }
            buf[n++] = out;
        } else {
            if (n + 1 >= cap) {
                cap *= 2;
                char *nb = arena_alloc(lx->a, cap, 1);
                memcpy(nb, buf, n);
                buf = nb;
            }
            buf[n++] = c;
            advance(lx, 1);
        }
    }

    if (!closed) {
        diag_error(d, start, "unterminated string literal");
        Token t = make_tok(lx, TT_ERROR, start, 2);
        t.v.str = (SV){ buf, n };
        return t;
    }

    Token t = make_tok(lx, TT_STRING, start, n + 2);
    t.v.str = (SV){ buf, n };
    return t;
}

Token lex_next(Lexer *lx, Diag *d) {
    /* skip whitespace + comments */
    for (;;) {
        char c = peek(lx, 0);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lx, 1);
        } else if (c == '/' && peek(lx, 1) == '/') {
            while (lx->pos < lx->src.len && peek(lx, 0) != '\n') advance(lx, 1);
        } else if (c == '/' && peek(lx, 1) == '*') {
            size_t start = lx->pos;
            advance(lx, 2);
            while (!(peek(lx, 0) == '*' && peek(lx, 1) == '/')) {
                if (lx->pos >= lx->src.len) {
                    Token t = make_tok(lx, TT_ERROR, start, 2);
                    t.v.text = sv_make("unterminated /* comment");
                    diag_error(d, start, "unterminated block comment");
                    return t;
                }
                advance(lx, 1);
            }
            advance(lx, 2);
        } else {
            break;
        }
    }

    size_t start = lx->pos;
    char c = peek(lx, 0);
    if (c == '\0' || lx->pos >= lx->src.len) {
        Token t = make_tok(lx, TT_EOF, start, 0);
        t.line = lx->line;
        t.col = lx->col;
        return t;
    }

    if (is_ident_start(c)) return lex_ident(lx, start);
    if (is_digit(c)) return lex_number(lx, d, start);
    if (c == '"') return lex_string(lx, d, start);

    Token t = make_tok(lx, TT_EOF, start, 1);
    switch (c) {
    case '(': t.kind = TT_LPAREN; advance(lx, 1); break;
    case ')': t.kind = TT_RPAREN; advance(lx, 1); break;
    case '{': t.kind = TT_LBRACE; advance(lx, 1); break;
    case '}': t.kind = TT_RBRACE; advance(lx, 1); break;
    case ',': t.kind = TT_COMMA; advance(lx, 1); break;
    case ':': t.kind = TT_COLON; advance(lx, 1); break;
    case ';': t.kind = TT_SEMI; advance(lx, 1); break;
    case '+': t.kind = TT_PLUS; advance(lx, 1); break;
    case '-':
        if (peek(lx, 1) == '>') {
            t.kind = TT_ARROW;
            t.len = 2;
            advance(lx, 2);
        } else {
            t.kind = TT_MINUS;
            advance(lx, 1);
        }
        break;
    case '*': t.kind = TT_STAR; advance(lx, 1); break;
    case '%': t.kind = TT_PERCENT; advance(lx, 1); break;
    case '!':
        if (peek(lx, 1) == '=') {
            t.kind = TT_BANGEQ;
            t.len = 2;
            advance(lx, 2);
        } else {
            t.kind = TT_BANG;
            advance(lx, 1);
        }
        break;
    case '=':
        if (peek(lx, 1) == '=') {
            t.kind = TT_EQEQ;
            t.len = 2;
            advance(lx, 2);
        } else {
            t.kind = TT_EQ;
            advance(lx, 1);
        }
        break;
    case '<':
        if (peek(lx, 1) == '=') {
            t.kind = TT_LTEQ;
            t.len = 2;
            advance(lx, 2);
        } else {
            t.kind = TT_LT;
            advance(lx, 1);
        }
        break;
    case '>':
        if (peek(lx, 1) == '=') {
            t.kind = TT_GTEQ;
            t.len = 2;
            advance(lx, 2);
        } else {
            t.kind = TT_GT;
            advance(lx, 1);
        }
        break;
    case '&':
        if (peek(lx, 1) == '&') {
            t.kind = TT_AMPAMP;
            t.len = 2;
            advance(lx, 2);
        } else {
            Token er = make_tok(lx, TT_ERROR, start, 1);
            er.v.text = sv_make("unexpected '&'");
            diag_error(d, start, "unexpected character '&' (did you mean '&&'?)");
            advance(lx, 1);
            return er;
        }
        break;
    case '|':
        if (peek(lx, 1) == '|') {
            t.kind = TT_OROR;
            t.len = 2;
            advance(lx, 2);
        } else {
            Token er = make_tok(lx, TT_ERROR, start, 1);
            er.v.text = sv_make("unexpected '|'");
            diag_error(d, start, "unexpected character '|' (did you mean '||'?)");
            advance(lx, 1);
            return er;
        }
        break;
    case '/':
        t.kind = TT_SLASH;
        advance(lx, 1);
        break;
    default:
        t.kind = TT_ERROR;
        diag_error(d, start, "unexpected character '%c' (0x%02x)", c, c);
        t.v.text = sv_make("unexpected character");
        advance(lx, 1);
        break;
    }
    return t;
}

const char *tok_kind_name(TokenKind k) {
    switch (k) {
    case TT_EOF: return "end of file";
    case TT_IDENT: return "identifier";
    case TT_INT: return "integer";
    case TT_FLOAT: return "float";
    case TT_STRING: return "string";
    case TT_FN: return "'fn'";
    case TT_VAR: return "'var'";
    case TT_RETURN: return "'return'";
    case TT_IF: return "'if'";
    case TT_ELSE: return "'else'";
    case TT_WHILE: return "'while'";
    case TT_TRUE: return "'true'";
    case TT_FALSE: return "'false'";
    case TT_I8: return "type 'i8'";
    case TT_I16: return "type 'i16'";
    case TT_I32: return "type 'i32'";
    case TT_I64: return "type 'i64'";
    case TT_U8: return "type 'u8'";
    case TT_U16: return "type 'u16'";
    case TT_U32: return "type 'u32'";
    case TT_U64: return "type 'u64'";
    case TT_F32: return "type 'f32'";
    case TT_F64: return "type 'f64'";
    case TT_BOOL: return "type 'bool'";
    case TT_STR: return "type 'str'";
    case TT_VOID: return "type 'void'";
    case TT_LPAREN: return "'('";
    case TT_RPAREN: return "')'";
    case TT_LBRACE: return "'{'";
    case TT_RBRACE: return "'}'";
    case TT_COMMA: return "','";
    case TT_COLON: return "':'";
    case TT_SEMI: return "';'";
    case TT_ARROW: return "'->'";
    case TT_EQ: return "'='";
    case TT_EQEQ: return "'=='";
    case TT_BANGEQ: return "'!='";
    case TT_LT: return "'<'";
    case TT_LTEQ: return "'<='";
    case TT_GT: return "'>'";
    case TT_GTEQ: return "'>='";
    case TT_PLUS: return "'+'";
    case TT_MINUS: return "'-'";
    case TT_STAR: return "'*'";
    case TT_SLASH: return "'/'";
    case TT_PERCENT: return "'%'";
    case TT_AMPAMP: return "'&&'";
    case TT_OROR: return "'||'";
    case TT_BANG: return "'!'";
    case TT_ERROR: return "lexer error";
    }
    return "?";
}