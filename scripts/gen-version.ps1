#!/usr/bin/env pwsh
# ============================================================================
# C+- version generator.
#
# Scheme:  <4a>.<3a>.<2a>.<1a>.<a>   (read right-to-left for significance)
#
#   4a  year of the language update   (e.g. 26  = 2-digit year)
#   3a  FIXED build constant          = 226 (never change)
#   2a  build month, Jan=1 .. Dec=12  (e.g. 9)
#   1a  build day-of-week code:
#          Minggu=1M   Senin=2SN   Selasa=3SA  Rabu=4RU
#          Kamis=5KS   Jumat=6JT   Sabtu=7SU
#   a   random suffix (acak), uppercase alnum
#
#   Example built on Saturday 2026-09-19:
#     26.226.9.7SU.K7QX
#
# The rightmost component changes most often (random), the leftmost least
# often (year, 2-digit). The value is computed from the machine's local clock
# at build time.
# ============================================================================
param(
    # Optional: also persist the version to a file (used by build scripts).
    [string]$OutputFile
)

$date = Get-Date

$dayMap = @{
    'Sunday'    = '1M'
    'Monday'    = '2SN'
    'Tuesday'   = '3SA'
    'Wednesday' = '4RU'
    'Thursday'  = '5KS'
    'Friday'    = '6JT'
    'Saturday'  = '7SU'
}

$dayCode = $dayMap[$date.DayOfWeek.ToString()]
if (-not $dayCode) { throw "unmapped day: $($date.DayOfWeek)" }

$month = [int]$date.Month
# 4a = year of the language update, shortened to 2 digits (e.g. 2026 -> 26)
$year = [int]($date.Year % 100)
$fixed = 226

# random (acak) suffix: 3 uppercase alphanumeric chars, no confusables (I,O)
$randChars = 'ABCDEFGHJKLMNPQRSTUVWXYZ0123456789'
$randomSuffix = -join (1..3 | ForEach-Object {
    $randChars[(Get-Random -Minimum 0 -Maximum $randChars.Length)]
})

$version = "$year.$fixed.$month.$dayCode.$randomSuffix"

if ($OutputFile) {
    $parent = Split-Path -Parent $OutputFile
    if ($parent -and -not (Test-Path $parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    [System.IO.File]::WriteAllText($OutputFile, $version)
}

Write-Output $version