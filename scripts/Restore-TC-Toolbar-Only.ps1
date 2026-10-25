# Surgical recovery for toolbar regressions in versions 0.2.1 through 0.2.4.
# Recognizes both plugin-owned shadow bars. Restores ONLY the old startup
# [Buttonbar] Buttonbar selector and keeps all other configuration unchanged.
# Does NOT replace the INI, usercmd.ini, any .bar file or any other setting.
[CmdletBinding()]
param(
    [string]$IniPath,
    [switch]$Force
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (@(Get-Process -Name 'TOTALCMD', 'TOTALCMD64' -ErrorAction SilentlyContinue).Count -gt 0) {
    throw 'Close ALL Total Commander processes before restoring the toolbar.'
}
if (-not $IniPath) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = [System.Windows.Forms.OpenFileDialog]::new()
    $dialog.Title = 'Select the wincmd.ini shown in Total Commander Help > About'
    $dialog.Filter = 'Total Commander wincmd.ini|wincmd.ini|INI files|*.ini'
    $dialog.FileName = 'wincmd.ini'
    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
        Write-Host 'Cancelled: nothing changed.'
        return
    }
    $IniPath = $dialog.FileName
}
$ini = (Resolve-Path -LiteralPath $IniPath -ErrorAction Stop).ProviderPath
if ([IO.Path]::GetFileName($ini) -ine 'wincmd.ini') {
    throw 'Select the actual Total Commander wincmd.ini.'
}
$backup = $ini + '.tcslinks.bak'
if (-not (Test-Path -LiteralPath $backup -PathType Leaf)) {
    throw "Pre-0.2.1 backup not found: $backup. Nothing changed."
}
if ((Get-Item -LiteralPath $backup).Length -eq 0) {
    throw 'Backup is empty. Refusing to modify current configuration.'
}

# Windows INI APIs follow Total Commander's ANSI/UTF-16LE configuration file
# conventions and avoid decoding the entire INI in PowerShell as the wrong code page.
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class TcIniRecovery {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, EntryPoint = "GetPrivateProfileStringW")]
    public static extern uint Read(string section, string key, string fallback,
                                   StringBuilder value, uint capacity, string path);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, EntryPoint = "WritePrivateProfileStringW", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool Write(string section, string key, string value, string path);
}
'@
function Get-BarReference([string]$path) {
    $buffer = [Text.StringBuilder]::new(32768)
    [void][TcIniRecovery]::Read('Buttonbar', 'Buttonbar', '#__TCS_ABSENT__#',
        $buffer, [uint32]$buffer.Capacity, $path)
    return $buffer.ToString()
}
$current = Get-BarReference $ini
$original = Get-BarReference $backup
if ($original -eq '#__TCS_ABSENT__#') {
    Write-Host 'The backup does not explicitly select a toolbar; restore the default by removing the plugin entry.'
}
if ($current -notmatch '(?i)(?:^|[\\/])(TCSymbolicLinks|TC-Symbolic-Links-Default)\.bar$') {
    Write-Host "Active toolbar is not one of the plugin-created shadow bars: $current"
    Write-Host 'No change is necessary. Existing settings remain untouched.'
    return
}
if ($original -ieq $current -or
    $original -match '(?i)(?:^|[\\/])(TCSymbolicLinks|TC-Symbolic-Links-Default)\.bar$') {
    throw 'Backup still points to a plugin-created shadow toolbar. Refusing to overwrite the selection with another broken copy.'
}
Write-Host "Current plugin-created toolbar:    $current"
Write-Host "Pre-plugin toolbar reference:     $original"
Write-Host 'ONLY the [Buttonbar] Buttonbar value will be restored.'
Write-Host 'All panel views, other INI preferences, usercmd.ini, and .bar files remain unchanged.'
if (-not $Force) {
    if ((Read-Host 'Type RESTORE to apply only this toolbar repair') -cne 'RESTORE') {
        Write-Host 'Cancelled: nothing changed.'
        return
    }
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$saved = $ini + '.before-toolbar-repair-' + $stamp + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
Copy-Item -LiteralPath $ini -Destination $saved -ErrorAction Stop
$restore = if ($original -eq '#__TCS_ABSENT__#') { $null } else { [string]$original }
$success = [TcIniRecovery]::Write('Buttonbar', 'Buttonbar', $restore, $ini)
$observed = Get-BarReference $ini
# Either missing or empty Buttonbar= selects the default TC startup toolbar.
# Windows INI implementations can encode deletion as an empty key: both states
# are safe, while any remaining shadow-bar value is strictly rejected.
$wasImplicit = $original -eq '#__TCS_ABSENT__#'
$verified = if ($wasImplicit) {
    $observed -eq '#__TCS_ABSENT__#' -or $observed -eq ''
} else {
    $observed -ceq $original
}
if ($wasImplicit -and -not $verified) {
    $success = [TcIniRecovery]::Write('Buttonbar', 'Buttonbar', '', $ini)
    $observed = Get-BarReference $ini
    $verified = $observed -eq ''
}
if (-not $success -or -not $verified) {
    Copy-Item -LiteralPath $saved -Destination $ini -Force
    throw 'Toolbar-only recovery verification failed; the original current INI was restored.'
}
Write-Host 'Restored the previous toolbar selection without changing other settings.'
Write-Host "Saved current INI snapshot: $saved"
Write-Host 'Restart Total Commander to see the original buttons.'
