<#
    Arbeitsbaum einrichten: AskSin++-Submodul holen und Patches anwenden.
    Windows-Gegenstueck zu scripts/setup.sh -- gleiche Semantik.

    Warum ueberhaupt ein Skript? AskSin++ ist Fremdcode unter CC BY-NC-SA und
    liegt deshalb nicht in diesem Repository, sondern als Submodul. Die eine
    noetige Aenderung daran (siehe NOTICE.md) ist ein Patch, der erst hier auf
    den Arbeitsbaum angewandt wird -- upstream/ wird nie direkt editiert
    (CLAUDE.md §2.2.1).

    Mehrfach aufrufbar: bereits angewandte Patches werden erkannt und
    uebersprungen.

    Aufruf:
        .\scripts\setup.ps1
        .\scripts\setup.ps1 -NoSubmodule
#>
[CmdletBinding()]
param(
    [switch]$NoSubmodule
)

$ErrorActionPreference = 'Stop'

$root     = Split-Path -Parent $PSScriptRoot
$upstream = Join-Path $root 'lib\asksinpp\upstream'
$patchdir = Join-Path $root 'lib\asksinpp\patches'

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Error 'git nicht gefunden. Es wird fuer das Submodul gebraucht.'
}

if (-not $NoSubmodule) {
    Write-Host 'Submodul lib/asksinpp/upstream holen ...'
    git -C $root submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { Write-Error 'git submodule update ist fehlgeschlagen.' }
}

if (-not (Test-Path (Join-Path $upstream 'AskSinPP.h'))) {
    Write-Error @"
$upstream ist leer -- das Submodul fehlt. Abhilfe:
    git submodule update --init --recursive
"@
}

Write-Host 'Patches aus lib/asksinpp/patches/ anwenden ...'

# --ignore-whitespace ist nicht Bequemlichkeit, sondern noetig: die Patches
# liegen per .gitattributes immer mit LF vor, der Arbeitsbaum des Submoduls
# dagegen unterliegt dessen eigenem core.autocrlf -- auf Windows also CRLF.
# Ohne die Option findet git apply dort keine einzige Kontextzeile. Die Option
# lockert nur den Vergleich am Zeilenende; der Inhalt der Treffer bleibt exakt.
$applyOpt = '--ignore-whitespace'

$patches = @(Get-ChildItem -Path $patchdir -Filter '*.patch' -File -ErrorAction SilentlyContinue | Sort-Object Name)

if ($patches.Count -eq 0) {
    Write-Host '  (keine Patches vorhanden)'
}

foreach ($patch in $patches) {
    $p = $patch.FullName

    git -C $upstream apply $applyOpt --reverse --check $p 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  bereits angewandt : $($patch.Name)"
        continue
    }

    git -C $upstream apply $applyOpt --check $p 2>$null
    if ($LASTEXITCODE -eq 0) {
        git -C $upstream apply $applyOpt $p
        if ($LASTEXITCODE -ne 0) { Write-Error "git apply ist bei $($patch.Name) fehlgeschlagen." }
        Write-Host "  angewandt         : $($patch.Name)"
        continue
    }

    # Die Meldung von git selbst ausgeben, nicht wegwerfen -- ohne sie ist
    # nicht zu unterscheiden, ob der Commit nicht stimmt, die Datei fehlt oder
    # die Zeilenenden nicht passen.
    $gitSagt = (git -C $upstream apply $applyOpt --check $p 2>&1 | Out-String).Trim()

    Write-Error @"
$($patch.Name) passt nicht auf den Arbeitsbaum.

git sagt dazu:
$gitSagt

Steht das Submodul auf dem Commit, den dieses Repo festhaelt? Pruefen mit:
    git submodule status
Zuruecksetzen mit:
    git submodule update --force --init --recursive
"@
}

Write-Host ''
Write-Host 'Fertig. Bauen mit:'
Write-Host '    $env:UFBT_HOME = "$HOME\.ufbt-unleashed"; ufbt'
