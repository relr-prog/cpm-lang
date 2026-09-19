#ifndef CPM_PARSER_H
#define CPM_PARSER_H

#include "../diag.h"
#include "../ast.h"

/* Parse a whole translation unit. On any error a diagnostic is recorded and
 * `*has_error` is set; a (possibly partial) Program is still returned.
 * The Program and all nodes are allocated in `a`. */
Program *parse_program(Source src, Diag *d, Arena *a);

#endif /* CPM_PARSER_H */