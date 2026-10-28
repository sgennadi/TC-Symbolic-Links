# Windows-only regression for non-destructive Total Commander icon recovery.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$tool = Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\Restore-TC-Toolbar-Icons.ps1'
$root = Join-Path $env:TEMP ('tcs-icon-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $root | Out-Null
function Assert([bool]$pass,[string]$message) { if (-not $pass) { throw $message } }
function Read-Key([string]$p,[string]$key) {
    $value = Get-Content -LiteralPath $p -Raw
    $m = [regex]::Match($value,'(?im)^' + [regex]::Escape($key) + '=(.*)$')
    if ($m.Success) { return $m.Groups[1].Value.Trim() }
    return '<absent>'
}
try {
    $ini = Join-Path $root 'wincmd.ini'
    $bar = Join-Path $root 'Personal.bar'
    $bak = $bar + '.tcslinks.bak'
    $commands = Join-Path $root 'usercmd.ini'
    [IO.File]::WriteAllText($ini, "[Buttonbar]" + [Environment]::NewLine + "Buttonbar=$bar")
    $source = @'
[Buttonbar]
Buttoncount=3
cmd1=notepad.exe
param1=hello
menu1=Notes
button1=WCMICONS.DLL,12
cmd2=calc.exe
param2=/x
menu2=Calculator
button2=
cmd3=cm_Copy
param3=
menu3=Copy
button3=WCICONS.DLL,22
'@
    $old = $source.Replace("button2=", "button2=WCMICONS.DLL,30")
    [IO.File]::WriteAllText($bar,$source,[Text.Encoding]::ASCII)
    [IO.File]::WriteAllText($bak,$old,[Text.Encoding]::ASCII)
    [IO.File]::WriteAllText($commands,('[em_MyCommand]' + [Environment]::NewLine + 'cmd=taskmgr.exe'))
    $iniHash = (Get-FileHash -LiteralPath $ini -Algorithm SHA256).Hash
    $usercmdHash = (Get-FileHash -LiteralPath $commands -Algorithm SHA256).Hash
    $backupHash = (Get-FileHash -LiteralPath $bak -Algorithm SHA256).Hash
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $tool -IniPath $ini -BarPath $bar -Force
    Assert ($LASTEXITCODE -eq 0) 'Icon recovery returned nonzero exit code'
    Assert ((Read-Key $bar 'button2') -eq 'WCMICONS.DLL,30') 'Missing icon was not restored'
    Assert ((Read-Key $bar 'button1') -eq 'WCMICONS.DLL,12') 'Existing icon changed'
    Assert ((Read-Key $bar 'button3') -eq 'WCICONS.DLL,22') 'Unrelated icon changed'
    Assert ((Read-Key $bar 'cmd2') -eq 'calc.exe') 'Button command changed'
    Assert ((Get-FileHash $ini -Algorithm SHA256).Hash -eq $iniHash) 'wincmd.ini was modified'
    Assert ((Get-FileHash $commands -Algorithm SHA256).Hash -eq $usercmdHash) 'usercmd.ini was modified'
    Assert ((Get-FileHash $bak -Algorithm SHA256).Hash -eq $backupHash) 'Original toolbar backup was overwritten'
    $snapshots = @(Get-ChildItem -LiteralPath $root -Filter 'Personal.bar.before-icon-recovery-*.bak')
    Assert ($snapshots.Count -eq 1) 'Missing rollback snapshot'
    Assert ([IO.File]::ReadAllText($snapshots[0].FullName) -ceq $source) 'Rollback content mismatch'
    $afterHash = (Get-FileHash $bar -Algorithm SHA256).Hash
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $tool -IniPath $ini -BarPath $bar -Force
    Assert ($LASTEXITCODE -eq 0) 'Idempotent rerun returned error'
    Assert ((Get-FileHash $bar -Algorithm SHA256).Hash -eq $afterHash) 'Repeated repair changed .bar'
    Assert (@(Get-ChildItem -LiteralPath $root -Filter 'Personal.bar.before-icon-recovery-*.bak').Count -eq 1) 'Repeated repair made unnecessary snapshot'
    # Conflicting icons in a backup must be skipped rather than guessed.
    $bar2 = Join-Path $root 'Ambiguous.bar'
    $bak2 = $bar2+'.tcslinks.bak'
    [IO.File]::WriteAllText($bar2,@'
[Buttonbar]
Buttoncount=1
cmd1=calc.exe
param1=
menu1=Calc
button1=
'@,[Text.Encoding]::ASCII)
    [IO.File]::WriteAllText($bak2,@'
[Buttonbar]
Buttoncount=2
cmd1=calc.exe
param1=
menu1=Calc
button1=WCMICONS.DLL,4
cmd2=calc.exe
param2=
menu2=Calc
button2=WCMICONS.DLL,5
'@,[Text.Encoding]::ASCII)
    $before = (Get-FileHash $bar2 -Algorithm SHA256).Hash
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $tool -IniPath $ini -BarPath $bar2 -Force
    Assert ($LASTEXITCODE -eq 0) 'Ambiguous icons should be skipped safely'
    Assert ((Get-FileHash $bar2 -Algorithm SHA256).Hash -eq $before) 'Ambiguous icons changed a toolbar'
    Write-Host 'PASS: icon-only restore, commands untouched, rollback, idempotence, ambiguity refusal'
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
