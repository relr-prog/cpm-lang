#define _POSIX_C_SOURCE 200809L
#include "resolve.h"

typedef struct Scope {
    struct Scope *parent;
    VEC           names; /* Vec of char* (arena-owned) */
    VEC           is_var; /* not used for now; single namespace phase1 */
} Scope;

typedef struct {
    Arena   *a;
    Diag    *d;
    Program *prog;
    Scope   *fn_scope;   /* root scope for current fn (params) */
    Scope   *scope;      /* current innermost scope */
    Type     cur_ret;    /* current function return type */
} RC;

typedef struct { SV name; int arity; } Intrinsic;

static const Intrinsic INTRINSICS[] = {
    { { "print", 5 }, 1 },      { { "println", 7 }, 1 },
    { { "print_i32", 9 }, 1 },  { { "println_i32", 11 }, 1 },
    { { "print_i64", 9 }, 1 },  { { "println_i64", 11 }, 1 },
    { { "print_f64", 9 }, 1 },  { { "println_f64", 11 }, 1 },
    { { "print_bool", 10 }, 1 }, { { "println_bool", 12 }, 1 },
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

static Scope *scope_push(RC *rc, Scope *parent) {
    Scope *s = arena_alloc(rc->a, sizeof(Scope), 8);
    memset(s, 0, sizeof(*s));
    s->parent = parent;
    return s;
}

static bool scope_def(RC *rc, Scope *s, SV name, size_t pos) {
    for (size_t i = 0; i < s->names.len; i++) {
        const char *p = (const char *)vec_get(&s->names, i);
        if (strlen(p) == name.len && memcmp(p, name.ptr, name.len) == 0) {
            diag_error(rc->d, pos, "redefinition of variable '%.*s'",
                       SV_ARG(name));
            return false;
        }
    }
    vec_push(&s->names, sv_to_cstr_arena(rc->a, name));
    return true;
}

/* returns the decl of a global var that matches `name`, or NULL */
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

/* find a variable: locals first, then globals; fns rejected */
static bool lookup_var(RC *rc, Scope *s, SV name, size_t pos, bool is_read) {
    for (Scope *sc = s; sc; sc = sc->parent) {
        for (size_t i = 0; i < sc->names.len; i++) {
            const char *p = (const char *)vec_get(&sc->names, i);
            if (strlen(p) == name.len && memcmp(p, name.ptr, name.len) == 0)
                return true;
        }
    }
    if (find_global_var(rc, name)) return true;
    if (find_fn(rc, name))
        diag_error(rc->d, pos, "function '%.*s' used as a variable",
                   SV_ARG(name));
    else
        diag_error(rc->d, pos, "use of undeclared %s '%.*s'",
                   is_read ? "variable" : "identifier", SV_ARG(name));
    return false;
}

static void resolve_expr(RC *rc, Scope *s, Expr *e);

static void resolve_args(RC *rc, Scope *s, Expr *e) {
    for (size_t i = 0; i < e->as.call.args.len; i++)
        resolve_expr(rc, s, vec_get(&e->as.call.args, i));
}

static void resolve_call(RC *rc, Scope *s, Expr *e) {
    SV callee = e->as.call.callee;
    size_t nargs = e->as.call.args.len;

    Decl *fn = find_fn(rc, callee);
    if (fn) {
        if (fn->fn_params.len != nargs) {
            diag_error(rc->d, e->pos, "call to '%.*s' expects %zu argument%s, "
                       "got %zu",
                       SV_ARG(callee), fn->fn_params.len,
                       fn->fn_params.len == 1 ? "" : "s", nargs);
        }
        resolve_args(rc, s, e);
        return;
    }

    int iarity = 0;
    if (resolve_is_intrinsic(callee, &iarity)) {
        if (iarity != (int)nargs) {
            diag_error(rc->d, e->pos, "call to intrinsic '%.*s' expects %d "
                       "argument%s, got %zu",
                       SV_ARG(callee), iarity, iarity == 1 ? "" : "s", nargs);
        }
        resolve_args(rc, s, e);
        return;
    }

    if (find_global_var(rc, callee))
        diag_error(rc->d, e->pos, "cannot call variable '%.*s'",
                   SV_ARG(callee));
    else
        diag_error(rc->d, e->pos, "call to unknown function '%.*s'",
                   SV_ARG(callee));
    resolve_args(rc, s, e);
}

static void resolve_expr(RC *rc, Scope *s, Expr *e) {
    if (!e) return;
    switch (e->kind) {
    case EX_IDENT:
        lookup_var(rc, s, e->as.ident, e->pos, true);
        break;
    case EX_CALL:
        resolve_call(rc, s, e);
        break;
    case EX_BINOP:
        resolve_expr(rc, s, e->as.bin.l);
        resolve_expr(rc, s, e->as.bin.r);
        break;
    case EX_UNOP:
        resolve_expr(rc, s, e->as.un.e);
        break;
    default:
        break; /* literals */
    }
}

static Stmt *resolve_stmt(RC *rc, Scope *s, Stmt *st) {
    switch (st->kind) {
    case ST_BLOCK: {
        Scope *inner = scope_push(rc, s);
        for (size_t i = 0; i < st->as.block.stmts.len; i++)
            resolve_stmt(rc, inner, vec_get(&st->as.block.stmts, i));
        return st;
    }
    case ST_IF:
        resolve_expr(rc, s, st->as.iff.cond);
        resolve_stmt(rc, s, st->as.iff.then);
        if (st->as.iff.els) resolve_stmt(rc, s, st->as.iff.els);
        return st;
    case ST_WHILE:
        resolve_expr(rc, s, st->as.whl.cond);
        resolve_stmt(rc, s, st->as.whl.body);
        return st;
    case ST_VAR: {
        scope_def(rc, s, st->as.var.name, st->pos);
        if (st->as.var.init) resolve_expr(rc, s, st->as.var.init);
        return st;
    }
    case ST_ASSIGN:
        lookup_var(rc, s, st->as.assign.name, st->pos, false);
        if (st->as.assign.rhs) resolve_expr(rc, s, st->as.assign.rhs);
        return st;
    case ST_RETURN:
        if (st->as.ret.has_val) {
            if (rc->cur_ret.kind == TY_VOID)
                diag_error(rc->d, st->pos,
                           "cannot return a value from a 'void' function");
            resolve_expr(rc, s, st->as.ret.val);
        } else {
            if (rc->cur_ret.kind != TY_VOID)
                diag_error(rc->d, st->pos,
                           "missing return value (function returns %s)",
                           type_name(rc->cur_ret.kind));
        }
        return st;
    case ST_EXPR:
        resolve_expr(rc, s, st->as.expr.expr);
        return st;
    }
    return st;
}

static void resolve_fn(RC *rc, Decl *fn) {
    Scope *root = scope_push(rc, NULL);
    rc->fn_scope = root;
    rc->scope = root;
    rc->cur_ret = fn->fn_ret;

    for (size_t i = 0; i < fn->fn_params.len; i++) {
        Param *p = vec_get(&fn->fn_params, i);
        scope_def(rc, root, p->name, p->pos);
    }
    resolve_stmt(rc, root, &fn->fn_body);
    rc->scope = NULL;
    rc->fn_scope = NULL;
}

static void resolve_global(RC *rc, Decl *g) {
    if (g->g_init) resolve_expr(rc, NULL, g->g_init);
}

static void check_main(RC *rc) {
    Decl *main = find_fn(rc, sv_make("main"));
    if (!main) {
        diag_error(rc->d, 0, "no 'fn main' entry point found");
        return;
    }
    if (main->fn_ret.kind != TY_I32)
        diag_error(rc->d, main->pos, "entry point 'main' must return 'i32', "
                   "got '%s'", type_name(main->fn_ret.kind));
    if (main->fn_params.len != 0)
        diag_error(rc->d, main->pos, "entry point 'main' must take no "
                   "parameters");
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
                diag_error(d, x->pos, "redefinition of '%.*s'",
                           SV_ARG(x->name));
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