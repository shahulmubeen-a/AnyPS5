param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [string]$Game = 'D:\Games\AnyPS5\PPSA26344-elem',
    [string]$Cache = 'C:\AnyPS5-cache-best\PPSA26344',
    [string]$Save = 'C:\Users\Mubeen\Documents\Projects\yotei-work\save-import\anyps5-zip-b',
    [int]$MenuFrame = 1500,
    [int]$MenuHold = 20,
    [int]$PopupPresses = 1,
    [int]$WorldSeconds = 180,
    [int]$TimeoutMin = 20,
    [int]$DumpEvery = 5,
    [string]$Hooks = '',
    [string]$ExtraEnv = '',
    [string]$Unset = '',
    [string]$Plan = '',
    [int]$PlanStartAfter = 60,
    [int]$StayAtMenu = 0
)
# One Yotei run on the element-work runtime (make-runtime.ps1): zip-b save, the yotei/best README environment of
# 161097737 plus skip-failed-work and frame dumps every $DumpEvery presents, 1080p-fixed app.exe.
# Waits for the main menu (past $MenuFrame presents and below 15 fps), holds it $MenuHold s, dismisses
# $PopupPresses bonus popups, presses CONTINUE and lets the world run $WorldSeconds s.
# $Hooks names a guest hook table (APS5_GUEST_HOOKS). $ExtraEnv adds switches "NAME=value;NAME2=value2", $Unset
# drops README switches "NAME;NAME2".
# $Plan names a sweep plan: after $PlanStartAfter s in the world, each non-comment line
#   <label> <seconds> [<control line>[;;<control line>...]]
# is applied by writing its control lines into the live control file <Game>\elem-<Tag>.txt (APS5_ELEM_FILE), then
# held for <seconds>; the presents counted over the step's second half and the newest frame dump are recorded in
# runs-best\plan-<Tag>.txt and runs-best\shots-<Tag>\<label>.bmp. A label "base" with no control lines measures the
# unchanged scene. The world phase ends when the plan ends (or after $WorldSeconds without a plan).
# $StayAtMenu > 0 skips CONTINUE: after the popups the run stays at the main menu (the benchmark scene) for
# $StayAtMenu s, or runs the plan there.
# Start it outside Claude's shell (Win32_Process Create), so the background-shell reaper cannot kill the game.
$tools = 'C:\Users\Mubeen\Documents\Projects\yotei-work\tools'
$runs = 'C:\Users\Mubeen\Documents\Projects\yotei-work\runs-best'
$log = Join-Path $runs "elem-$Tag.log"
$csv = Join-Path $runs "monitor-$Tag.csv"
$pad = Join-Path $Game "pad-$Tag.txt"
$control = Join-Path $Game "elem-$Tag.txt"
function Note([string]$text) { "$(Get-Date -Format 'HH:mm:ss') $text" | Add-Content -Path $log }
function Row {
    for ($i = 0; $i -lt 5; ++$i) {
        try { $line = Get-Content $csv -Tail 1 -ErrorAction Stop; $f = ([string]$line).Split(','); if ($f.Count -ge 5 -and $f[3] -match '^\d+$') { return [pscustomobject]@{ T = [int]$f[1]; Frames = [int]$f[3]; Dump = $f[4] } } } catch {}
        Start-Sleep -Milliseconds 400
    }
    return $null
}
function Stop-Game {
    Get-Process -Name app -ErrorAction SilentlyContinue | Stop-Process -Force
    for ($i = 0; $i -lt 60 -and (Get-Process app -ErrorAction SilentlyContinue); ++$i) { Start-Sleep -Seconds 1 }
}
Note "start: game $Game, hooks '$Hooks', extra '$ExtraEnv', unset '$Unset', plan '$Plan'"
try {
    Stop-Game
    & (Join-Path $tools 'save-swap.ps1') -From $Save -Game $Game | ForEach-Object { Note "$_" }
    New-Item -ItemType File -Force $pad, $control | Out-Null
    $archive = Join-Path $Game "frames-before-$Tag"
    New-Item -ItemType Directory -Force $archive, $Cache | Out-Null
    Get-ChildItem $Game -Filter 'frame_*.bmp' | Move-Item -Destination $archive
    foreach ($name in Get-ChildItem Env: | Where-Object { $_.Name -like 'APS5_*' -or $_.Name -like 'ANYPS5_*' } | ForEach-Object Name) { Remove-Item "Env:$name" }
    # yotei/best README of 161097737 (2026-10-24).
    $runEnv = [ordered]@{
        APS5_SPANNED_WAVE = '1'; APS5_BINDLESS_IMAGES = '1'; APS5_BINDLESS_LAZY = '1'; APS5_NO_RESULT_MEMO = '1'; APS5_STAGING_DIFF = '1'
        APS5_PIPELINE_INDIRECT = '1'; APS5_NO_ADDRESS_DRAW_REUSE = '1'; APS5_NO_PIPELINE_JOURNAL = '1'; APS5_RESOURCE_CACHE_ENTRIES = '1024'
        APS5_ASYNC_LANE = '51'; APS5_NO_DRAW_AHEAD = '1'; APS5_DEFER_REFRESH_FLUSH = '1'; APS5_PIPELINE_DISPATCH = '1'
        APS5_LOCAL_SKIP_FAILED_WORK = '1'; APS5_SKIP_REFUSED_DRAWS = '1'
        ANYPS5_SHADER_CACHE_DIR = $Cache; APS5_DUMP_FRAMES = '100000'; APS5_DUMP_FRAMES_EVERY = "$DumpEvery"; APS5_DUMP_SCALE = '2'
        APS5_PAD_INPUT_FILE = $pad; APS5_ELEM_FILE = $control
    }
    if ($Hooks) { $runEnv['APS5_GUEST_HOOKS'] = (Resolve-Path $Hooks).Path; $runEnv['APS5_GUEST_HOOKS_LOG'] = '4' }
    foreach ($pair in ($ExtraEnv -split ';' | Where-Object { $_ -match '=' })) { $name, $value = $pair -split '=', 2; $runEnv[$name] = $value }
    foreach ($name in ($Unset -split ';' | Where-Object { $_ })) { $runEnv.Remove($name) }
    foreach ($key in $runEnv.Keys) { Set-Item -Path "Env:$key" -Value $runEnv[$key] }
    Note ('env: ' + (($runEnv.Keys | ForEach-Object { "$_=$($runEnv[$_])" }) -join ' '))
    $command = '/V:ON /c ""' + "$Game\app.exe" + '" 1>NUL 2>"' + "$runs\run-$Tag.err.log" + '" & echo !ERRORLEVEL! > "' + "$runs\exit-$Tag.txt" + '""'
    $shell = Start-Process -FilePath cmd.exe -ArgumentList $command -WorkingDirectory $Game -PassThru -WindowStyle Normal
    $app = $null
    for ($i = 0; $i -lt 480 -and $null -eq $app; ++$i) { Start-Sleep -Milliseconds 250; $app = Get-CimInstance Win32_Process -Filter "ParentProcessId=$($shell.Id) AND Name='app.exe'" }
    if ($null -eq $app) { throw 'app.exe did not start' }
    Note "launched pid $($app.ProcessId)"
    Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile', '-File', (Join-Path $tools 'monitor-best.ps1'), '-Tag', $Tag, '-Game', $Game
    $deadline = (Get-Date).AddMinutes($TimeoutMin)
    Start-Sleep -Seconds 20
    while ($true) {
        if (-not (Get-Process -Name app -ErrorAction SilentlyContinue)) { throw 'app.exe exited before the menu' }
        if ((Get-Date) -gt $deadline) { throw "no menu after $TimeoutMin min" }
        try { $line = Get-Content $csv -Tail 1 -ErrorAction Stop } catch { $line = $null }
        if ($null -eq $line) { Start-Sleep -Seconds 3; continue }
        $f = ([string]$line).Split(',')
        if ($f.Count -ge 4 -and $f[3] -match '^\d+$' -and [int]$f[3] -gt $MenuFrame -and $f[2] -match '^[\d.]+$' -and [double]$f[2] -lt 15) { break }
        Start-Sleep -Seconds 3
    }
    Note "menu at $($f -join ',')"
    Start-Sleep -Seconds $MenuHold
    $steps = @(for ($i = 0; $i -lt $PopupPresses; ++$i) { 'cross:300:6000' })
    if ($StayAtMenu -le 0) { $steps += 'cross:300:0' }
    if ($steps.Count -gt 0) { & (Join-Path $tools 'pad-send.ps1') -File $pad -Steps $steps | ForEach-Object { Note "$_" } }
    if ($StayAtMenu -gt 0) {
        $WorldSeconds = $StayAtMenu
        Note 'popups dismissed, menu phase'
    } else {
        Note 'continue pressed, world phase'
    }
    if (-not $Plan) {
        $end = (Get-Date).AddSeconds($WorldSeconds)
        while ((Get-Date) -lt $end) {
            if (-not (Get-Process -Name app -ErrorAction SilentlyContinue)) { throw 'app.exe exited in the world phase' }
            Start-Sleep -Seconds 5
        }
    } else {
        $out = Join-Path $runs "plan-$Tag.txt"
        $shots = Join-Path $runs "shots-$Tag"
        New-Item -ItemType Directory -Force $shots | Out-Null
        "# $(Get-Date -Format 'yyyy-MM-dd HH:mm') plan $Plan; label seconds presents-per-10s frames span-s dump" | Set-Content $out
        Start-Sleep -Seconds $PlanStartAfter
        $queue = [System.Collections.Generic.List[string]]::new()
        foreach ($line in (Get-Content $Plan | Where-Object { $_.Trim() -and -not $_.Trim().StartsWith('#') })) { $queue.Add($line.Trim()) }
        for ($index = 0; $index -lt $queue.Count; ++$index) {
            $line = $queue[$index]
            # "@autoplan <rounds> [elem_plan.py options]": summarizes the newest <rounds> element report rounds of this
            # run (elem_report.py) and inserts the sweep elem_plan.py builds from them.
            if ($line.StartsWith('@autoplan')) {
                $words = $line -split '\s+'
                $rounds = $words[1]
                $options = if ($words.Count -gt 2) { $words[2..($words.Count - 1)] } else { @() }
                $porterTools = 'C:\Users\Mubeen\Documents\Projects\ps5-porter\porter\tools'
                $json = Join-Path $runs "report-$Tag.json"
                $auto = Join-Path $runs "autoplan-$Tag.txt"
                & python -I (Join-Path $porterTools 'elem_report.py') (Join-Path $runs "run-$Tag.err.log") --last $rounds --json $json --top 60 | Set-Content (Join-Path $runs "report-$Tag.txt")
                & python -I (Join-Path $porterTools 'elem_plan.py') $json $auto @options | Out-Null
                $insert = [System.Collections.Generic.List[string]]::new()
                foreach ($generated in (Get-Content $auto | Where-Object { $_.Trim() -and -not $_.Trim().StartsWith('#') })) { $insert.Add($generated.Trim()) }
                Note "expanded @autoplan into $($insert.Count) steps from the newest $rounds rounds"
                $queue.InsertRange($index + 1, $insert)
                continue
            }
            # "@topmat <n> <seconds> [gmat|mat] [base-every]": the n materials with the most worker + commit time in the
            # newest three [elem-mat] report rounds of this run, one step each, with a base step every [base-every].
            if ($line.StartsWith('@topmat')) {
                $words = $line -split '\s+'
                $n = [int]$words[1]; $stepSeconds = [int]$words[2]
                $verb = if ($words.Count -gt 3) { $words[3] } else { 'gmat' }
                $every = if ($words.Count -gt 4) { [int]$words[4] } else { 4 }
                $matLines = Select-String -Path (Join-Path $runs "run-$Tag.err.log") -Pattern '^\[elem-mat\] ' | ForEach-Object { $_.Line }
                $rounds = $matLines | ForEach-Object { [int](($_ -split ' ')[1]) }
                $newest = ($rounds | Measure-Object -Maximum).Maximum
                $sums = @{}
                foreach ($m in $matLines) {
                    $f = $m -split ' '
                    if ([int]$f[1] -lt $newest - 2) { continue }
                    $sums[$f[7]] = [double]$sums[$f[7]] + [double]$f[2] + [double]$f[3]
                }
                $top = $sums.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First $n
                $insert = [System.Collections.Generic.List[string]]::new()
                $count = 0
                foreach ($entry in $top) {
                    ++$count
                    $insert.Add("mat$count-$($entry.Key) $stepSeconds $verb $($entry.Key)")
                    if ($count % $every -eq 0) { $insert.Add("base-m$count $stepSeconds") }
                }
                Note "expanded @topmat into $($insert.Count) steps: $(($top | ForEach-Object { "$($_.Key)=$([math]::Round($_.Value))" }) -join ' ')"
                $queue.InsertRange($index + 1, $insert)
                continue
            }
            $parts = $line -split '\s+', 3
            $label = $parts[0]; $seconds = [int]$parts[1]
            $lines = if ($parts.Count -gt 2) { $parts[2] -split ';;' | ForEach-Object { $_.Trim() } } else { @() }
            ($lines -join "`n") | Set-Content -Encoding ascii $control
            Note "step $label ($seconds s): $($lines -join ' ;; ')"
            Start-Sleep -Seconds ([int]($seconds / 2))
            $first = Row
            Start-Sleep -Seconds ($seconds - [int]($seconds / 2))
            $last = Row
            if (-not (Get-Process -Name app -ErrorAction SilentlyContinue)) { throw "app.exe exited during step $label" }
            if ($first -and $last -and $last.T -gt $first.T) {
                $rate = [math]::Round(10.0 * ($last.Frames - $first.Frames) / ($last.T - $first.T), 2)
                if ($last.Dump) { Copy-Item (Join-Path $Game $last.Dump) (Join-Path $shots "$label.bmp") -Force -ErrorAction SilentlyContinue }
                "$label $seconds $rate $($last.Frames - $first.Frames) $($last.T - $first.T) $($last.Dump)" | Add-Content $out
            } else {
                "$label $seconds ? ? ? ?" | Add-Content $out
            }
        }
        '' | Set-Content -Encoding ascii $control
        'PLAN DONE' | Add-Content $out
    }
} catch {
    Note "error: $_"
} finally {
    Stop-Game
    & (Join-Path $tools 'save-swap.ps1') -From $Save -Game $Game | ForEach-Object { Note "$_" }
    Note 'ALL DONE'
    'done' | Set-Content -Path (Join-Path $runs "elem-$Tag.done")
}
