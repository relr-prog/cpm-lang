#!/usr/bin/env pwsh
# Root build: build the bootstrap compiler, then refresh test baselines.
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'scripts\build-bootstrap.ps1')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "bootstrap compiler ready. run: scripts/run-tests.ps1"