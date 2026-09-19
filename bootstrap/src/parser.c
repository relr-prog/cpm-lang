#define _POSIX_C_SOURCE 200809L
#include "parser.h"

#include "../lexer.h"

typedef struct {
    Lexer lx;
    Token cur;
    Diag *d;
    Arena *a;
    bool has_peek;
    Token peeked;
    bool failed;
} Parser;

static void p_advance(Parser *p) {
    if (p->has_peek) {
        p->cur = p->peeked;
        p->has_peek = false;
    } else {
        p->cur = lex_next(&p->lx, p->d);
    }
    if (p->cur.kind == TT_ERROR) p->failed = true;
}

static Token p_peek(Parser *p) {
    if (!p->has_peek) {
        p->peeked = lex_next(&p->lx, p->d);
        p->has_peek = true;
    }
    if (p->peeked.kind == TT_ERROR) p->failed = true;
    return p->peeked;
}

static bool p_is(Parser *p, TokenKind k) { return p->cur.kind == k; }
static bool p_peek_is(Parser *p, TokenKind k) { return p_peek(p).kind == k; }

static void p_exp_err(Parser *p, const char *what) {
    if (p->cur.kind == TT_IDENT)
        diag_error(p->d, p->cur.pos, "expected %s, found identifier '%.*s'",
                   what, SV_ARG(p->cur.v.text));
    else
        diag_error(p->d, p->cur.pos, "expected %s, found %s", what,
                   tok_kind_name(p->cur.kind));
}

/* consume `k`; on mismatch report "expected X" and advance past it */
static bool p_expect(Parser *p, TokenKind k, const char *what) {
    if (!p_is(p, k)) {
        p_exp_err(p, what);
        if (p->cur.kind == TT_EOF) { p->failed = true; return false; }
        p_advance(p);
        return false;
    }
    p_advance(p);
    return true;
}

static Expr *parse_expr(Parser *p, int min_prec);
static Stmt *parse_stmt(Parser *p);

static Type parse_type(Parser *p) {
    Type t = { TY_VOID, 0 };
    switch (p->cur.kind) {
    case TT_I8: t.kind = TY_I8; break;
    case TT_I16: t.kind = TY_I16; break;
    case TT_I32: t.kind = TY_I32; break;
    case TT_I64: t.kind = TY_I64; break;
    case TT_U8: t.kind = TY_U8; break;
    case TT_U16: t.kind = TY_U16; break;
    case TT_U32: t.kind = TY_U32; break;
    case TT_U64: t.kind = TY_U64; break;
    case TT_F32: t.kind = TY_F32; break;
    case TT_F64: t.kind = TY_F64; break;
    case TT_BOOL: t.kind = TY_BOOL; break;
    case TT_STR: t.kind = TY_STR; break;
    case TT_VOID: t.kind = TY_VOID; break;
    default:
        p_exp_err(p, "type name");
        return t; /* don't consume; caller's expect() handles the fallout */
    }
    p_advance(p);
    return t;
}

static Expr *new_expr(Parser *p, ExprKind k, size_t pos) {
    Expr *e = arena_alloc(p->a, sizeof(Expr), 8);
    memset(e, 0, sizeof(*e));
    e->kind = k;
    e->pos = pos;
    return e;
}

static Stmt *new_stmt(Parser *p, StmtKind k, size_t pos) {
    Stmt *s = arena_alloc(p->a, sizeof(Stmt), 8);
    memset(s, 0, sizeof(*s));
    s->kind = k;
    s->pos = pos;
    return s;
}

static const struct { TokenKind tok; BinOpKind op; int prec; } BINOP_TABLE[] = {
    { TT_OROR,     OP_LOR, 1 },
    { TT_AMPAMP,   OP_LAND, 2 },
    { TT_EQEQ,     OP_EQ,  3 },
    { TT_BANGEQ,   OP_NE,  3 },
    { TT_LT,       OP_LT,  4 },
    { TT_LTEQ,     OP_LE,  4 },
    { TT_GT,       OP_GT,  4 },
    { TT_GTEQ,     OP_GE,  4 },
    { TT_PLUS,     OP_ADD, 5 },
    { TT_MINUS,    OP_SUB, 5 },
    { TT_STAR,     OP_MUL, 6 },
    { TT_SLASH,    OP_DIV, 6 },
    { TT_PERCENT,  OP_MOD, 6 },
};

static TokenKind binop_tok(TokenKind k, bool *found) {
    for (size_t i = 0; i < sizeof(BINOP_TABLE) / sizeof(BINOP_TABLE[0]); i++)
        if (BINOP_TABLE[i].tok == k) { *found = true; return k; }
    *found = false;
    return TT_EOF;
}

static int binop_prec(TokenKind k) {
    for (size_t i = 0; i < sizeof(BINOP_TABLE) / sizeof(BINOP_TABLE[0]); i++)
        if (BINOP_TABLE[i].tok == k) return BINOP_TABLE[i].prec;
    return -1;
}

static BinOpKind binop_of(TokenKind tk) {
    for (size_t i = 0; i < sizeof(BINOP_TABLE) / sizeof(BINOP_TABLE[0]); i++)
        if (BINOP_TABLE[i].tok == tk) return BINOP_TABLE[i].op;
    return OP_ADD;
}

static Expr *parse_primary(Parser *p) {
    size_t pos = p->cur.pos;
    switch (p->cur.kind) {
    case TT_INT: {
        Expr *e = new_expr(p, EX_INT, pos);
        e->as.ival = p->cur.v.ival;
        p_advance(p);
        return e;
    }
    case TT_FLOAT: {
        Expr *e = new_expr(p, EX_FLOAT, pos);
        e->as.fval = p->cur.v.fval;
        p_advance(p);
        return e;
    }
    case TT_STRING: {
        Expr *e = new_expr(p, EX_STR, pos);
        e->as.str = p->cur.v.str;
        p_advance(p);
        return e;
    }
    case TT_TRUE:
    case TT_FALSE: {
        Expr *e = new_expr(p, EX_BOOL, pos);
        e->as.bval = (p->cur.kind == TT_TRUE);
        p_advance(p);
        return e;
    }
    case TT_IDENT: {
        SV name = p->cur.v.text;
        p_advance(p);
        if (p_is(p, TT_LPAREN)) {
            p_advance(p);
            Expr *e = new_expr(p, EX_CALL, pos);
            e->as.call.callee = name;
            while (!p_is(p, TT_RPAREN)) {
                if (p_is(p, TT_EOF)) {
                    p_exp_err(p, "')' to close call");
                    p->failed = true;
                    break;
                }
                Expr *arg = parse_expr(p, 0);
                vec_push(&e->as.call.args, arg);
                if (p_is(p, TT_COMMA)) { p_advance(p); continue; }
                break;
            }
            p_expect(p, TT_RPAREN, "')'");
            return e;
        }
        Expr *e = new_expr(p, EX_IDENT, pos);
        e->as.ident = name;
        return e;
    }
    case TT_LPAREN: {
        p_advance(p);
        Expr *e = parse_expr(p, 0);
        p_expect(p, TT_RPAREN, "')'");
        return e;
    }
    case TT_MINUS: {
        p_advance(p);
        Expr *e = new_expr(p, EX_UNOP, pos);
        e->as.un.op = UN_NEG;
        e->as.un.e = parse_expr(p, 7);
        return e;
    }
    case TT_BANG: {
        p_advance(p);
        Expr *e = new_expr(p, EX_UNOP, pos);
        e->as.un.op = UN_NOT;
        e->as.un.e = parse_expr(p, 7);
        return e;
    }
    default:
        p_exp_err(p, "expression");
        if (p->cur.kind != TT_EOF) p_advance(p);
        return new_expr(p, EX_INT, pos); /* placeholder keeps parser alive */
    }
}

static Expr *parse_expr(Parser *p, int min_prec) {
    Expr *lhs = parse_primary(p);
    for (;;) {
        bool found;
        TokenKind tk = binop_tok(p->cur.kind, &found);
        if (!found) break;
        int prec = binop_prec(tk);
        if (prec < min_prec) break;
        size_t pos = p->cur.pos;
        p_advance(p);
        Expr *rhs = parse_expr(p, prec + 1);
        Expr *e = new_expr(p, EX_BINOP, pos);
        e->as.bin.op = binop_of(tk);
        e->as.bin.l = lhs;
        e->as.bin.r = rhs;
        lhs = e;
    }
    return lhs;
}

static Expr *parse_expr0(Parser *p) { return parse_expr(p, 0); }

static Stmt *parse_block(Parser *p) {
    size_t pos = p->cur.pos;
    p_expect(p, TT_LBRACE, "'{' to open block");
    Stmt *s = new_stmt(p, ST_BLOCK, pos);
    while (!p_is(p, TT_RBRACE) && !p_is(p, TT_EOF)) {
        Stmt *inner = parse_stmt(p);
        if (inner) vec_push(&s->as.block.stmts, inner);
    }
    p_expect(p, TT_RBRACE, "'}' to close block");
    return s;
}

static Stmt *parse_var_stmt(Parser *p) {
    size_t pos = p->cur.pos;
    p_advance(p); /* 'var' */
    Stmt *s = new_stmt(p, ST_VAR, pos);
    if (!p_is(p, TT_IDENT))
        p_exp_err(p, "variable name");
    s->as.var.name = p->cur.v.text;
    p_advance(p);
    p_expect(p, TT_COLON, "':'");
    s->as.var.type = parse_type(p);
    if (p_is(p, TT_EQ)) {
        p_advance(p);
        s->as.var.init = parse_expr0(p);
    } else {
        s->as.var.init = NULL;
        p_exp_err(p, "'=' in variable declaration");
    }
    p_expect(p, TT_SEMI, "';' after declaration");
    return s;
}

static Stmt *parse_if_stmt(Parser *p) {
    size_t pos = p->cur.pos;
    p_advance(p); /* 'if' */
    Stmt *s = new_stmt(p, ST_IF, pos);
    p_expect(p, TT_LPAREN, "'(' after 'if'");
    s->as.iff.cond = parse_expr0(p);
    p_expect(p, TT_RPAREN, "')'");
    s->as.iff.then = parse_block(p);
    s->as.iff.els = NULL;
    if (p_is(p, TT_ELSE)) {
        p_advance(p);
        if (p_is(p, TT_IF))
            s->as.iff.els = parse_stmt(p);
        else
            s->as.iff.els = parse_block(p);
    }
    return s;
}

static Stmt *parse_while_stmt(Parser *p) {
    size_t pos = p->cur.pos;
    p_advance(p); /* 'while' */
    Stmt *s = new_stmt(p, ST_WHILE, pos);
    p_expect(p, TT_LPAREN, "'(' after 'while'");
    s->as.whl.cond = parse_expr0(p);
    p_expect(p, TT_RPAREN, "')'");
    s->as.whl.body = parse_block(p);
    return s;
}

static Stmt *parse_return_stmt(Parser *p) {
    size_t pos = p->cur.pos;
    p_advance(p); /* 'return' */
    Stmt *s = new_stmt(p, ST_RETURN, pos);
    if (p_is(p, TT_SEMI)) {
        s->as.ret.has_val = false;
        s->as.ret.val = NULL;
    } else {
        s->as.ret.has_val = true;
        s->as.ret.val = parse_expr0(p);
    }
    p_expect(p, TT_SEMI, "';' after 'return'");
    return s;
}

static Stmt *parse_stmt(Parser *p) {
    switch (p->cur.kind) {
    case TT_LBRACE:
        return parse_block(p);
    case TT_VAR:
        return parse_var_stmt(p);
    case TT_IF:
        return parse_if_stmt(p);
    case TT_WHILE:
        return parse_while_stmt(p);
    case TT_RETURN:
        return parse_return_stmt(p);
    case TT_BREAK: {
        size_t pos = p->cur.pos;
        p_advance(p);
        Stmt *s = new_stmt(p, ST_BREAK, pos);
        p_expect(p, TT_SEMI, "';' after 'break'");
        return s;
    }
    case TT_CONTINUE: {
        size_t pos = p->cur.pos;
        p_advance(p);
        Stmt *s = new_stmt(p, ST_CONTINUE, pos);
        p_expect(p, TT_SEMI, "';' after 'continue'");
        return s;
    }
    case TT_SEMI: {
        size_t pos = p->cur.pos;
        p_advance(p);
        return new_stmt(p, ST_EXPR, pos);
    }
    case TT_IDENT:
        if (p_peek_is(p, TT_EQ)) {
            /* `x = expr;` */
            size_t pos = p->cur.pos;
            SV name = p->cur.v.text;
            p_advance(p); /* ident */
            p_advance(p); /* '=' */
            Stmt *s = new_stmt(p, ST_ASSIGN, pos);
            s->as.assign.name = name;
            s->as.assign.rhs = parse_expr0(p);
            p_expect(p, TT_SEMI, "';' after assignment");
            return s;
        }
        /* fallthrough: expression statement */
        /* FALLTHROUGH */
    default: {
        size_t pos = p->cur.pos;
        Expr *e = parse_expr(p, 0);
        Stmt *s = new_stmt(p, ST_EXPR, pos);
        s->as.expr.expr = e;
        p_expect(p, TT_SEMI, "';' after expression statement");
        return s;
    }
    }
}

static Decl *parse_params_then_body(Parser *p, Decl *d) {
    p_expect(p, TT_LPAREN, "'(' to open parameter list");
    if (p_is(p, TT_RPAREN)) {
        p_advance(p);
    } else {
        for (;;) {
            Param *param = arena_alloc(p->a, sizeof(Param), 8);
            param->pos = p->cur.pos;
            if (!p_is(p, TT_IDENT))
                p_exp_err(p, "parameter name");
            param->name = p->cur.v.text;
            p_advance(p);
            p_expect(p, TT_COLON, "':'");
            param->type = parse_type(p);
            vec_push(&d->fn_params, param);
            if (p_is(p, TT_COMMA)) { p_advance(p); continue; }
            break;
        }
        p_expect(p, TT_RPAREN, "')' to close parameter list");
    }
    p_expect(p, TT_ARROW, "'->' after parameter list");
    d->fn_ret = parse_type(p);
    Stmt *body = parse_block(p);
    d->fn_body = *body;
    return d;
}

static Decl *parse_fn_decl(Parser *p) {
    size_t pos = p->cur.pos;
    p_advance(p); /* 'fn' */
    Decl *d = arena_alloc(p->a, sizeof(Decl), 8);
    memset(d, 0, sizeof(*d));
    d->kind = DECL_FN;
    d->pos = pos;
    if (!p_is(p, TT_IDENT))
        p_exp_err(p, "function name");
    d->name = p->cur.v.text;
    p_advance(p);
    return parse_params_then_body(p, d);
}

static Decl *parse_global_var(Parser *p) {
    size_t pos = p->cur.pos;
    p_advance(p); /* 'var' */
    Decl *d = arena_alloc(p->a, sizeof(Decl), 8);
    memset(d, 0, sizeof(*d));
    d->kind = DECL_GLOBAL;
    d->pos = pos;
    if (!p_is(p, TT_IDENT))
        p_exp_err(p, "global variable name");
    d->name = p->cur.v.text;
    p_advance(p);
    p_expect(p, TT_COLON, "':'");
    d->g_type = parse_type(p);
    if (p_is(p, TT_EQ)) {
        p_advance(p);
        d->g_init = parse_expr0(p);
    } else {
        d->g_init = NULL;
    }
    p_expect(p, TT_SEMI, "';' after global declaration");
    return d;
}

Program *parse_program(Source src, Diag *d, Arena *a) {
    Parser p;
    memset(&p, 0, sizeof(p));
    lex_init(&p.lx, src, a);
    p.d = d;
    p.a = a;
    p_advance(&p);

    Program *prog = arena_alloc(a, sizeof(Program), 8);
    memset(prog, 0, sizeof(*prog));

    while (!p_is(&p, TT_EOF)) {
        if (p.failed) break;
        if (p_is(&p, TT_FN)) {
            Decl *fn = parse_fn_decl(&p);
            vec_push(&prog->decls, fn);
        } else if (p_is(&p, TT_VAR)) {
            Decl *g = parse_global_var(&p);
            vec_push(&prog->decls, g);
        } else {
            p_exp_err(&p, "function or variable declaration");
            if (p.cur.kind == TT_EOF) break;
            p_advance(&p);
        }
    }
    return prog;
}