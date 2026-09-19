#define _POSIX_C_SOURCE 200809L
#include "util.h"
#include <stdarg.h>
#include <stdio.h>

typedef struct Block {
    struct Block *next;
    size_t        used;
    size_t        cap;
    uintmax_t     data[]; /* aligned enough for any type */
} Block;

struct Arena {
    Block *head;
    Block *tail;
};

#define BLOCK_ALIGN  sizeof(uintmax_t)
#define INIT_CAP     8192
#define MAX_CAP      (1u << 20)

void arena_init(Arena **a) {
    *a = calloc(1, sizeof(Arena));
}

void arena_free(Arena *a) {
    if (!a) return;
    Block *b = a->head;
    while (b) {
        Block *next = b->next;
        free(b);
        b = next;
    }
    free(a);
}

static Block *block_new(size_t cap, Block *next) {
    Block *b = malloc(offsetof(Block, data) + cap);
    b->next = next;
    b->used = 0;
    b->cap  = cap;
    return b;
}

void *arena_alloc(Arena *a, size_t size, size_t align) {
    if (align > BLOCK_ALIGN) align = BLOCK_ALIGN;
    if (align == 0) align = BLOCK_ALIGN;

    if (!a->tail)
        a->head = a->tail = block_new(INIT_CAP, NULL);

    Block *b = a->tail;
    size_t aligned = (b->used + align - 1) & ~(align - 1);
    if (aligned + size > b->cap) {
        size_t cap = b->cap * 2;
        if (cap < size + BLOCK_ALIGN) cap = (size + BLOCK_ALIGN) * 2;
        if (cap > MAX_CAP) cap = MAX_CAP;
        if (cap < size + BLOCK_ALIGN) cap = size + BLOCK_ALIGN;
        b = block_new(cap, NULL);
        a->tail->next = b;
        a->tail = b;
        aligned = 0;
    }
    void *p = (char *)b->data + aligned;
    b->used = aligned + size;
    return p;
}

/* ---------- SV ---------- */
SV sv_from_cstr(const char *s) { return (SV){ s, strlen(s) }; }

SV sv_sub(SV s, size_t off, size_t len) {
    if (off > s.len) off = s.len;
    if (len > s.len - off) len = s.len - off;
    return (SV){ s.ptr + off, len };
}

bool sv_eq(SV a, SV b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}

bool sv_eq_cstr(SV a, const char *b) {
    size_t n = strlen(b);
    return a.len == n && (n == 0 || memcmp(a.ptr, b, n) == 0);
}

char *sv_to_cstr_arena(Arena *a, SV s) {
    char *p = arena_alloc(a, s.len + 1, 1);
    memcpy(p, s.ptr, s.len);
    p[s.len] = '\0';
    return p;
}

/* ---------- SB ---------- */
void sb_init(SB *sb) {
    sb->cap = 256;
    sb->len = 0;
    sb->data = malloc(sb->cap);
}

void sb_free(SB *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = sb->cap = 0;
}

static void sb_reserve(SB *sb, size_t extra) {
    if (sb->len + extra + 1 <= sb->cap) return;
    while (sb->len + extra + 1 > sb->cap) sb->cap *= 2;
    sb->data = realloc(sb->data, sb->cap);
}

void sb_appendv(SB *sb, SV s) {
    sb_reserve(sb, s.len);
    memcpy(sb->data + sb->len, s.ptr, s.len);
    sb->len += s.len;
}

void sb_appendcstr(SB *sb, const char *s) { sb_appendv(sb, sv_from_cstr(s)); }

void sb_append_char(SB *sb, char c) {
    sb_reserve(sb, 1);
    sb->data[sb->len++] = c;
}

void sb_printf(SB *sb, const char *fmt, ...) {
    va_list ap;
    va_list ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int need = vsnprintf(NULL, 0, fmt, ap) + 1;
    va_end(ap);
    sb_reserve(sb, (size_t)need);
    vsnprintf(sb->data + sb->len, (size_t)need, fmt, ap2);
    sb->len += (size_t)need - 1;
    va_end(ap2);
}

const char *sb_cstr(SB *sb) {
    sb_reserve(sb, 0);
    sb->data[sb->len] = '\0';
    return sb->data;
}

/* ---------- VEC ---------- */
void vec_init(VEC *v) {
    v->data = NULL;
    v->len = v->cap = 0;
}

void vec_push(VEC *v, void *item) {
    if (v->len == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->data = realloc(v->data, v->cap * sizeof(void *));
    }
    v->data[v->len++] = item;
}

void *vec_get(VEC *v, size_t i) { return v->data[i]; }

void vec_set(VEC *v, size_t i, void *item) { v->data[i] = item; }

void vec_free(VEC *v) {
    free(v->data);
    v->data = NULL;
    v->len = v->cap = 0;
}