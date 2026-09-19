#define _POSIX_C_SOURCE 200809L
#include "../codegen.h"
#include <inttypes.h>
#include <stdio.h>

typedef struct {
    Program *prog;
    Diag    *d;
    Arena   *a;
    SB      *out;
} CG;

static const char *c_type(Type t) {
    switch (t.kind) {
    case TY_I8: return "int8_t";
    case TY_I16: return "int16_t";
    case TY_I32: return "int32_t";
    case TY_I64: return "int64_t";
    case TY_U8: return "uint8_t";
    case TY_U16: return "uint16_t";
    case TY_U32: return "uint32_t";
    case TY_U64: return "uint64_t";
    case TY_F32: return "float";
    case TY_F64: return "double";
    case TY_BOOL: return "bool";
    case TY_STR: return "const char *";
    case TY_VOID: return "void";
    }
    return "void";
}

static void emit_indent(SB *out, int depth) {
    for (int i = 0; i < depth; i++) sb_appendcstr(out, "  ");
}

static void emit_c_string(SB *out, SV s) {
    sb_append_char(out, '"');
    for (size_t i = 0; i < s.len; i++) {
        char c = s.ptr[i];
        switch (c) {
        case '"': sb_appendcstr(out, "\\\""); break;
        case '\\': sb_appendcstr(out, "\\\\"); break;
        case '\n': sb_appendcstr(out, "\\n"); break;
        case '\t': sb_appendcstr(out, "\\t"); break;
        case '\r': sb_appendcstr(out, "\\r"); break;
        case '\0': sb_appendcstr(out, "\\0"); break;
        default:
            if ((unsigned char)c < 0x20)
                sb_printf(out, "\\%03o", (unsigned char)c);
            else
                sb_append_char(out, c);
            break;
        }
    }
    sb_append_char(out, '"');
}

static bool cg_is_user_fn(CG *cg, SV name) {
    for (size_t i = 0; i < cg->prog->decls.len; i++) {
        Decl *d = vec_get(&cg->prog->decls, i);
        if (d->kind == DECL_FN && sv_eq(d->name, name)) return true;
    }
    return false;
}

static void cg_expr(CG *cg, Expr *e, SB *out);

/* printf format for a named intrinsic; NULL if not an intrinsic.
 * '%s' => str arg, '%d'/%lld/%g => numeric arg, '%b' => bool arg. */
static const char *cg_intrinsic_fmt(const SV callee) {
    if (sv_eq(callee, (SV){ "print", 5 })) return "%s";
    if (sv_eq(callee, (SV){ "println", 7 })) return "%s\n";
    if (sv_eq(callee, (SV){ "print_i32", 9 })) return "%d";
    if (sv_eq(callee, (SV){ "println_i32", 11 })) return "%d\n";
    if (sv_eq(callee, (SV){ "print_i64", 9 })) return "%lld";
    if (sv_eq(callee, (SV){ "println_i64", 11 })) return "%lld\n";
    if (sv_eq(callee, (SV){ "print_f64", 9 })) return "%g";
    if (sv_eq(callee, (SV){ "println_f64", 11 })) return "%g\n";
    if (sv_eq(callee, (SV){ "print_bool", 10 })) return "%b";
    if (sv_eq(callee, (SV){ "println_bool", 12 })) return "%b\n";
    return NULL;
}

/* Substitute the intrinsic's %s/%d/%b format with the proper printf literal
 * and emit a printf() call. `argkind` returns the expected arg type. */
static void cg_intrinsic_call(CG *cg, Expr *e, const char *fmt, SB *out) {
    sb_appendcstr(out, "printf(");
    /* translate our format into a C printf format string */
    SB fsb;
    sb_init(&fsb);
    for (const char *p = fmt; *p; p++) {
        if (*p == '%') {
            char c = p[1];
            switch (c) {
            case 'b':
                sb_appendcstr(&fsb, "%s");
                p++;
                break;
            case 's': sb_appendcstr(&fsb, "%s"); p++; break;
            case 'd': sb_appendcstr(&fsb, "%d"); p++; break;
            case 'l': sb_appendcstr(&fsb, "%lld"); p += 3; break;
            case 'g': sb_appendcstr(&fsb, "%g"); p++; break;
            case '\n': sb_append_char(&fsb, '\n'); p++; break;
            default: sb_append_char(&fsb, *p); break;
            }
        } else if (*p == '\n') {
            sb_append_char(&fsb, '\n');
        } else {
            sb_append_char(&fsb, *p);
        }
    }
    emit_c_string(out, (SV){ sb_cstr(&fsb), fsb.len });
    sb_free(&fsb);

    sb_appendcstr(out, ", ");
    Expr *arg = vec_get(&e->as.call.args, 0);
    bool is_bool = strchr(fmt, 'b') != NULL;
    if (is_bool) {
        sb_append_char(out, '(');
        cg_expr(cg, arg, out);
        sb_appendcstr(out, ") ? \"true\" : \"false\"");
    } else {
        cg_expr(cg, arg, out);
    }
    sb_append_char(out, ')');
}

static void cg_expr(CG *cg, Expr *e, SB *out) {
    if (!e) return;
    switch (e->kind) {
    case EX_INT:
        sb_printf(out, "%" PRIu64, e->as.ival);
        break;
    case EX_FLOAT: {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.17g", e->as.fval);
        bool has_dot = strchr(buf, '.') || strchr(buf, 'e') || strchr(buf, 'E');
        sb_appendcstr(out, buf);
        if (!has_dot) sb_appendcstr(out, ".0");
        break;
    }
    case EX_BOOL:
        sb_appendcstr(out, e->as.bval ? "true" : "false");
        break;
    case EX_STR:
        emit_c_string(out, e->as.str);
        break;
    case EX_IDENT:
        sb_appendv(out, e->as.ident);
        break;
    case EX_CALL: {
        SV callee = e->as.call.callee;
        const char *fmt = cg_is_user_fn(cg, callee) ? NULL
                                                    : cg_intrinsic_fmt(callee);
        if (fmt) {
            cg_intrinsic_call(cg, e, fmt, out);
            break;
        }
        sb_printf(out, "%.*s(", SV_ARG(callee));
        for (size_t i = 0; i < e->as.call.args.len; i++) {
            if (i) sb_appendcstr(out, ", ");
            cg_expr(cg, vec_get(&e->as.call.args, i), out);
        }
        sb_append_char(out, ')');
        break;
    }
    case EX_UNOP:
        sb_append_char(out, '(');
        sb_append_char(out, e->as.un.op == UN_NEG ? '-' : '!');
        cg_expr(cg, e->as.un.e, out);
        sb_append_char(out, ')');
        break;
    case EX_BINOP: {
        const char *op = NULL;
        switch (e->as.bin.op) {
        case OP_ADD: op = "+"; break;
        case OP_SUB: op = "-"; break;
        case OP_MUL: op = "*"; break;
        case OP_DIV: op = "/"; break;
        case OP_MOD: op = "%"; break;
        case OP_EQ: op = "=="; break;
        case OP_NE: op = "!="; break;
        case OP_LT: op = "<"; break;
        case OP_LE: op = "<="; break;
        case OP_GT: op = ">"; break;
        case OP_GE: op = ">="; break;
        case OP_LAND: op = "&&"; break;
        case OP_LOR: op = "||"; break;
        }
        sb_append_char(out, '(');
        cg_expr(cg, e->as.bin.l, out);
        sb_printf(out, " %s ", op);
        cg_expr(cg, e->as.bin.r, out);
        sb_append_char(out, ')');
        break;
    }
    }
}

static void cg_stmt(CG *cg, Stmt *st, SB *out, int depth);

static void cg_block(CG *cg, Stmt *blk, SB *out, int depth) {
    sb_appendcstr(out, "{\n");
    for (size_t i = 0; i < blk->as.block.stmts.len; i++) {
        cg_stmt(cg, vec_get(&blk->as.block.stmts, i), out, depth + 1);
        sb_append_char(out, '\n');
    }
    emit_indent(out, depth);
    sb_append_char(out, '}');
}

static void cg_stmt(CG *cg, Stmt *st, SB *out, int depth) {
    emit_indent(out, depth);
    switch (st->kind) {
    case ST_BLOCK:
        cg_block(cg, st, out, depth);
        break;
    case ST_IF:
        sb_appendcstr(out, "if (");
        cg_expr(cg, st->as.iff.cond, out);
        sb_appendcstr(out, ") ");
        cg_block(cg, st->as.iff.then, out, depth);
        if (st->as.iff.els) {
            sb_appendcstr(out, " else ");
            if (st->as.iff.els->kind == ST_IF)
                cg_stmt(cg, st->as.iff.els, out, depth);
            else
                cg_block(cg, st->as.iff.els, out, depth);
        }
        break;
    case ST_WHILE:
        sb_appendcstr(out, "while (");
        cg_expr(cg, st->as.whl.cond, out);
        sb_appendcstr(out, ") ");
        cg_block(cg, st->as.whl.body, out, depth);
        break;
    case ST_BREAK:
        sb_appendcstr(out, "break;");
        break;
    case ST_CONTINUE:
        sb_appendcstr(out, "continue;");
        break;
    case ST_RETURN:
        sb_appendcstr(out, "return");
        if (st->as.ret.has_val) {
            sb_append_char(out, ' ');
            cg_expr(cg, st->as.ret.val, out);
        }
        sb_append_char(out, ';');
        break;
    case ST_VAR:
        sb_appendcstr(out, c_type(st->as.var.type));
        sb_appendcstr(out, " ");
        sb_appendv(out, st->as.var.name);
        if (st->as.var.init) {
            sb_appendcstr(out, " = ");
            cg_expr(cg, st->as.var.init, out);
        }
        sb_append_char(out, ';');
        break;
    case ST_ASSIGN:
        sb_appendv(out, st->as.assign.name);
        sb_appendcstr(out, " = ");
        if (st->as.assign.rhs) cg_expr(cg, st->as.assign.rhs, out);
        sb_append_char(out, ';');
        break;
    case ST_EXPR:
        cg_expr(cg, st->as.expr.expr, out);
        sb_append_char(out, ';');
        break;
    }
}

static void cg_fn_params(CG *cg, Decl *fn, SB *out) {
    (void)cg;
    for (size_t i = 0; i < fn->fn_params.len; i++) {
        if (i) sb_appendcstr(out, ", ");
        Param *p = vec_get(&fn->fn_params, i);
        sb_appendcstr(out, c_type(p->type));
        sb_appendcstr(out, " ");
        sb_appendv(out, p->name);
    }
}

static bool fn_is_main(Decl *d) {
    return d->kind == DECL_FN && sv_eq(d->name, (SV){ "main", 4 });
}

static void cg_emit_prototypes(CG *cg, SB *out) {
    for (size_t i = 0; i < cg->prog->decls.len; i++) {
        Decl *d = vec_get(&cg->prog->decls, i);
        if (d->kind != DECL_FN || fn_is_main(d)) continue;
        sb_printf(out, "%s %.*s(", c_type(d->fn_ret), SV_ARG(d->name));
        cg_fn_params(cg, d, out);
        sb_appendcstr(out, ");\n");
    }
}

static void cg_emit_globals(CG *cg, SB *out) {
    for (size_t i = 0; i < cg->prog->decls.len; i++) {
        Decl *d = vec_get(&cg->prog->decls, i);
        if (d->kind != DECL_GLOBAL) continue;
        sb_appendcstr(out, "static ");
        sb_appendcstr(out, c_type(d->g_type));
        sb_appendcstr(out, " ");
        sb_appendv(out, d->name);
        if (d->g_init) {
            sb_appendcstr(out, " = ");
            cg_expr(cg, d->g_init, out);
        }
        sb_appendcstr(out, ";\n");
    }
}

static void cg_emit_defs(CG *cg, SB *out) {
    for (size_t i = 0; i < cg->prog->decls.len; i++) {
        Decl *d = vec_get(&cg->prog->decls, i);
        if (d->kind != DECL_FN) continue;
        bool is_main = fn_is_main(d);
        if (is_main) {
            sb_appendcstr(out, "int main(void) ");
            cg_block(cg, &d->fn_body, out, 0);
            sb_appendcstr(out, "\n");
        } else {
            sb_printf(out, "%s %.*s(", c_type(d->fn_ret), SV_ARG(d->name));
            cg_fn_params(cg, d, out);
            sb_appendcstr(out, ") ");
            cg_block(cg, &d->fn_body, out, 0);
            sb_appendcstr(out, "\n\n");
        }
    }
}

int codegen_program(Program *prog, Diag *d, Arena *a, SB *out) {
    CG cg;
    memset(&cg, 0, sizeof(cg));
    cg.prog = prog;
    cg.d = d;
    cg.a = a;
    cg.out = out;

    sb_appendcstr(out, "/* generated by cpm-boot -- do not edit */\n");
    sb_appendcstr(out, "#include <stdbool.h>\n");
    sb_appendcstr(out, "#include <stdint.h>\n");
    sb_appendcstr(out, "#include <stdio.h>\n\n");

    cg_emit_prototypes(&cg, out);
    cg_emit_globals(&cg, out);
    sb_append_char(out, '\n');
    cg_emit_defs(&cg, out);

    return (int)d->has_error;
}