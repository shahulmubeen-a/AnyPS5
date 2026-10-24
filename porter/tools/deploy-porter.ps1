param([switch]$Restore, [string]$Game = 'D:\Games\AnyPS5\PPSA26344-best', [string]$Build = 'C:\Users\Mubeen\Documents\Projects\ps5-porter\build\core\libs\libs', [string]$Backup = '1afe29a87')
# Deploys the ps5-porter driver and libSceAgc into the PPSA26344-best runtime, or restores the 1afe29a87 originals.
# The originals are kept in <Game>\_build-<Backup> (libSceAgcDriver.prx was put there on 2026-10-22; libSceAgc.prx
# is added on the first deploy). Refuses while app.exe runs.
$libs = 'libSceAgcDriver.prx', 'libSceAgc.prx'
if (Get-Process -Name app -ErrorAction SilentlyContinue) { throw 'app.exe is running' }
$keep = Join-Path $Game "_build-$Backup"
New-Item -ItemType Directory -Force (Join-Path $keep 'unpatched') | Out-Null
foreach ($lib in $libs) {
    foreach ($sub in '', 'unpatched') {
        $kept = Join-Path (Join-Path $keep $sub) $lib
        $live = Join-Path (Join-Path (Join-Path $Game 'libs') $sub) $lib
        if (-not (Test-Path $kept)) { Copy-Item $live $kept; "kept original $live" }
        $source = if ($Restore) { $kept } else { Join-Path (Join-Path $Build $sub) $lib }
        Copy-Item $source $live -Force
        "$(if ($Restore) { 'restored' } else { 'deployed' }) $live <- $source ($((Get-FileHash $live).Hash.Substring(0, 12)))"
    }
}
