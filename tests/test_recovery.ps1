# Read-only-target fixture: verifies that the 0.2.1 surgical recovery changes
# ONLY the selected toolbar key, and preserves custom views/usercmd/bar files.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$temp = Join-Path $env:TEMP ('tcs-recovery-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $temp | Out-Null
try {
    $ini = Join-Path $temp 'wincmd.ini'
    $prePlugin = @'
[Buttonbar]
Buttonbar=%COMMANDER_INI_PATH%\CustomToolbar.bar
[left]
SpecialView=8
[right]
SpecialView=3
[OtherUserSettings]
DoNotTouch=YES
'@
    $broken = @'
[Buttonbar]
Buttonbar=%COMMANDER_INI_PATH%\TCSymbolicLinks.bar
[left]
SpecialView=11
[right]
SpecialView=9
[OtherUserSettings]
DoNotTouch=YES
'@
    Set-Content -LiteralPath ($ini + '.tcslinks.bak') -Value $prePlugin -Encoding Ascii
    Set-Content -LiteralPath $ini -Value $broken -Encoding Ascii
    $usercmd = Join-Path $temp 'usercmd.ini'
    $usercmdText = '[em_MyOwnButton]'+"`r`n"+'cmd=C:\Windows\notepad.exe'
    [IO.File]::WriteAllText($usercmd, $usercmdText)
    $bar = Join-Path $temp 'CustomToolbar.bar'
    [IO.File]::WriteAllText($bar, "[Buttonbar]`r`nButtoncount=7`r`ncmd1=notepad.exe`r`n")
    $barBefore = [IO.File]::ReadAllBytes($bar)
    & (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts/Restore-TC-Toolbar-Only.ps1') -IniPath $ini -Force
    $after = Get-Content -LiteralPath $ini -Raw
    if ($after -notmatch '(?m)^Buttonbar=%COMMANDER_INI_PATH%\\CustomToolbar\.bar\s*$') {
        throw 'Restored toolbar reference does not match pre-plugin value'
    }
    if ($after -notmatch 'SpecialView=11' -or $after -notmatch 'SpecialView=9' -or
        $after -notmatch 'DoNotTouch=YES') { throw 'Restorer reverted unrelated settings' }
    if ([IO.File]::ReadAllText($usercmd) -cne $usercmdText) { throw 'usercmd.ini altered' }
    if ([Convert]::ToBase64String($barBefore) -cne [Convert]::ToBase64String([IO.File]::ReadAllBytes($bar))) {
        throw 'Custom .bar was altered'
    }
    Write-Host 'PASS: toolbar-only restore preserved existing buttons, usercmd and other settings'
} finally {
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
