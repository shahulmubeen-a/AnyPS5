param([string]$Tag = 'l3p1', [int]$MenuHold = 60, [int]$WorldSeconds = 180, [string]$Hooks = "$PSScriptRoot\..\hooks\PPSA26344-l3probe.hooks", [string]$ExtraEnv = '', [int]$TimeoutMin = 20)
# L3 probe run: boots the PPSA26344-best runtime on the Ezo save with a probe hook table, holds the main menu for
# $MenuHold s, presses CONTINUE (ezo-run.ps1), lets the world run for $WorldSeconds s and stops the game.
# $ExtraEnv adds switches as "NAME=value;NAME2=value2".
# Start it outside Claude's shell (Win32_Process Create) so the background-shell reaper cannot kill the game.
$tools = 'C:\Users\Mubeen\Documents\Projects\yotei-work\tools'
$runs = 'C:\Users\Mubeen\Documents\Projects\yotei-work\runs-best'
$log = Join-Path $runs "porter-$Tag.log"
function Note([string]$text) { "$(Get-Date -Format 'HH:mm:ss') $text" | Add-Content -Path $log }
Note "start, hooks $((Resolve-Path $Hooks).Path), extra env '$ExtraEnv'"
try {
    $runEnv = @{ APS5_GUEST_HOOKS = (Resolve-Path $Hooks).Path; APS5_GUEST_HOOKS_LOG = '8'; APS5_PROFILE_DRAW = '1' }
    foreach ($pair in ($ExtraEnv -split ';' | Where-Object { $_ -match '=' })) { $name, $value = $pair -split '=', 2; $runEnv[$name] = $value }
    & (Join-Path $tools 'ezo-run.ps1') -Tag $Tag -MenuHold $MenuHold -TimeoutMin $TimeoutMin -Env $runEnv *>&1 | ForEach-Object { Note "$_" }
    Note 'continue pressed, world phase'
    Start-Sleep -Seconds $WorldSeconds
} catch {
    Note "error: $_"
} finally {
    Get-Process -Name app -ErrorAction SilentlyContinue | Stop-Process -Force
    Note 'game stopped'
    'done' | Set-Content -Path (Join-Path $runs "porter-$Tag.done")
}
