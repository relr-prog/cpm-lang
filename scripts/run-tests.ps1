#!/usr/bin/env pwsh
# Test runner for the bootstrap compiler.
#
# Conventions under bootstrap/tests/:
#   <name>.cpm        source
#   <name>.expect     (e2e) expected stdout of compiled program
#   <name>.stderr     (diag) exact expected compiler stderr (error tests)
#
# Flags:
#   -Build        rebuild bootstrap first (default)
#   -NoBuild      skip rebuild
#   -WriteBaselines  (re)create missing .expect/.stderr baselines from current
#                    behavior. Review baselines before committing them.
#   -Filter <glob> only run tests matching <glob>
param(
    [switch]$NoBuild,
    [switch]$WriteBaselines,
    [string]$Filter = '*'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$boot = Join-Path $root 'build-out\cpm-boot.exe'
$testDir = Join-Path $root 'bootstrap\tests'
$tmpDir = Join-Path $root 'build-out\_tmp'

$RunBegin = Get-Date
$total = 0; $passed = 0; $failed = 0
$failures = [System.Collections.Generic.List[string]]::new()

if (-not $NoBuild) {
    & (Join-Path $PSScriptRoot 'build-bootstrap.ps1')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
if (-not (Test-Path $boot)) { throw "boot compiler missing: $boot (run build.ps1)" }

New-Item -ItemType Directory -Force -Path $tmpDir | Out-Null

function Read-Raw([string]$Path) {
    if (-not (Test-Path $Path)) { return $null }
    return [System.IO.File]::ReadAllText($Path)
}

function Write-Raw([string]$Path, [string]$Content) {
    [System.IO.File]::WriteAllText($Path, $Content)
}

$tests = Get-ChildItem -Path $testDir -Filter *.cpm |
    Where-Object { $_.Name -like $Filter }

foreach ($t in $tests) {
    $total++
    $base = $t.FullName -replace '\.cpm$', ''
    $name = $t.BaseName
    $expectPath = "$base.expect"
    $stderrPath = "$base.stderr"
    $isDiag = $name.StartsWith('neg_')
    $isE2e = -not $isDiag

    if ($isE2e) {
        $cPath = Join-Path $tmpDir "$name.c"
        $exePath = Join-Path $tmpDir "$name.exe"
        $outPath = Join-Path $tmpDir "$name.out"

        & $boot $t.FullName -o $cPath 2> (Join-Path $tmpDir "$name.boot.err")
        if ($LASTEXITCODE -ne 0) {
            $failures.Add("$name : cpm-boot failed unexpectedly")
            $failed++; continue
        }
        $cc = (Get-Command clang -ErrorAction SilentlyContinue)
        if (-not $cc) { $cc = Get-Command gcc }
        & $cc.Source -std=c11 -Wall -o $exePath $cPath
        if ($LASTEXITCODE -ne 0) {
            $failures.Add("$name : host C compile failed")
            $failed++; continue
        }
        & $exePath 1> $outPath
        $exitCode = $LASTEXITCODE
        $got = Read-Raw $outPath
        $hasBaseline = Test-Path $expectPath
        if ($WriteBaselines) { Write-Raw $expectPath $got; $hasBaseline = $true }
        $want = Read-Raw $expectPath
        if (-not $hasBaseline) {
            $failures.Add("$name : missing baseline; run with -WriteBaselines")
            $failed++
        } elseif ($got -ne $want -or $exitCode -ne 0) {
            $failures.Add("$name : stdout or exit mismatch (exit=$exitCode)")
            $failed++
        } else {
            $passed++
        }
    } elseif ($isDiag) {
        $errPath = Join-Path $tmpDir "$name.err"
        & $boot $t.FullName 2> $errPath 1> (Join-Path $tmpDir "$name.stdout")
        $exitCode = $LASTEXITCODE
        $gotRel = (Read-Raw $errPath).Replace("$root\", '')
        if ($WriteBaselines) { Write-Raw $stderrPath $gotRel }
        $want = Read-Raw $stderrPath
        if ($exitCode -eq 0 -or $gotRel -ne $want) {
            $failures.Add("$name : compiler must fail (exit=$exitCode) with exact stderr")
            $failed++
        } else {
            $passed++
        }
    }
}

$RunEnd = (Get-Date)
$secs = [math]::Round(($RunEnd - $RunBegin).TotalSeconds, 1)

if ($failed -eq 0) {
    Write-Host "ALL PASS ($passed/$total) in ${secs}s" -ForegroundColor Green
    exit 0
} else {
    Write-Host "FAILED: $failed/$total (passed $passed) in ${secs}s" -ForegroundColor Red
    foreach ($f in $failures) { Write-Host "  - $f" -ForegroundColor Red }
    exit 1
}