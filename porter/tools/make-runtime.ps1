param(
    [string]$Game = 'D:\Games\AnyPS5\PPSA26344-elem',
    [string]$Build = 'C:\Users\Mubeen\Documents\Projects\ps5-porter\build',
    [string]$Dump = 'D:\Games\PS5 Dumps\PPSA26344-app0',
    [string]$Elf = 'D:\Games\PS5 Dumps\PPSA26344-elf\eboot.bin',
    [string]$Patches = 'C:\Users\Mubeen\Documents\Projects\yotei-work\engine\patches\PPSA26344-1080p-fixed.relinker-patches.toml',
    [string]$Save = 'C:\Users\Mubeen\Documents\Projects\yotei-work\save-import\anyps5-zip-b',
    [string]$MinGw = 'C:\Users\Mubeen\tools\winlibs-15.2.0-r7\mingw64\bin',
    [switch]$LibsOnly
)
# Makes (or refreshes) a separate Yotei runtime for the porter's element work, beside PPSA26344-best:
# - app0 with junctions to the dump's big folders (cache_ps5, movies, sce_sys, sounds, ui), a hard link of the
#   dump's top-level file and a real sce_module folder (the relinker writes libc.prx.guest.prx there);
# - libs = the build's core/libs/libs (patched .prx, unpatched/, implib/) plus the three MinGW runtime DLLs;
# - app.exe relinked from the decrypted eboot with the 1080p-fixed game patches;
# - the zip-b save.
# -LibsOnly refreshes only libs (after a driver rebuild). Refuses while app.exe runs.
# Undo: unlink the junctions in app0 first (cmd /c rmdir <junction>), then delete the folder.
$ErrorActionPreference = 'Stop'
if (Get-Process -Name app -ErrorAction SilentlyContinue) { throw 'app.exe is running' }
$libsFrom = Join-Path $Build 'core\libs\libs'
if (-not $LibsOnly) {
    New-Item -ItemType Directory -Force $Game, "$Game\app0", "$Game\app0\sce_module", "$Game\data", "$Game\download0", "$Game\_trophy" | Out-Null
    foreach ($name in 'cache_ps5', 'movies', 'sce_sys', 'sounds', 'ui') {
        $link = Join-Path "$Game\app0" $name
        if (-not (Test-Path $link)) { New-Item -ItemType Junction -Path $link -Target (Join-Path $Dump $name) | Out-Null }
    }
    foreach ($file in Get-ChildItem $Dump -File) {
        $link = Join-Path "$Game\app0" $file.Name
        if (-not (Test-Path -LiteralPath $link)) { New-Item -ItemType HardLink -Path $link -Target $file.FullName | Out-Null }
    }
    foreach ($file in 'anyps5-entitlements.ini') {
        $from = Join-Path 'D:\Games\AnyPS5\PPSA26344-best' $file
        if ((Test-Path $from) -and -not (Test-Path (Join-Path $Game $file))) { Copy-Item $from $Game }
    }
    & (Join-Path $Build 'core\relinker\relinker.exe') --windows --registry --patches $Patches $Elf (Join-Path $Game 'app.exe')
    if ($LASTEXITCODE -ne 0) { throw "relinker failed with $LASTEXITCODE" }
    & 'C:\Users\Mubeen\Documents\Projects\yotei-work\tools\save-swap.ps1' -From $Save -Game $Game
}
if (Test-Path "$Game\libs") { Remove-Item "$Game\libs" -Recurse -Force }
Copy-Item $libsFrom "$Game\libs" -Recurse
foreach ($dll in 'libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll') { Copy-Item (Join-Path $MinGw $dll) "$Game\libs" }
"runtime $Game ready: $((Get-ChildItem "$Game\libs" -Recurse -File).Count) lib files; driver $((Get-FileHash "$Game\libs\libSceAgcDriver.prx").Hash.Substring(0,12))"
