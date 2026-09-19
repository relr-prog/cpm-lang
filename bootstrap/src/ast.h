#ifndef CPM_AST_H
#define CPM_AST_H

#include "util/util.h"
#include "token.h"

/* ---------- Type ---------- */
typedef enum {
    TY_I8, TY_I16, TY_I32, TY_I64,
    TY_U8, TY_U16, TY_U32, TY_U64,
    TY_F32, TY_F64, TY_BOOL, TY_STR, TY_VOID, TY_ERROR,
} TyKind;

typedef struct {
    TyKind   kind;
    uint32_t ptr_depth; /* levels of 'type*' indirection (future) */
} Type;

const char *type_name(TyKind k);

/* ---------- Forward ---------- */
typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Decl Decl;
typedef struct Param Param;
typedef struct Program Program;

/* ---------- Expressions ---------- */
typedef enum {
    EX_INT, EX_FLOAT, EX_BOOL, EX_STR, EX_IDENT, EX_BINOP, EX_UNOP, EX_CALL,
} ExprKind;

typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_LAND, OP_LOR,
} BinOpKind;

typedef enum { UN_NEG, UN_NOT } UnOpKind;

struct Expr {
    ExprKind kind;
    size_t   pos; /* source offset of expression start */
    union {
        uint64_t ival;
        double   fval;
        bool     bval;
        SV       str;
        SV       ident;
        struct { BinOpKind op; Expr *l; Expr *r; } bin;
        struct { UnOpKind op; Expr *e; } un;
        struct { SV callee; VEC args; } call; /* args: Vec of Expr* */
    } as;
};

/* ---------- Statements ---------- */
typedef enum {
    ST_BLOCK, ST_IF, ST_WHILE, ST_BREAK, ST_CONTINUE, ST_RETURN, ST_VAR, ST_ASSIGN, ST_EXPR,
} StmtKind;

struct Stmt {
    StmtKind kind;
    size_t   pos;
    union {
        struct { VEC stmts; } block;                 /* Vec of Stmt* */
        struct { Expr *cond; Stmt *then; Stmt *els; } iff;
        struct { Expr *cond; Stmt *body; } whl;
        struct { bool has_val; Expr *val; } ret;     /* return */
        struct { Type type; SV name; Expr *init; } var;
        struct { SV name; Expr *rhs; } assign;
        struct { Expr *expr; } expr;
    } as;
};

/* ---------- Declarations ---------- */
struct Param {
    SV   name;
    Type type;
    size_t pos;
};

typedef enum { DECL_FN, DECL_GLOBAL } DeclKind;

struct Decl {
    DeclKind kind;
    SV       name;
    size_t   pos;
    Type     fn_ret;            /* DECL_FN */
    VEC      fn_params;         /* DECL_FN: Vec of Param* */
    Stmt     fn_body;           /* DECL_FN */
    Type     g_type;            /* DECL_GLOBAL */
    Expr    *g_init;            /* DECL_GLOBAL: initializer expr */
};

struct Program {
    VEC decls; /* Vec of Decl*, source order */
};

#endif /* CPM_AST_H */