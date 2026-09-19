# AGENTS.md

Repo: **cpm-lang** — the C+- (cpm) language. Source lives in `bootstrap/` (C
bootstrap compiler) plus `scripts/`, `docs/`.

## Working directory rule
This session's working repo is `C:\Users\Hype\dev\cpm-lang` (NOT the default
`C:\Windows\System32`). Always `Push-Location` there before git/build commands.

## Build & test (what actually works, verified)
```powershell
scripts\build-bootstrap.ps1          # builds build-out\cpm-boot.exe
scripts\run-tests.ps1                # full suite (builds first unless -NoBuild)
scripts\run-tests.ps1 -Filter "str_*" -WriteBaselines   # (re)create missing baselines
scripts\run-tests.ps1 -NoBuild       # skip rebuild, just run suite
```
- Test runner: `bootstrap/tests/*.cpm`. Positive tests have `<name>.expect`
  (stdout). Negative tests `neg_*.cpm` have `<name>.stderr` (expected compiler
  diag, exact match). `-WriteBaselines` writes the missing baseline files.
- Current expected state: **ALL PASS**. Do not ship a change that regresses the
  suite.

## Version scheme (scripts\gen-version.ps1)
Format `YYYY.MMM.DD.DOW.RND` e.g. `26.226.9.7SU.HJ3`. Rightmost component is
build/random; bump manually for a release, then tag `cpm-<version>` and push
main + tag together:
```powershell
git tag "cpm-26.226.9.7SU.HJ3"
git push origin main "cpm-26.226.9.7SU.HJ3"
```

## Environment gotchas
- `\$` suffix is POWERFUL in this repo (value intrinsics). Don't assume: verify
  with a small probe.
- Git identity is NOT configured globally in this environment — prior history
  uses `relr-prog <relr-prog@users.noreply.github.com>` or
  `relr-prog <geraaaaan@gmail.com>`. Set repo-local config if needed before
  committing:
  `git config user.name relr-prog` / `git config user.email ...`
- When reading files that the shell mangles: write a small pwsh script that
  dumps file contents into `%TEMP%\opencode\*.txt`, then read that txt.

## Work rules (to keep sessions fast)
- Verify precise facts with one small probe; don't guess.
- When output is mangled/cross-contaminated: stop and dump to a clean txt file,
  then read it. Don't keep re-reading the corrupted source.
- Take a backup ("yakin source on disk = apa yang ku diffs-old") before big
  edits.
- Ask the user before guessing user intent (identity, tags, naming).
