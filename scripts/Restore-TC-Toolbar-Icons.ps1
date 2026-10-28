#requires -Version 5.1
# Restore only missing icon references in the active Total Commander toolbar.
# Existing buttons, commands, usercmd.ini and wincmd.ini are NEVER rewritten.
# Like Install_TC_Directory_Symlink_v10.ps1, keep immutable originals and
# create a fresh rollback snapshot for every actual modification.
[CmdletBinding()]
param(
    [string]$IniPath,
    [string]$BarPath,
    [string]$BackupPath,
    [switch]$Force
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (@(Get-Process -Name 'TOTALCMD','TOTALCMD64' -ErrorAction SilentlyContinue).Count -gt 0) {
    throw 'Close all Total Commander processes before repairing icons.'
}
Add-Type -AssemblyName System.Windows.Forms
function Choose-File([string]$title,[string]$name,[string]$filter) {
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    try {
        $dialog.Title = $title
        $dialog.FileName = $name
        $dialog.Filter = $filter
        if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
            throw 'Cancelled: no files were changed.'
        }
        return $dialog.FileName
    } finally { $dialog.Dispose() }
}
if (-not $IniPath) {
    $IniPath = Choose-File 'Select the CURRENT wincmd.ini (TC Help > About)' 'wincmd.ini' 'Total Commander INI|wincmd.ini|INI files|*.ini'
}
$ini = (Resolve-Path -LiteralPath $IniPath -ErrorAction Stop).ProviderPath
if ([IO.Path]::GetFileName($ini) -ine 'wincmd.ini') {
    throw 'Select the current Total Commander wincmd.ini.'
}

Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class TcSafeIconIni {
    [DllImport("kernel32.dll", EntryPoint="GetPrivateProfileStringW", CharSet=CharSet.Unicode)]
    public static extern uint Read(string section, string key, string fallback,
        StringBuilder output, uint capacity, string path);
    [DllImport("kernel32.dll", EntryPoint="WritePrivateProfileStringW", CharSet=CharSet.Unicode, SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool Write(string section, string key, string value, string path);
}
'@
function Get-IniKey([string]$path,[string]$key,[string]$section='Buttonbar') {
    $output = [Text.StringBuilder]::new(32768)
    $length = [TcSafeIconIni]::Read($section,$key,'#TCS_ABSENT#',
        $output,[uint32]$output.Capacity,$path)
    if ($length -ge $output.Capacity - 1) {
        throw "INI value is too long: $key in $path"
    }
    return $output.ToString()
}
function Get-Toolbar([string]$path) {
    $countString = Get-IniKey $path 'Buttoncount'
    $count = 0
    if (-not [int]::TryParse($countString, [ref]$count) -or $count -lt 1 -or $count -gt 10000) {
        throw "Invalid [Buttonbar] Buttoncount in $path"
    }
    $result = @()
    for ($i=1; $i -le $count; $i++) {
        $n = [string]$i
        $result += [pscustomobject]@{
            Number = $i
            Cmd = Get-IniKey $path ('cmd'+$n)
            Param = Get-IniKey $path ('param'+$n)
            Menu = Get-IniKey $path ('menu'+$n)
            Icon = Get-IniKey $path ('button'+$n)
        }
    }
    return ,$result
}
function Expand-ToolbarReference([string]$value,[string]$iniDir) {
    if (-not $value -or $value -eq '#TCS_ABSENT#') { return '' }
    $result = [regex]::Replace($value, '(?i)%COMMANDER_INI_PATH%',
        [Text.RegularExpressions.MatchEvaluator]{param($m) $iniDir})
    if ($result -match '(?i)%COMMANDER_PATH%') {
        if (-not $env:COMMANDER_PATH) { return '' }
        $result = [regex]::Replace($result, '(?i)%COMMANDER_PATH%',
            [Text.RegularExpressions.MatchEvaluator]{param($m) $env:COMMANDER_PATH})
    }
    $result = [Environment]::ExpandEnvironmentVariables($result)
    if (-not [IO.Path]::IsPathRooted($result)) { return '' }
    return $result
}
$iniDir = Split-Path -Parent $ini
if (-not $BarPath) {
    $reference = Get-IniKey $ini 'Buttonbar'
    $BarPath = Expand-ToolbarReference $reference $iniDir
    if (-not $BarPath -or -not (Test-Path -LiteralPath $BarPath -PathType Leaf)) {
        Write-Warning 'The active toolbar could not be identified safely (default or redirected toolbar).'
        $BarPath = Choose-File 'Select your existing active toolbar .bar (NOT a backup)' '' 'Total Commander toolbars|*.bar|All files|*.*'
    }
}
$bar = (Resolve-Path -LiteralPath $BarPath -ErrorAction Stop).ProviderPath
if ([IO.Path]::GetExtension($bar) -ine '.bar') {
    throw 'Select the active .bar file, not a backup or a clone.'
}
if ($bar -match '(?i)(?:^|[\\/])(TCSymbolicLinks|TC-Symbolic-Links-Default)\.bar$') {
    throw 'An old plugin-created shadow toolbar is selected. Restore the original bar selection FIRST with Restore-TC-Toolbar-Only.cmd.'
}
if ((Get-Item -LiteralPath $bar).Length -eq 0) {
    throw 'Active .bar is empty. Refusing to modify it.'
}
$sources = @()
if ($BackupPath) {
    $sources += (Resolve-Path -LiteralPath $BackupPath -ErrorAction Stop).ProviderPath
} else {
    $immutable = $bar + '.tcslinks.bak'
    if (Test-Path -LiteralPath $immutable -PathType Leaf) { $sources += $immutable }
    $name = [IO.Path]::GetFileName($bar)
    $folder = Split-Path -Parent $bar
    $sources += @(Get-ChildItem -LiteralPath $folder -Filter ($name+'.tcslinks.preupdate.*.bak') -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime,Name | ForEach-Object { $_.FullName })
    $sources += @(Get-ChildItem -LiteralPath $folder -Filter ($name+'.before-icon-recovery-*.bak') -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime,Name | ForEach-Object { $_.FullName })
}
$sources = @($sources | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -Unique)
if ($sources.Count -eq 0) {
    throw "No original icon-bearing .bar backups found for $bar. Nothing was modified."
}
$current = @(Get-Toolbar $bar)
$backups = @()
foreach ($source in $sources) {
    if ((Get-Item -LiteralPath $source).Length -eq 0) { continue }
    try {
        $backups += [pscustomobject]@{Path=$source; Buttons=@(Get-Toolbar $source)}
    } catch {
        Write-Warning "Ignored invalid backup: $source"
    }
}
if ($backups.Count -eq 0) { throw 'No valid .bar backups contain button metadata.' }

$restore = @()
$ambiguous = 0
foreach ($button in $current) {
    if ($button.Icon -ne '#TCS_ABSENT#' -and -not [string]::IsNullOrWhiteSpace($button.Icon)) {
        continue
    }
    if (-not $button.Cmd -or $button.Cmd -eq '#TCS_ABSENT#' -or $button.Cmd -eq '0') {
        continue
    }
    $choices = @()
    foreach ($backup in $backups) {
        foreach ($entry in $backup.Buttons) {
            if ($entry.Cmd -cne $button.Cmd -or $entry.Param -cne $button.Param -or
                $entry.Menu -cne $button.Menu) { continue }
            if ($entry.Icon -and $entry.Icon -ne '#TCS_ABSENT#' -and $entry.Icon -ne '0') {
                $choices += [string]$entry.Icon
            }
        }
    }
    $choices = @($choices | Select-Object -Unique)
    if ($choices.Count -eq 1) {
        $restore += [pscustomobject]@{Index=$button.Number; Icon=$choices[0]; Command=$button.Cmd}
    } elseif ($choices.Count -gt 1) {
        $ambiguous++
        Write-Warning "Button $($button.Number): conflicting old icon references; skipped."
    }
}
Write-Host ''
Write-Host 'TC icon recovery - original commands are protected' -ForegroundColor Cyan
Write-Host "Active: $bar"
Write-Host "Backups inspected: $($backups.Count)"
Write-Host "Current buttons: $($current.Count)"
Write-Host "Unambiguous missing icons: $($restore.Count)"
Write-Host "Ambiguous icons skipped: $ambiguous"
if ($restore.Count -eq 0) {
    Write-Host 'No missing icon references could be recovered safely. No files changed.' -ForegroundColor Yellow
    Write-Host 'If whole buttons disappeared, use Restore-TC-Toolbar-Only.cmd first.'
    return
}
foreach ($item in $restore) {
    Write-Host ("  Button {0}: {1} -> icon={2}" -f $item.Index,$item.Command,$item.Icon)
}
Write-Host 'The current toolbar will be snapshotted before changing only missing buttonN values.'
if (-not $Force) {
    if ((Read-Host 'Type RESTORE to apply') -cne 'RESTORE') {
        Write-Host 'Cancelled: no files changed.'
        return
    }
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$rollback = $bar + '.before-icon-recovery-' + $stamp + '-' + [guid]::NewGuid().ToString('N').Substring(0,8) + '.bak'
Copy-Item -LiteralPath $bar -Destination $rollback -ErrorAction Stop
if ((Get-FileHash -LiteralPath $bar -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath $rollback -Algorithm SHA256).Hash) {
    throw 'Rollback copy verification failed. No edits made.'
}
try {
    foreach ($item in $restore) {
        $key = 'button'+$item.Index
        if (-not [TcSafeIconIni]::Write('Buttonbar',$key,$item.Icon,$bar)) {
            throw "Cannot restore $key"
        }
    }
    $after = @(Get-Toolbar $bar)
    if ($after.Count -ne $current.Count) { throw 'Button count changed during recovery.' }
    foreach ($entry in $current) {
        $new = $after[$entry.Number-1]
        if ($new.Cmd -cne $entry.Cmd -or $new.Param -cne $entry.Param -or
            $new.Menu -cne $entry.Menu) {
            throw "Existing command changed during recovery: $($entry.Number)"
        }
        if ($restore.Index -notcontains $entry.Number -and $new.Icon -cne $entry.Icon) {
            throw "Unrelated icon changed during recovery: $($entry.Number)"
        }
    }
    foreach ($item in $restore) {
        if ($after[$item.Index-1].Icon -cne $item.Icon) {
            throw "Icon verification failed for button $($item.Index)"
        }
    }
} catch {
    $message = [string]$_
    Copy-Item -LiteralPath $rollback -Destination $bar -Force -ErrorAction Stop
    throw "Recovery failed; original toolbar restored byte-for-byte. $message"
}
Write-Host ("SUCCESS: {0} missing icon reference(s) restored." -f $restore.Count) -ForegroundColor Green
Write-Host "Rollback snapshot: $rollback"
Write-Host 'wincmd.ini and usercmd.ini were not modified. Restart Total Commander.'
