#requires -Version 5.1
# Restore only missing Total Commander em_* definitions from the plugin's original backup.
# Existing usercmd.ini sections are verified and never intentionally replaced.
[CmdletBinding()]
param([string]$IniPath, [switch]$Force)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $IniPath) {
    Add-Type -AssemblyName System.Windows.Forms
    $pick = [System.Windows.Forms.OpenFileDialog]::new()
    $pick.Title = 'Choose active Total Commander wincmd.ini (Help > About)'
    $pick.FileName = 'wincmd.ini'
    $pick.Filter = 'Total Commander settings|wincmd.ini|INI files|*.ini'
    if ($pick.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { return }
    $IniPath = $pick.FileName
}
$ini = (Resolve-Path -LiteralPath $IniPath -ErrorAction Stop).ProviderPath
if ([IO.Path]::GetFileName($ini) -ine 'wincmd.ini') { throw 'Select the active wincmd.ini.' }
$folder = Split-Path -Parent $ini
$target = Join-Path $folder 'usercmd.ini'
$source = $target + '.tcslinks.bak'
if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    throw 'No usercmd.ini.tcslinks.bak found. Do not invent lost custom commands.'
}
if (@(Get-Process -Name 'TOTALCMD','TOTALCMD64' -ErrorAction SilentlyContinue).Count -gt 0) {
    throw 'Close all Total Commander windows first.'
}
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class TcCommandIni {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, EntryPoint="GetPrivateProfileSectionNamesW")]
    public static extern uint GetNames([Out] char[] text, uint capacity, string ini);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, EntryPoint="GetPrivateProfileSectionW")]
    public static extern uint GetSection(string name, [Out] char[] text, uint capacity, string ini);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, EntryPoint="WritePrivateProfileSectionW", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool WriteSection(string name, string text, string ini);
}
'@

function Read-Names([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return @() }
    $buffer = [char[]]::new(131072)
    $length = [TcCommandIni]::GetNames($buffer, [uint32]$buffer.Length, $Path)
    if ($length -ge $buffer.Length - 2) { throw "Too many INI sections in $Path" }
    if ($length -eq 0) { return @() }
    return @(([string]::new($buffer,0,[int]$length)).Split([char]0) |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}
function Read-Section([string]$Path,[string]$Section) {
    $buffer = [char[]]::new(131072)
    $length = [TcCommandIni]::GetSection($Section,$buffer,[uint32]$buffer.Length,$Path)
    if ($length -ge $buffer.Length - 2) { throw "Section too large: $Section" }
    if ($length -eq 0) { return '' }
    return [string]::new($buffer,0,[int]$length)
}
$originalNames = @(Read-Names $source | Where-Object { $_ -match '(?i)^em_' })
if ($originalNames.Count -eq 0) { throw 'Backup has no em_* command sections.' }
$presentNames = @(Read-Names $target)
if ((Test-Path -LiteralPath $target -PathType Leaf) -and
    ((Get-Item -LiteralPath $target).Length -gt 0) -and $presentNames.Count -eq 0) {
    throw 'Current usercmd.ini could not be decoded. Refusing to edit it.'
}
$present = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($name in $presentNames) { [void]$present.Add($name) }
$missing = @($originalNames | Where-Object { -not $present.Contains($_) })
if ($missing.Count -eq 0) {
    Write-Host 'All original em_* sections are present; no files changed.' -ForegroundColor Green
    return
}
Write-Host ('Missing user commands: ' + ($missing -join ', ')) -ForegroundColor Yellow
Write-Host 'Only these sections will be restored; current definitions will remain unchanged.'
if (-not $Force -and ((Read-Host 'Type RESTORE to apply') -cne 'RESTORE')) { return }
$existed = Test-Path -LiteralPath $target -PathType Leaf
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$rollback = if ($existed) {
    $target + '.before-merge-' + $timestamp + '-' + [guid]::NewGuid().ToString('N').Substring(0,8)
} else { '' }
$originalSections = @{}
foreach ($name in $presentNames) { $originalSections[$name] = Read-Section $target $name }
if ($existed) {
    Copy-Item -LiteralPath $target -Destination $rollback -ErrorAction Stop
    if ((Get-FileHash -LiteralPath $rollback -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash) {
        throw 'Failed to verify the rollback copy. No edits were made.'
    }
}
try {
    if (-not $existed) {
        Copy-Item -LiteralPath $source -Destination $target -ErrorAction Stop
    } else {
        foreach ($name in $missing) {
            $body = Read-Section $source $name
            if (-not $body) { throw "Empty original command: $name" }
            $body = $body + [char]0 + [char]0
            if (-not [TcCommandIni]::WriteSection($name,$body,$target)) {
                throw "Failed to restore missing command $name"
            }
        }
    }
    $after = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($name in @(Read-Names $target)) { [void]$after.Add($name) }
    foreach ($name in $originalSections.Keys) {
        if (-not $after.Contains($name) -or (Read-Section $target $name) -cne $originalSections[$name]) {
            throw "Existing section changed: $name"
        }
    }
    foreach ($name in $missing) {
        if (-not $after.Contains($name) -or (Read-Section $target $name) -cne (Read-Section $source $name)) {
            throw "Restored section could not be verified: $name"
        }
    }
} catch {
    $errorMessage = [string]$_
    if ($existed) {
        Copy-Item -LiteralPath $rollback -Destination $target -Force -ErrorAction Stop
    } else {
        Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
    }
    throw "Restore did not verify; original usercmd.ini recovered. $errorMessage"
}
Write-Host ("Restored {0} missing custom commands: {1}" -f $missing.Count,($missing -join ', ')) -ForegroundColor Green
if ($rollback) { Write-Host "Rollback copy: $rollback" }
