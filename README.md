# C+- (Cpm)

A C-like systems programming language with a self-hosting compiler.

## Bootstrap strategy

The compiler is ultimately written in C+- itself. To bootstrap it without a
C+- compiler, we keep a small hand-written compiler in C (`bootstrap/`) that
is *just* capable enough to compile the real compiler sources. The bootstrap
is then discarded once the real compiler can build itself.

```
cpm-boot (C)  --compiles-->  cpm (C+-)  --compiles-->  cpm' (C+-)
                               ^                           |
                               +-- self-hosting: cpm compiles itself --+
```

Cycle:
1. Write bootstrap compiler in C (subset of C+- semantics).
2. Write the real compiler in C+-, compile it with the bootstrap.
3. Use the result to compile itself: `stage1 + source = stage2`.
4. Verify `stage2` behaviorally equals `stage1`; keep `cpm` as the real compiler.

## Layout

```
bootstrap/   hand-written compiler in C (subset only, discardable)
src/         the real compiler, written in C+-       (later phases)
docs/        language spec + internals
scripts/     build / test / bootstrap-cycle automation
```

## Status

Phase 1: repo skeleton + bootstrap compiler subset (lexer, parser, resolve,
C backend), test harness, hello-world end-to-end.