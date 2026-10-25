# READ ONLY: helps locate the original Total Commander toolbar after 0.2.1/0.2.4.
# Does not modify wincmd.ini, any .bar file, the registry, or Total Commander.
[CmdletBinding()]
param([string]$IniPath)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IniPath) {
    Add-Type -AssemblyName System.Windows.Forms
    $picker = [System.Windows.Forms.OpenFileDialog]::new()
    $picker.Title = 'Choose your actual Total Commander wincmd.ini (Help > About)'
    $picker.Filter = 'Total Commander INI (wincmd.ini)|wincmd.ini|INI files|*.ini'
    $picker.FileName = 'wincmd.ini'
    if ($picker.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { return }
    $IniPath = $picker.FileName
}
$ini = (Resolve-Path -LiteralPath $IniPath).ProviderPath
if ([IO.Path]::GetFileName($ini) -ine 'wincmd.ini') { throw 'Select the active wincmd.ini.' }
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class TcToolbarReadOnly {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, EntryPoint="GetPrivateProfileStringW")]
    public static extern uint Read(string section, string key, string fallback,
        StringBuilder buffer, uint capacity, string path);
}
'@
function Read-Key([string]$file, [string]$section, [string]$key) {
    $text = [Text.StringBuilder]::new(32768)
    [void][TcToolbarReadOnly]::Read($section, $key, '<not configured>', $text,
        [uint32]$text.Capacity, $file)
    return $text.ToString()
}
$backup = $ini + '.tcslinks.bak'
Write-Host 'TC toolbar diagnostics (READ ONLY)' -ForegroundColor Cyan
Write-Host "Config: $ini"
Write-Host "Current [Buttonbar] Buttonbar: $(Read-Key $ini 'Buttonbar' 'Buttonbar')"
if (Test-Path -LiteralPath $backup -PathType Leaf) {
    Write-Host "Before first plugin install: $(Read-Key $backup 'Buttonbar' 'Buttonbar')" -ForegroundColor Green
} else {
    Write-Warning 'Original .tcslinks.bak was not found. Do not guess which toolbar used to be active.'
}
$folder = Split-Path -Parent $ini
$files = @()
$files += Get-ChildItem -LiteralPath $folder -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '(?i)\.bar(?:\.tcslinks\.bak)?$' }
$barReference = Read-Key $ini 'Buttonbar' 'Buttonbar'
if ($barReference -and $barReference -ne '<not configured>' -and
    $barReference -notmatch '%|\*|\?' -and [IO.Path]::IsPathRooted($barReference)) {
    if (Test-Path -LiteralPath $barReference -PathType Leaf) {
        $files += Get-Item -LiteralPath $barReference
    }
}
Write-Host ''
Write-Host 'Toolbar files found in the TC profile (no changes made):' -ForegroundColor Cyan
$files | Sort-Object FullName -Unique | ForEach-Object {
    $buttons = Read-Key $_.FullName 'Buttonbar' 'Buttoncount'
    '{0,4} buttons | {1,8} bytes | {2}' -f $buttons, $_.Length, $_.FullName
}
Write-Host ''
Write-Host 'To restore, first use Restore-TC-Toolbar-Only.cmd with the same wincmd.ini.'
Write-Host 'If it cannot identify your original toolbar, use Configuration > Change button bar'
Write-Host 'and manually select a known-good .bar or its saved copy; do NOT delete old files.'
