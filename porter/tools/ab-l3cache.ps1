param([string]$TagA = 'l3c1a', [string]$TagB = 'l3c1b', [int]$MenuHold = 60, [int]$WorldSeconds = 180, [string]$Save = 'C:\Users\Mubeen\Documents\Projects\yotei-work\save-import\anyps5-zip-b', [string]$ExtraA = 'APS5_PORTER_L3=1', [string]$ExtraB = 'APS5_PORTER_L3=1;APS5_PORTER_L3_CACHE=1', [switch]$SkipRestore, [switch]$SkipA, [int]$TimeoutMin = 20)
# A/B of the engine draw cache: run A (markers only) then run B (markers + APS5_PORTER_L3_CACHE=1), each on the
# zip-b save through l3probe-run.ps1 (60 s menu, CONTINUE, 180 s world). Frame dumps of run A are archived by run
# B's launcher into <Game>\frames-before-<TagB>; run B's stay in <Game>. Afterwards the 1afe29a87 driver and the
# zip-b save are restored unless -SkipRestore. Start detached (Win32_Process Create), never from Claude's shell.
$tools = 'C:\Users\Mubeen\Documents\Projects\yotei-work\tools'
$runs = 'C:\Users\Mubeen\Documents\Projects\yotei-work\runs-best'
$porter = 'C:\Users\Mubeen\Documents\Projects\ps5-porter\porter\tools'
$log = Join-Path $runs "ab-l3cache-$TagA.log"
function Note([string]$text) { "$(Get-Date -Format 'HH:mm:ss') $text" | Add-Content -Path $log }
function Stop-Game {
    Get-Process -Name app -ErrorAction SilentlyContinue | Stop-Process -Force
    for ($i = 0; $i -lt 60 -and (Get-Process app -ErrorAction SilentlyContinue); ++$i) { Start-Sleep -Seconds 1 }
}
Note "start: A=$TagA ($ExtraA) B=$TagB ($ExtraB)"
try {
    foreach ($pair in @(@($TagA, $ExtraA), @($TagB, $ExtraB))) {
        $tag, $extra = $pair
        if ($SkipA -and $tag -eq $TagA) { continue }
        Stop-Game
        & (Join-Path $tools 'save-swap.ps1') -From $Save | ForEach-Object { Note "$_" }
        Note "run $tag"
        & (Join-Path $porter 'l3probe-run.ps1') -Tag $tag -MenuHold $MenuHold -WorldSeconds $WorldSeconds -Hooks (Join-Path $porter '..\hooks\PPSA26344-l3marks.hooks') -ExtraEnv $extra -TimeoutMin $TimeoutMin
        Note "run $tag done"
        Stop-Game
    }
} catch {
    Note "error: $_"
} finally {
    Stop-Game
    if (-not $SkipRestore) {
        & (Join-Path $porter 'deploy-porter.ps1') -Restore | ForEach-Object { Note "$_" }
        & (Join-Path $tools 'save-swap.ps1') -From $Save | ForEach-Object { Note "$_" }
    }
    Note 'ALL DONE'
    'done' | Set-Content -Path (Join-Path $runs "ab-l3cache-$TagA.done")
}
