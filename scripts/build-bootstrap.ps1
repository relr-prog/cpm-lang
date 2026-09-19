#!/usr/bin/env pwsh
# Build the C+- bootstrap compiler (written in C) into build-out/cpm-boot.exe
# Requires: clang or gcc on PATH (WinLibs works well).
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$srcDir = Join-Path $root 'bootstrap\src'
$outDir = Join-Path $root 'build-out'
$outExe = Join-Path $outDir 'cpm-boot.exe'

# Generate the C+- version (scheme 4a.3a.2a.1a.a, see gen-version.ps1).
$ver = & (Join-Path $PSScriptRoot 'gen-version.ps1') -OutputFile (Join-Path $outDir 'version.txt') | Select-Object -Last 1
Write-Host "version: $ver"

$cc = $null
foreach ($cand in @('clang', 'gcc')) {
    if (Get-Command $cand -ErrorAction SilentlyContinue) { $cc = $cand; break }
}
if (-not $cc) { throw "No C compiler found (looked for clang/gcc on PATH)." }

$files = Get-ChildItem -Path $srcDir -Recurse -Filter *.c | ForEach-Object { $_.FullName }

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Push-Location (Split-Path -Parent $root)
try {
    & $cc -std=c11 -Wall -Wextra -Wpedantic -Wno-format-truncation `
        -Wno-newline-eof -O1 `
        "-DCPM_VERSION=`"$ver`"" `
        -I (Join-Path $srcDir 'util') `
        $files `
        -o $outExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}

Write-Host "built: $outExe"
exit 0