#ifndef CPM_RESOLVE_H
#define CPM_RESOLVE_H

#include "../diag.h"
#include "../ast.h"

/* Name resolution + light semantic checks.
 * Records diagnostics on errors; returns the number of errors. */
int resolve_program(Program *prog, Diag *d, Arena *a);

/* true if `name` is a compiler intrinsic (mapped by the C backend). */
bool resolve_is_intrinsic(const SV name, int *arity);

#endif /* CPM_RESOLVE_H */