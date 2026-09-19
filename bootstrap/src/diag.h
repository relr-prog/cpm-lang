#ifndef CPM_DIAG_H
#define CPM_DIAG_H

#include "../util/util.h"

typedef struct {
    const char *name; /* file name for display */
    const char *src;  /* NUL-terminated source */
    size_t      len;
} Source;

static inline size_t src_len(const Source *src) { return src->len; }
static inline const char *src_src(const Source *src) { return src->src; }

typedef struct {
    Source  src;      /* source this diag belongs to (first one) */
    VEC     msg;      /* Vec of char* finished messages (not yet rendered) */
    bool    has_error;
} Diag;

void diag_init(Diag *d, Source src);
void diag_free(Diag *d);

void diag_error(Diag *d, size_t pos, const char *fmt, ...);
void diag_warn(Diag *d, size_t pos, const char *fmt, ...);
void diag_note(Diag *d, size_t pos, const char *fmt, ...);

/* Print every diag with caret rendering to stderr.
 * Returns number of errors reported. */
int diag_report(Diag *d);

#endif /* CPM_DIAG_H */