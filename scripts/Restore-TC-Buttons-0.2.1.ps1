# Restores original TC settings from the backups created by TC Symbolic Links
# 0.2.1. Does not delete any .bar files, plugins or commands. Run after
# closing ALL Total Commander windows. No administrator rights are required.
[CmdletBinding()]
param([string]$IniPath)
$ErrorActionPreference = 'Stop'

$running = @(Get-Process -Name 'TOTALCMD','TOTALCMD64' -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    throw 'Total Commander is running. Close ALL TC windows and retry.'
}
if (-not $IniPath) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = 'Select the active Total Commander wincmd.ini (see Help > About in TC)'
    $dialog.Filter = 'Total Commander wincmd.ini|wincmd.ini|INI files|*.ini'
    $dialog.FileName = 'wincmd.ini'
    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
        Write-Host 'No file selected. No changes made.'
        return
    }
    $IniPath = $dialog.FileName
}
$ini = (Resolve-Path -LiteralPath $IniPath).ProviderPath
if ([IO.Path]::GetFileName($ini) -ine 'wincmd.ini') {
    throw 'Select the active wincmd.ini, not a different INI file.'
}
$backup = $ini + '.tcslinks.bak'
if (-not (Test-Path -LiteralPath $backup -PathType Leaf)) {
    throw "Backup not found: $backup. No changes made."
}
if ((Get-Item -LiteralPath $backup).Length -eq 0) {
    throw "Backup is empty: $backup. No changes made."
}
$folder = Split-Path -Parent $ini
$cmd = Join-Path $folder 'usercmd.ini'
$cmdBackup = $cmd + '.tcslinks.bak'
$hasCmd = (Test-Path -LiteralPath $cmd -PathType Leaf) -and
          (Test-Path -LiteralPath $cmdBackup -PathType Leaf)
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
Write-Host ''
Write-Host "Active INI: $ini"
Write-Host "Restore from: $backup"
Write-Host 'This restores toolbar selection, panel views and other INI settings to their pre-0.2.1 state.'
if ($hasCmd) {
    Write-Host 'Will also restore usercmd.ini from its pre-0.2.1 backup to recover custom button commands.'
}
Write-Host 'The current versions will be preserved in timestamped .before-restore copies.'
$answer = Read-Host 'Type RESTORE to proceed'
if ($answer -cne 'RESTORE') {
    Write-Host 'Cancelled; no changes made.'
    return
}
$iniSaved = $ini + '.before-tcs-restore-' + $timestamp
Copy-Item -LiteralPath $ini -Destination $iniSaved -ErrorAction Stop
if ($hasCmd) {
    $cmdSaved = $cmd + '.before-tcs-restore-' + $timestamp
    Copy-Item -LiteralPath $cmd -Destination $cmdSaved -ErrorAction Stop
}
Copy-Item -LiteralPath $backup -Destination $ini -Force -ErrorAction Stop
if ($hasCmd) {
    Copy-Item -LiteralPath $cmdBackup -Destination $cmd -Force -ErrorAction Stop
}
if ((Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath $ini -Algorithm SHA256).Hash) {
    throw 'INI restore verification failed. Check the saved original and recovery copies.'
}
Write-Host ''
Write-Host 'Restored. Your original toolbar path, previous panel views and custom commands are back.'
Write-Host "Preserved the previous (broken) INI: $iniSaved"
Write-Host 'Start Total Commander again. Do NOT re-run the 0.2.1 automatic setup.'
