#ifndef CPM_CODEGEN_H
#define CPM_CODEGEN_H

#include "../diag.h"
#include "../ast.h"

/* Translate the (resolved) program into portable C. Appends to `out`.
 * Returns the number of errors reported. */
int codegen_program(Program *prog, Diag *d, Arena *a, SB *out);

#endif /* CPM_CODEGEN_H */