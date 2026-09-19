#define _POSIX_C_SOURCE 200809L
#include "resolve.h"

#include <stdio.h>

typedef struct Scope {
    struct Scope *parent;
    VEC           names; /* Vec of char* (arena-owned) */
    VEC           types; /* Vec of Type, parallel to names */
} Scope;

typedef struct {
    Arena   *a;
    Diag    *d;
    Program *prog;
    Scope   *fn_scope;
    Scope   *scope;
    Type     cur_ret;
    int      loop_depth;
} RC;

typedef struct { SV name; int arity; Type arg_type; } Intrinsic;

static const Intrinsic INTRINSICS[] = {
    { { "print", 5 }, 1, { TY_STR, 0 } },
    { { "println", 7 }, 1, { TY_STR, 0 } },
    { { "print_i32", 9 }, 1, { TY_I32, 0 } },
    { { "println_i32", 11 }, 1, { TY_I32, 0 } },
    { { "print_i64", 9 }, 1, { TY_I64, 0 } },
    { { "println_i64", 11 }, 1, { TY_I64, 0 } },
    { { "print_f64", 9 }, 1, { TY_F64, 0 } },
    { { "println_f64", 11 }, 1, { TY_F64, 0 } },
    { { "print_bool", 10 }, 1, { TY_BOOL, 0 } },
    { { "println_bool", 12 }, 1, { TY_BOOL, 0 } },
};

bool resolve_is_intrinsic(const SV name, int *arity) {
    for (size_t i = 0; i < sizeof(INTRINSICS) / sizeof(INTRINSICS[0]); i++) {
        if (sv_eq(name, INTRINSICS[i].name)) {
            *arity = INTRINSICS[i].arity;
            return true;
        }
    }
    return false;
}

static const Intrinsic *find_intrinsic(SV name) {
    for (size_t i = 0; i < sizeof(INTRINSICS) / sizeof(INTRINSICS[0]); i++)
        if (sv_eq(name, INTRINSICS[i].name)) return &INTRINSICS[i];
    return NULL;
}

static bool type_eq(Type a, Type b) {
    return a.kind == b.kind && a.ptr_depth == b.ptr_depth;
}

static bool type_is_integer(Type t) {
    return t.ptr_depth == 0 &&
           (t.kind == TY_I8 || t.kind == TY_I16 || t.kind == TY_I32 || t.kind == TY_I64 ||
            t.kind == TY_U8 || t.kind == TY_U16 || t.kind == TY_U32 || t.kind == TY_U64);
}

static bool type_is_float(Type t) {
    return t.ptr_depth == 0 && (t.kind == TY_F32 || t.kind == TY_F64);
}

static bool type_is_numeric(Type t) {
    return type_is_integer(t) || type_is_float(t);
}

static const char *type_desc(Type t) {
    /* Pointer types are reserved for a future phase; keep diagnostics useful. */
    return type_name(t.kind);
}

static void diag_type_mismatch(RC *rc, size_t pos, const char *context,
                               Type expected, Type got) {
    diag_error(rc->d, pos, "%s: expected '%s', got '%s'",
               context, type_desc(expected), type_desc(got));
}

static Scope *scope_push(RC *rc, Scope *parent) {
    Scope *s = arena_alloc(rc->a, sizeof(Scope), 8);
    memset(s, 0, sizeof(*s));
    s->parent = parent;
    return s;
}

static bool scope_def(RC *rc, Scope *s, SV name, Type type, size_t pos) {
    for (size_t i = 0; i < s->names.len; i++) {
        const char *p = (const char *)vec_get(&s->names, i);
        if (strlen(p) == name.len && memcmp(p, name.ptr, name.len) == 0) {
            diag_error(rc->d, pos, "redefinition of variable '%.*s'",
                       SV_ARG(name));
            return false;
        }
    }
    vec_push(&s->names, sv_to_cstr_arena(rc->a, name));
    Type *slot = arena_alloc(rc->a, sizeof(Type), 8);
    *slot = type;
    vec_push(&s->types, slot);
    return true;
}

/* returns the decl of a global var that matches name, or NULL */
static Decl *find_global_var(RC *rc, SV name) {
    for (size_t i = 0; i < rc->prog->decls.len; i++) {
        Decl *d = vec_get(&rc->prog->decls, i);
        if (d->kind == DECL_GLOBAL && sv_eq(d->name, name)) return d;
    }
    return NULL;
}

static Decl *find_fn(RC *rc, SV name) {
    for (size_t i = 0; i < rc->prog->decls.len; i++) {
        Decl *d = vec_get(&rc->prog->decls, i);
        if (d->kind == DECL_FN && sv_eq(d->name, name)) return d;
    }
    return NULL;
}

/* Find a variable and return its declared type. */
static bool lookup_var(RC *rc, Scope *s, SV name, size_t pos, bool is_read,
                       Type *out_type) {
    for (Scope *sc = s; sc; sc = sc->parent) {
        for (size_t i = 0; i < sc->names.len; i++) {
            const char *p = (const char *)vec_get(&sc->names, i);
            if (strlen(p) == name.len && memcmp(p, name.ptr, name.len) == 0) {
                if (out_type) *out_type = *(Type *)vec_get(&sc->types, i);
                return true;
            }
        }
    }

    Decl *g = find_global_var(rc, name);
    if (g) {
        if (out_type) *out_type = g->g_type;
        return true;
    }

    if (find_fn(rc, name))
        diag_error(rc->d, pos, "function '%.*s' used as a variable",
                   SV_ARG(name));
    else
        diag_error(rc->d, pos, "use of undeclared %s '%.*s'",
                   is_read ? "variable" : "identifier", SV_ARG(name));
    if (out_type) *out_type = (Type){ TY_ERROR, 0 };
    return false;
}

static Type resolve_expr(RC *rc, Scope *s, Expr *e);

static void check_expected(RC *rc, size_t pos, const char *context,
                           Type expected, Type got) {
    if (expected.kind == TY_ERROR || got.kind == TY_ERROR)
        return; /* suppress cascading diagnostics from recoverable errors */
    if (!type_eq(expected, got))
        diag_type_mismatch(rc, pos, context, expected, got);
}

static Type resolve_call(RC *rc, Scope *s, Expr *e) {
    SV callee = e->as.call.callee;
    size_t nargs = e->as.call.args.len;

    Decl *fn = find_fn(rc, callee);
    if (fn) {
        if (fn->fn_params.len != nargs) {
            diag_error(rc->d, e->pos, "call to '%.*s' expects %zu argument%s, got %zu",
                       SV_ARG(callee), fn->fn_params.len,
                       fn->fn_params.len == 1 ? "" : "s", nargs);
        }

        size_t ncheck = nargs < fn->fn_params.len ? nargs : fn->fn_params.len;
        for (size_t i = 0; i < nargs; i++) {
            Expr *arg = vec_get(&e->as.call.args, i);
            Type got = resolve_expr(rc, s, arg);
            if (i < ncheck) {
                Param *p = vec_get(&fn->fn_params, i);
                char context[128];
                snprintf(context, sizeof(context), "argument %zu to '%.*s'",
                         i + 1, SV_ARG(callee));
                check_expected(rc, arg->pos, context, p->type, got);
            }
        }
        return fn->fn_ret;
    }

    const Intrinsic *intr = find_intrinsic(callee);
    if (intr) {
        if (intr->arity != (int)nargs) {
            diag_error(rc->d, e->pos, "call to intrinsic '%.*s' expects %d argument%s, got %zu",
                       SV_ARG(callee), intr->arity,
                       intr->arity == 1 ? "" : "s", nargs);
        }
        for (size_t i = 0; i < nargs; i++) {
            Expr *arg = vec_get(&e->as.call.args, i);
            Type got = resolve_expr(rc, s, arg);
            if (i == 0 && intr->arity == 1) {
                char context[128];
                snprintf(context, sizeof(context), "argument 1 to '%.*s'",
                         SV_ARG(callee));
                check_expected(rc, arg->pos, context, intr->arg_type, got);
            }
        }
        return (Type){ TY_VOID, 0 };
    }

    if (find_global_var(rc, callee))
        diag_error(rc->d, e->pos, "cannot call variable '%.*s'",
                   SV_ARG(callee));
    else
        diag_error(rc->d, e->pos, "call to unknown function '%.*s'",
                   SV_ARG(callee));

    for (size_t i = 0; i < nargs; i++)
        resolve_expr(rc, s, vec_get(&e->as.call.args, i));
    return (Type){ TY_ERROR, 0 };
}

static Type resolve_expr(RC *rc, Scope *s, Expr *e) {
    if (!e) return (Type){ TY_VOID, 0 };

    switch (e->kind) {
    case EX_INT:
        return (Type){ TY_I32, 0 };
    case EX_FLOAT:
        return (Type){ TY_F64, 0 };
    case EX_BOOL:
        return (Type){ TY_BOOL, 0 };
    case EX_STR:
        return (Type){ TY_STR, 0 };

    case EX_IDENT: {
        Type t;
        lookup_var(rc, s, e->as.ident, e->pos, true, &t);
        return t;
    }

    case EX_CALL:
        return resolve_call(rc, s, e);

    case EX_UNOP: {
        Type t = resolve_expr(rc, s, e->as.un.e);
        if (t.kind == TY_ERROR) return t;
        if (e->as.un.op == UN_NEG) {
            if (!type_is_numeric(t))
                diag_error(rc->d, e->pos,
                           "unary '-' requires a numeric operand, got '%s'",
                           type_desc(t));
            return t;
        }
        if (e->as.un.op == UN_NOT) {
            if (!type_eq(t, (Type){ TY_BOOL, 0 }))
                diag_error(rc->d, e->pos,
                           "unary '!' requires 'bool' operand, got '%s'",
                           type_desc(t));
            return (Type){ TY_BOOL, 0 };
        }
        return t;
    }

    case EX_BINOP: {
        Type l = resolve_expr(rc, s, e->as.bin.l);
        Type r = resolve_expr(rc, s, e->as.bin.r);
        if (l.kind == TY_ERROR) return l;
        if (r.kind == TY_ERROR) return r;
        BinOpKind op = e->as.bin.op;

        switch (op) {
        case OP_LAND:
        case OP_LOR:
            if (!type_eq(l, (Type){ TY_BOOL, 0 }) ||
                !type_eq(r, (Type){ TY_BOOL, 0 })) {
                diag_error(rc->d, e->pos,
                           "logical operator requires 'bool' operands, got '%s' and '%s'",
                           type_desc(l), type_desc(r));
            }
            return (Type){ TY_BOOL, 0 };

        case OP_EQ:
        case OP_NE:
            if (!type_eq(l, r))
                diag_error(rc->d, e->pos,
                           "comparison requires operands of the same type, got '%s' and '%s'",
                           type_desc(l), type_desc(r));
            return (Type){ TY_BOOL, 0 };

        case OP_LT:
        case OP_LE:
        case OP_GT:
        case OP_GE:
            if (!type_is_numeric(l) || !type_is_numeric(r) || !type_eq(l, r))
                diag_error(rc->d, e->pos,
                           "ordered comparison requires matching numeric operands, got '%s' and '%s'",
                           type_desc(l), type_desc(r));
            return (Type){ TY_BOOL, 0 };

        case OP_MOD:
            if (!type_is_integer(l) || !type_is_integer(r) || !type_eq(l, r))
                diag_error(rc->d, e->pos,
                           "operator '%' requires matching integer operands, got '%s' and '%s'",
                           type_desc(l), type_desc(r));
            return l;

        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
            if (!type_is_numeric(l) || !type_is_numeric(r) || !type_eq(l, r))
                diag_error(rc->d, e->pos,
                           "arithmetic operator requires matching numeric operands, got '%s' and '%s'",
                           type_desc(l), type_desc(r));
            return l;
        }
        return (Type){ TY_VOID, 0 };
    }
    }

    return (Type){ TY_VOID, 0 };
}

static Stmt *resolve_stmt(RC *rc, Scope *s, Stmt *st) {
    switch (st->kind) {
    case ST_BLOCK: {
        Scope *inner = scope_push(rc, s);
        for (size_t i = 0; i < st->as.block.stmts.len; i++)
            resolve_stmt(rc, inner, vec_get(&st->as.block.stmts, i));
        return st;
    }

    case ST_IF: {
        Type cond = resolve_expr(rc, s, st->as.iff.cond);
        check_expected(rc, st->as.iff.cond->pos, "'if' condition", (Type){ TY_BOOL, 0 }, cond);
        resolve_stmt(rc, s, st->as.iff.then);
        if (st->as.iff.els) resolve_stmt(rc, s, st->as.iff.els);
        return st;
    }

    case ST_WHILE: {
        Type cond = resolve_expr(rc, s, st->as.whl.cond);
        check_expected(rc, st->as.whl.cond->pos, "'while' condition", (Type){ TY_BOOL, 0 }, cond);
        rc->loop_depth++;
        resolve_stmt(rc, s, st->as.whl.body);
        rc->loop_depth--;
        return st;
    }

    case ST_BREAK:
        if (rc->loop_depth == 0)
            diag_error(rc->d, st->pos, "'break' is only valid inside a 'while' loop");
        return st;

    case ST_CONTINUE:
        if (rc->loop_depth == 0)
            diag_error(rc->d, st->pos, "'continue' is only valid inside a 'while' loop");
        return st;

    case ST_VAR: {
        if (st->as.var.type.kind == TY_VOID)
            diag_error(rc->d, st->pos, "variable '%.*s' cannot have type 'void'",
                       SV_ARG(st->as.var.name));

        /* Define after checking the initializer: self-reference is not allowed. */
        if (st->as.var.init) {
            Type got = resolve_expr(rc, s, st->as.var.init);
            check_expected(rc, st->as.var.init->pos, "variable initializer",
                           st->as.var.type, got);
        }
        scope_def(rc, s, st->as.var.name, st->as.var.type, st->pos);
        return st;
    }

    case ST_ASSIGN: {
        Type lhs;
        bool found = lookup_var(rc, s, st->as.assign.name, st->pos, false, &lhs);
        Type rhs = st->as.assign.rhs
                 ? resolve_expr(rc, s, st->as.assign.rhs)
                 : (Type){ TY_VOID, 0 };
        if (found)
            check_expected(rc, st->as.assign.rhs ? st->as.assign.rhs->pos : st->pos,
                           "assignment", lhs, rhs);
        return st;
    }

    case ST_RETURN:
        if (st->as.ret.has_val) {
            if (rc->cur_ret.kind == TY_VOID) {
                diag_error(rc->d, st->pos,
                           "cannot return a value from a 'void' function");
                resolve_expr(rc, s, st->as.ret.val);
            } else {
                Type got = resolve_expr(rc, s, st->as.ret.val);
                check_expected(rc, st->as.ret.val->pos, "return value", rc->cur_ret, got);
            }
        } else if (rc->cur_ret.kind != TY_VOID) {
            diag_error(rc->d, st->pos,
                       "missing return value (function returns %s)",
                       type_name(rc->cur_ret.kind));
        }
        return st;

    case ST_EXPR:
        if (st->as.expr.expr) resolve_expr(rc, s, st->as.expr.expr);
        return st;
    }

    return st;
}

static void resolve_fn(RC *rc, Decl *fn) {
    Scope *root = scope_push(rc, NULL);
    rc->fn_scope = root;
    rc->scope = root;
    rc->cur_ret = fn->fn_ret;
    rc->loop_depth = 0;

    if (fn->fn_ret.kind == TY_VOID) {
        /* valid */
    }

    for (size_t i = 0; i < fn->fn_params.len; i++) {
        Param *p = vec_get(&fn->fn_params, i);
        if (p->type.kind == TY_VOID)
            diag_error(rc->d, p->pos, "parameter '%.*s' cannot have type 'void'",
                       SV_ARG(p->name));
        scope_def(rc, root, p->name, p->type, p->pos);
    }

    resolve_stmt(rc, root, &fn->fn_body);
    rc->scope = NULL;
    rc->fn_scope = NULL;
}

static void resolve_global(RC *rc, Decl *g) {
    if (g->g_type.kind == TY_VOID)
        diag_error(rc->d, g->pos, "global variable '%.*s' cannot have type 'void'",
                   SV_ARG(g->name));

    if (g->g_init) {
        Type got = resolve_expr(rc, NULL, g->g_init);
        check_expected(rc, g->g_init->pos, "global initializer", g->g_type, got);
    }
}

static void check_main(RC *rc) {
    Decl *main = find_fn(rc, sv_make("main"));
    if (!main) {
        diag_error(rc->d, 0, "no 'fn main' entry point found");
        return;
    }
    if (main->fn_ret.kind != TY_I32)
        diag_error(rc->d, main->pos, "entry point 'main' must return 'i32', got '%s'",
                   type_name(main->fn_ret.kind));
    if (main->fn_params.len != 0)
        diag_error(rc->d, main->pos, "entry point 'main' must take no parameters");
}

int resolve_program(Program *prog, Diag *d, Arena *a) {
    RC rc;
    memset(&rc, 0, sizeof(rc));
    rc.a = a;
    rc.d = d;
    rc.prog = prog;

    /* detect duplicate top-level names (fns + globals) */
    for (size_t i = 0; i < prog->decls.len; i++) {
        Decl *x = vec_get(&prog->decls, i);
        for (size_t j = i + 1; j < prog->decls.len; j++) {
            Decl *y = vec_get(&prog->decls, j);
            if (sv_eq(x->name, y->name))
                diag_error(d, x->pos, "redefinition of '%.*s'", SV_ARG(x->name));
        }
    }

    for (size_t i = 0; i < prog->decls.len; i++) {
        Decl *decl = vec_get(&prog->decls, i);
        if (decl->kind == DECL_FN)
            resolve_fn(&rc, decl);
        else
            resolve_global(&rc, decl);
    }

    check_main(&rc);
    return (int)d->has_error;
}
