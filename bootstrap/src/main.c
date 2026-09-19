#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "parser.h"
#include "resolve.h"
#include "codegen.h"
#include "util/util.h"

#ifndef CPM_VERSION
#define CPM_VERSION "dev"
#endif

static void usage(void) {
    fprintf(stderr,
            "cpm-boot - C+- bootstrap compiler (C backend)\n"
            "\n"
            "usage: cpm-boot <file.cpm> [-o out.c]\n"
            "\n"
            "  No -o : generated C is written to stdout.\n"
            "  flags : -o <file>     write generated C to <file>\n"
            "          -v,--version  print compiler version and exit\n"
            "\n");
}

static char *read_entire_file(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    *len_out = got;
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    const char *input = NULL;
    const char *output = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0 ||
                   strcmp(argv[i], "--version") == 0) {
            printf("cpm-boot (C+-) %s\n", CPM_VERSION);
            return 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "cpm-boot: unknown option '%s'\n", argv[i]);
            return 2;
        } else if (!input) {
            input = argv[i];
        } else {
            fprintf(stderr, "cpm-boot: unexpected extra argument '%s'\n",
                    argv[i]);
            return 2;
        }
    }
    if (!input) {
        usage();
        return 2;
    }

    size_t len = 0;
    char *src = read_entire_file(input, &len);
    if (!src) return 1;

    Source s = { input, src, len };
    Diag diag;
    diag_init(&diag, s);
    Arena *a = NULL;
    arena_init(&a);

    Program *prog = parse_program(s, &diag, a);

    if (diag.has_error) {
        diag_report(&diag);
        diag_free(&diag);
        arena_free(a);
        free(src);
        return 1;
    }

    resolve_program(prog, &diag, a);
    if (diag.has_error) {
        diag_report(&diag);
        diag_free(&diag);
        arena_free(a);
        free(src);
        return 1;
    }

    SB out;
    sb_init(&out);
    codegen_program(prog, &diag, a, &out);
    if (diag.has_error) {
        diag_report(&diag);
        sb_free(&out);
        diag_free(&diag);
        arena_free(a);
        free(src);
        return 1;
    }

    if (output) {
        FILE *f = fopen(output, "wb");
        if (!f) { perror(output); return 1; }
        fwrite(out.data, 1, out.len, f);
        fclose(f);
    } else {
        fwrite(out.data, 1, out.len, stdout);
    }

    sb_free(&out);
    diag_free(&diag);
    arena_free(a);
    free(src);
    return 0;
}