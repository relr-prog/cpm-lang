#ifndef CPM_UTIL_H
#define CPM_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Arena (bump) allocator ---------- */
typedef struct Arena Arena;
struct Arena;

void  arena_init(Arena **a);
void  arena_free(Arena *a);
void *arena_alloc(Arena *a, size_t size, size_t align);

/* ---------- String view ---------- */
typedef struct {
    const char *ptr; /* not NUL terminated, includes len */
    size_t      len;
} SV;

#define SV_FMT "%.*s"
#define SV_ARG(sv) (int)(sv).len, (sv).ptr

#define sv_make(s) ((SV){ (s), sizeof(s) - 1 })
SV  sv_from_cstr(const char *s);
SV  sv_sub(SV s, size_t off, size_t len);
bool sv_eq(SV a, SV b);
bool sv_eq_cstr(SV a, const char *b);
char *sv_to_cstr_arena(Arena *a, SV s);

/* ---------- String builder ---------- */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} SB;

void sb_init(SB *sb);
void sb_free(SB *sb);
void sb_appendv(SB *sb, SV s);
void sb_appendcstr(SB *sb, const char *s);
void sb_append_char(SB *sb, char c);
void sb_printf(SB *sb, const char *fmt, ...);
const char *sb_cstr(SB *sb); /* NUL-terminates */

/* ---------- Generic pointer vector ---------- */
typedef struct {
    void **data;
    size_t len;
    size_t cap;
} VEC;

void vec_init(VEC *v);
void vec_push(VEC *v, void *item);
void *vec_get(VEC *v, size_t i);
void vec_set(VEC *v, size_t i, void *item);
void vec_free(VEC *v);

#endif /* CPM_UTIL_H */