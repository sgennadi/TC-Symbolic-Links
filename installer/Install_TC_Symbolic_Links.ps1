#Requires -Version 5.1
# TC Symbolic Links 0.2.16 - architecture-selective installer.
# Only one native WDX is installed. Preserve other Total Commander settings.
[CmdletBinding()]
param(
    [ValidateSet('Auto','x86','x64')][string]$Architecture='Auto',
    [string]$IniPath='',
    [string]$TcExe='',
    [string]$Destination='',
    [switch]$NonInteractive,
    [switch]$TestOnly
)
Set-StrictMode -Version 2
$ErrorActionPreference='Stop'
$script:Root=Split-Path -Parent $MyInvocation.MyCommand.Path
$script:Lang=[Globalization.CultureInfo]::CurrentUICulture.TwoLetterISOLanguageName
$script:InvokingTc=''
function Txt([string]$en,[string]$ru,[string]$he) {
    if ($script:Lang -eq 'ru') {return $ru}
    if ($script:Lang -eq 'he') {return $he}
    return $en
}
function Dialog([string]$message,[bool]$errorFlag=$false) {
    if ($NonInteractive) {Write-Output $message;return}
    Add-Type -AssemblyName System.Windows.Forms
    $icon=if($errorFlag){[Windows.Forms.MessageBoxIcon]::Error}else{[Windows.Forms.MessageBoxIcon]::Information}
    [void][Windows.Forms.MessageBox]::Show($message,'TC Symbolic Links Installer',[Windows.Forms.MessageBoxButtons]::OK,$icon)
}
function Prepare-INI {
    if ('TCSIniAPI' -as [type]) {return}
    $native=@'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class TCSIniAPI {
 [DllImport("kernel32.dll", EntryPoint="GetPrivateProfileStringW", CharSet=CharSet.Unicode)]
 public static extern uint Get(string section,string name,string fallback,StringBuilder value,uint length,string path);
 [DllImport("kernel32.dll", EntryPoint="WritePrivateProfileStringW", CharSet=CharSet.Unicode,SetLastError=true)]
 [return: MarshalAs(UnmanagedType.Bool)]
 public static extern bool Write(string section,string name,string value,string path);
}
'@
    Add-Type -TypeDefinition $native
}
function Read-INI([string]$file,[string]$section,[string]$name) {
    $output=[Text.StringBuilder]::new(32768)
    [void][TCSIniAPI]::Get($section,$name,'',$output,[uint32]$output.Capacity,$file)
    return $output.ToString()
}
function Write-INI([string]$file,[string]$section,[string]$name,[AllowNull()][string]$value) {
    if (-not [TCSIniAPI]::Write($section,$name,$value,$file)) {
        throw ('Cannot update '+$section+'/'+$name)
    }
}
function Parent-Tc {
    $id=[int]$PID
    for($depth=0;$depth -lt 12;$depth++) {
        try {$p=Get-CimInstance Win32_Process -Filter ('ProcessId = '+$id) -ErrorAction Stop}
        catch {break}
        if($null -eq $p){break}
        if($p.Name -ieq 'TOTALCMD64.EXE'){return [pscustomobject]@{Arch='x64';Path=[string]$p.ExecutablePath}}
        if($p.Name -ieq 'TOTALCMD.EXE'){return [pscustomobject]@{Arch='x86';Path=[string]$p.ExecutablePath}}
        $next=[int]$p.ParentProcessId
        if($next -le 0 -or $next -eq $id){break}
        $id=$next
    }
    return $null
}
function Select-Architecture {
    if($Architecture -ne 'Auto'){return $Architecture}
    if($TcExe){
        $name=[IO.Path]::GetFileName($TcExe)
        if($name -ieq 'TOTALCMD64.EXE'){return 'x64'}
        if($name -ieq 'TOTALCMD.EXE'){return 'x86'}
        throw 'TcExe must refer to TOTALCMD.EXE or TOTALCMD64.EXE.'
    }
    $caller=Parent-Tc
    if($null -ne $caller){
        $script:InvokingTc=$caller.Path
        return $caller.Arch
    }
    $p64=@(Get-Process -Name TOTALCMD64 -ErrorAction SilentlyContinue)
    $p32=@(Get-Process -Name TOTALCMD -ErrorAction SilentlyContinue)
    if($p64.Count -gt 0 -and $p32.Count -eq 0){return 'x64'}
    if($p32.Count -gt 0 -and $p64.Count -eq 0){return 'x86'}
    if($NonInteractive){throw 'Cannot auto-detect Total Commander. Specify -Architecture x64 or x86.'}
    Add-Type -AssemblyName System.Windows.Forms
    $question=Txt 'Select target Total Commander: Yes = x64; No = x86; Cancel = abort.' 'Выберите версию Total Commander: Да = x64; Нет = x86; Отмена = выход.' 'בחר גרסה: כן = x64, לא = x86, ביטול = יציאה.'
    $choice=[Windows.Forms.MessageBox]::Show($question,'TC Symbolic Links Installer',[Windows.Forms.MessageBoxButtons]::YesNoCancel,[Windows.Forms.MessageBoxIcon]::Question)
    if($choice -eq [Windows.Forms.DialogResult]::Yes){return 'x64'}
    if($choice -eq [Windows.Forms.DialogResult]::No){return 'x86'}
    throw 'Installation cancelled.'
}
function Find-INI {
    if($IniPath){return [IO.Path]::GetFullPath($IniPath)}
    if($env:COMMANDER_INI -and (Test-Path -LiteralPath $env:COMMANDER_INI -PathType Leaf)){
        return [IO.Path]::GetFullPath($env:COMMANDER_INI)
    }
    $candidates=[Collections.Generic.List[string]]::new()
    foreach($key in @('HKCU:\Software\Ghisler\Total Commander','HKLM:\Software\Ghisler\Total Commander','HKLM:\Software\WOW6432Node\Ghisler\Total Commander')){
        try {
            $value=Get-ItemProperty -LiteralPath $key -ErrorAction Stop
            if($value.IniFileName){$candidates.Add([Environment]::ExpandEnvironmentVariables([string]$value.IniFileName))}
        }catch{}
    }
    if($env:APPDATA){$candidates.Add((Join-Path $env:APPDATA 'Ghisler\wincmd.ini'))}
    if($TcExe){$candidates.Add((Join-Path ([IO.Path]::GetDirectoryName($TcExe)) 'wincmd.ini'))}
    if($script:InvokingTc){$candidates.Add((Join-Path ([IO.Path]::GetDirectoryName($script:InvokingTc)) 'wincmd.ini'))}
    foreach($p in $candidates){if($p -and (Test-Path -LiteralPath $p -PathType Leaf)){return [IO.Path]::GetFullPath($p)}}
    if($NonInteractive){throw 'wincmd.ini not found. Specify -IniPath.'}
    Add-Type -AssemblyName System.Windows.Forms
    $select=[Windows.Forms.OpenFileDialog]::new()
    $select.Filter='Total Commander config|wincmd.ini|INI files|*.ini'
    $select.Title=Txt 'Choose your Total Commander wincmd.ini' 'Выберите wincmd.ini вашего Total Commander' 'בחר wincmd.ini'
    if($select.ShowDialog() -ne [Windows.Forms.DialogResult]::OK){throw 'Configuration selection cancelled.'}
    return [IO.Path]::GetFullPath($select.FileName)
}
function Detect-Slot([string]$ini){
    $free=-1;$registered=-1
    for($n=0;$n -lt 1024;$n++){
        $value=Read-INI $ini 'ContentPlugins' ([string]$n)
        if(-not $value -and $free -eq -1){$free=$n}
        if($value -match '(?i)(?:^|[\\/])SymlinkInspector[.]wdx(?:64)?$' -and $value -match '(?i)TCSymbolicLinks'){
            if($registered -ge 0){throw 'Multiple old registrations; refusing to replace them automatically.'}
            $registered=$n
        }
    }
    if($registered -ge 0){return $registered}
    if($free -ge 0){return $free}
    throw 'No available WDX registration slot.'
}
function Check-PE([string]$path,[int]$machine) {
    if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw 'WDX payload missing.'}
    $file=[IO.File]::OpenRead($path)
    try{
        if($file.Length -lt 512){throw 'WDX truncated.'}
        $reader=[IO.BinaryReader]::new($file)
        if($reader.ReadUInt16() -ne 0x5A4D){throw 'WDX MZ header invalid.'}
        [void]$file.Seek(0x3c,[IO.SeekOrigin]::Begin)
        $offset=$reader.ReadInt32()
        if($offset -lt 64 -or $offset -gt $file.Length-24){throw 'WDX PE offset invalid.'}
        [void]$file.Seek($offset,[IO.SeekOrigin]::Begin)
        if($reader.ReadUInt32() -ne 0x00004550){throw 'WDX PE header invalid.'}
        if($reader.ReadUInt16() -ne $machine){throw 'Wrong WDX machine architecture.'}
    }finally{$file.Dispose()}
}
function Wait-For-TcExit {
    $running=@(Get-Process -Name 'TOTALCMD64','TOTALCMD' -ErrorAction SilentlyContinue)
    if($running.Count -eq 0){return}
    if($NonInteractive){throw 'Close all Total Commander windows before modifying wincmd.ini.'}
    Dialog (Txt 'Please close ALL Total Commander windows now. The installer will then write the plugin registration safely.' 'Закройте ВСЕ окна Total Commander. Только после выхода TC установщик безопасно запишет регистрацию плагина.' 'סגור את כל חלונות Total Commander לפני עדכון ההגדרות.')
    for($attempt=0;$attempt -lt 90;$attempt++){
        if(@(Get-Process -Name 'TOTALCMD64','TOTALCMD' -ErrorAction SilentlyContinue).Count -eq 0){return}
        Start-Sleep -Seconds 2
    }
    throw 'Total Commander remains open. Installation cancelled without changing settings.'
}
function Install-Architecture([string]$arch,[string]$ini){
    Prepare-INI
    $name=if($arch -eq 'x64'){'SymlinkInspector.wdx64'}else{'SymlinkInspector.wdx'}
    $machine=if($arch -eq 'x64'){0x8664}else{0x014c}
    $source=Join-Path (Join-Path $script:Root ('Payload\'+$arch)) $name
    Check-PE $source $machine
    if(-not (Test-Path -LiteralPath $ini -PathType Leaf)){throw 'wincmd.ini does not exist.'}
    $targetDir=if($Destination){[IO.Path]::GetFullPath($Destination)}else{
        Join-Path (Join-Path ([IO.Path]::GetDirectoryName($ini)) 'plugins\wdx\TCSymbolicLinks') $arch
    }
    $targetDir=[IO.Path]::GetFullPath($targetDir)
    if($targetDir.TrimEnd('\') -ieq $script:Root.TrimEnd('\')){throw 'Cannot install over unpacked source.'}
    $target=Join-Path $targetDir $name
    $other=if($arch -eq 'x64'){'SymlinkInspector.wdx'}else{'SymlinkInspector.wdx64'}
    if(Test-Path -LiteralPath (Join-Path $targetDir $other) -PathType Leaf){
        throw 'Refusing to overwrite a shared x86/x64 plugin folder. Choose the architecture-specific destination.'
    }
    $slot=Detect-Slot $ini
    $key=[string]$slot
    $iniBackup=$ini+'.tcslinks.preinstall.'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'.'+[Guid]::NewGuid().ToString('N').Substring(0,8)+'.bak'
    New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
    Copy-Item -LiteralPath $ini -Destination $iniBackup -ErrorAction Stop
    $previous=Test-Path -LiteralPath $target -PathType Leaf
    $moduleBackup=''
    if($previous){
        $backupDir=Join-Path $targetDir '.tcslinks-backups'
        New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
        $moduleBackup=Join-Path $backupDir ($name+'.'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'.'+[Guid]::NewGuid().ToString('N')+'.bak')
        Copy-Item -LiteralPath $target -Destination $moduleBackup -ErrorAction Stop
    }
    $touched=$false
    try{
        Copy-Item -LiteralPath $source -Destination $target -Force -ErrorAction Stop
        $touched=$true
        if((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -cne (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash){
            throw 'Installed WDX hash mismatch.'
        }
        Write-INI $ini 'ContentPlugins' $key $target
        Write-INI $ini 'ContentPlugins' ($key+'_detect') ''
        Write-INI $ini 'ContentPlugins' ($key+'_flags') '0'
        if($arch -eq 'x64'){Write-INI $ini 'ContentPlugins64' $key '1'}else{Write-INI $ini 'ContentPlugins64' $key $null}
        if((Read-INI $ini 'ContentPlugins' $key) -cne $target){throw 'WDX registration did not persist.'}
        return [pscustomobject]@{Architecture=$arch;Module=$target;Slot=$slot;IniBackup=$iniBackup}
    }catch{
        $problem=$_.Exception.Message
        $rollback=@()
        try{Copy-Item -LiteralPath $iniBackup -Destination $ini -Force -ErrorAction Stop}catch{$rollback+=('INI: '+$_.Exception.Message)}
        if($touched){
            try{
                if($previous){Copy-Item -LiteralPath $moduleBackup -Destination $target -Force -ErrorAction Stop}
                else{Remove-Item -LiteralPath $target -Force -ErrorAction Stop}
            }catch{$rollback+=('WDX: '+$_.Exception.Message)}
        }
        if($rollback.Count){$problem+='; ROLLBACK INCOMPLETE: '+($rollback -join '; ')}
        throw $problem
    }
}
try{
    $chosen=Select-Architecture
    $ini=Find-INI
    if($TestOnly){Write-Output ('SELECTED_ARCHITECTURE='+$chosen);Write-Output ('TC_INI='+$ini);exit 0}
    Wait-For-TcExit
    $result=Install-Architecture $chosen $ini
    $text=(Txt 'Installed only the selected WDX architecture.' 'Установлена только подходящая разрядность WDX.' 'הותקנה גרסת WDX המתאימה בלבד.')
    $text+=[Environment]::NewLine+'Architecture: '+$result.Architecture
    $text+=[Environment]::NewLine+'Module: '+$result.Module
    $text+=[Environment]::NewLine+'INI backup: '+$result.IniBackup
    $text+=[Environment]::NewLine+(Txt 'Restart Total Commander. On first load confirm the safe three-button setup.' 'Перезапустите Total Commander. Подтвердите безопасную настройку трёх кнопок.' 'הפעל מחדש את Total Commander להגדרת הכפתורים.')
    Dialog $text
    exit 0
}catch{
    Dialog ((Txt 'Installation failed: ' 'Ошибка установки: ' 'שגיאת התקנה: ')+$_.Exception.Message) $true
    exit 1
}
