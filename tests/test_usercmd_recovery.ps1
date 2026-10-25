# Windows PowerShell 5.1 smoke tests for surgical usercmd.ini recovery.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\Restore-TC-Missing-Commands.ps1'
$root = Join-Path $env:TEMP ('tc-cmd-recovery-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $root | Out-Null
function Assert([bool]$ok,[string]$reason) { if (-not $ok) { throw $reason } }
function Invoke-Recover([string]$config,[bool]$success) {
    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $scriptPath -IniPath $config -Force
    $result = $LASTEXITCODE
    Assert (($result -eq 0) -eq $success) "Unexpected restore result $result (expect $success)"
}
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class FixtureIni {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, EntryPoint="GetPrivateProfileStringW")]
    public static extern uint Read(string section,string key,string fallback,StringBuilder output,uint count,string path);
}
'@
function Get-Value([string]$path,[string]$section,[string]$key) {
    $buf = [Text.StringBuilder]::new(1024)
    [void][FixtureIni]::Read($section,$key,'<missing>',$buf,[uint32]$buf.Capacity,$path)
    return $buf.ToString()
}
try {
    $hebrew = -join @([char]0x05e2, [char]0x05d1, [char]0x05e8, [char]0x05d9, [char]0x05ea)
    $profile = Join-Path $root ($hebrew + ' original profile')
    New-Item -ItemType Directory -Force $profile | Out-Null
    $ini = Join-Path $profile 'wincmd.ini'
    $usercmd = Join-Path $profile 'usercmd.ini'
    $backup = $usercmd + '.tcslinks.bak'
    $original = @'
[em_MyButton]
cmd=calc.exe
param=keep-new
[em_NewCustom]
cmd=cmd.exe
param=do-not-touch
'@
    $old = @'
[em_MyButton]
cmd=notepad.exe
param=old-version
[em_LostButton]
cmd=winver.exe
param=restore-this
[em_LostHebrew]
cmd=explorer.exe
param=another-missing
'@
    [IO.File]::WriteAllText($ini, '[Configuration]')
    [IO.File]::WriteAllText($usercmd,$original,[Text.Encoding]::ASCII)
    [IO.File]::WriteAllText($backup,$old,[Text.Encoding]::ASCII)
    $originalHash = (Get-FileHash $usercmd -Algorithm SHA256).Hash
    $oldHash = (Get-FileHash $backup -Algorithm SHA256).Hash
    Invoke-Recover $ini $true
    Assert ((Get-Value $usercmd 'em_MyButton' 'cmd') -eq 'calc.exe') 'Existing command was overwritten'
    Assert ((Get-Value $usercmd 'em_NewCustom' 'param') -eq 'do-not-touch') 'New custom command lost'
    Assert ((Get-Value $usercmd 'em_LostButton' 'param') -eq 'restore-this') 'Missing command was not restored'
    Assert ((Get-Value $usercmd 'em_LostHebrew' 'cmd') -eq 'explorer.exe') 'Second missing command absent'
    Assert ((Get-FileHash $backup -Algorithm SHA256).Hash -eq $oldHash) 'Immutable old backup changed'
    $saves = @(Get-ChildItem -LiteralPath $profile -Filter 'usercmd.ini.before-merge-*')
    Assert ($saves.Count -eq 1) 'No rollback snapshot of previous usercmd.ini'
    Assert ((Get-FileHash $saves[0].FullName -Algorithm SHA256).Hash -eq $originalHash) 'Rollback snapshot differs'
    $mergedHash = (Get-FileHash $usercmd -Algorithm SHA256).Hash
    Invoke-Recover $ini $true
    Assert ((Get-FileHash $usercmd -Algorithm SHA256).Hash -eq $mergedHash) 'Repeated recovery is not idempotent'
    $profile2 = Join-Path $root 'empty-current'
    New-Item -ItemType Directory -Force $profile2 | Out-Null
    $ini2 = Join-Path $profile2 'wincmd.ini'
    [IO.File]::WriteAllText($ini2,'[Configuration]')
    $usercmd2 = Join-Path $profile2 'usercmd.ini'
    [IO.File]::WriteAllText(($usercmd2+'.tcslinks.bak'),$old,[Text.Encoding]::ASCII)
    Invoke-Recover $ini2 $true
    Assert ((Get-Value $usercmd2 'em_LostButton' 'cmd') -eq 'winver.exe') 'Missing usercmd.ini not recovered'
    $profile3 = Join-Path $root 'no-backup'
    New-Item -ItemType Directory -Force $profile3 | Out-Null
    $ini3 = Join-Path $profile3 'wincmd.ini'
    [IO.File]::WriteAllText($ini3,'[Configuration]')
    Invoke-Recover $ini3 $false
    Assert (-not (Test-Path (Join-Path $profile3 'usercmd.ini'))) 'Missing backup created arbitrary commands'
    Write-Host 'PASS: usercmd recovery adds only missing sections, preserves user changes, saves rollback, is idempotent and refuses missing backup'
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
