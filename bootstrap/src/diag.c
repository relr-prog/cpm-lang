#define _POSIX_C_SOURCE 200809L
#include "diag.h"
#include <stdarg.h>
#include <stdio.h>

void diag_init(Diag *d, Source src) {
    d->src = src;
    vec_init(&d->msg);
    d->has_error = false;
}

void diag_free(Diag *d) { vec_free(&d->msg); }

static char *fmt_alloc(const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *s = malloc((size_t)n + 1);
    vsnprintf(s, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return s;
}

typedef struct {
    const char *kind; /* "error", "warning", "note" */
    size_t      pos;
    char       *text; /* formatted message (no prefix) */
} Msg;

static bool line_col(const Source *src, size_t pos, size_t *line_out,
                     size_t *col_out) {
    if (pos > src_len(src)) return false;
    size_t line = 0;
    size_t line_start = 0;
    size_t i = 0;
    while (i < (size_t)src_len(src) && i < pos) {
        if (src_src(src)[i] == '\n') {
            line++;
            line_start = i + 1;
        }
        i++;
    }
    *line_out = line + 1;
    *col_out = pos - line_start + 1;
    return true;
}

void diag_error(Diag *d, size_t pos, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    Msg *m = malloc(sizeof(Msg));
    m->kind = "error";
    m->pos = pos;
    m->text = fmt_alloc(fmt, ap);
    va_end(ap);
    vec_push(&d->msg, m);
    d->has_error = true;
}

void diag_warn(Diag *d, size_t pos, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    Msg *m = malloc(sizeof(Msg));
    m->kind = "warning";
    m->pos = pos;
    m->text = fmt_alloc(fmt, ap);
    va_end(ap);
    vec_push(&d->msg, m);
}

void diag_note(Diag *d, size_t pos, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    Msg *m = malloc(sizeof(Msg));
    m->kind = "note";
    m->pos = pos;
    m->text = fmt_alloc(fmt, ap);
    va_end(ap);
    vec_push(&d->msg, m);
}

int diag_report(Diag *d) {
    size_t nerr = 0;
    for (size_t i = 0; i < d->msg.len; i++) {
        Msg *m = vec_get(&d->msg, i);
        if (strcmp(m->kind, "error") == 0) nerr++;

        size_t line = 0, col = 0;
        bool ok = line_col(&d->src, m->pos, &line, &col);

        if (ok) {
            fprintf(stderr, "%s:%zu:%zu: %s: %s\n",
                    d->src.name, line, col, m->kind, m->text);
            /* render source line + caret */
            size_t line_start = m->pos - (col - 1);
            size_t line_end = line_start;
            while (line_end < src_len(&d->src) && src_src(&d->src)[line_end] != '\n')
                line_end++;
            fprintf(stderr, "    ");
            fwrite(src_src(&d->src) + line_start, 1, line_end - line_start, stderr);
            fprintf(stderr, "\n");
            fprintf(stderr, "    ");
            for (size_t c = 0; c < col - 1; c++) fputc(' ', stderr);
            fputc('^', stderr);
            fprintf(stderr, "\n");
        } else {
            fprintf(stderr, "%s: %s: %s\n", d->src.name, m->kind, m->text);
        }
        free(m->text);
        free(m);
    }
    d->msg.len = 0;
    return (int)nerr;
}