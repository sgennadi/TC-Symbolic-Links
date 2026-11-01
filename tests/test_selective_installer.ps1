#Requires -Version 5.1
# Regression: selective installer must NEVER install both architectures.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Win32,[Parameter(Mandatory)][string]$Win64)
Set-StrictMode -Version 2
$ErrorActionPreference='Stop'
$root=Join-Path ([IO.Path]::GetTempPath()) ('TC-Select-Arch-'+[Guid]::NewGuid().ToString('N'))
function Check([bool]$ok,[string]$msg){if(-not $ok){throw "FAIL: $msg"}}
function Hash([string]$path){return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
try{
    New-Item -ItemType Directory -Path $root | Out-Null
    $pkg=Join-Path $root 'extract'
    foreach($path in @($pkg,(Join-Path $pkg 'Payload\x64'),(Join-Path $pkg 'Payload\x86'))){
        New-Item -ItemType Directory -Path $path -Force | Out-Null
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\installer\Install_TC_Symbolic_Links.ps1') -Destination $pkg
    Copy-Item -LiteralPath $Win64 -Destination (Join-Path $pkg 'Payload\x64\SymlinkInspector.wdx64')
    Copy-Item -LiteralPath $Win32 -Destination (Join-Path $pkg 'Payload\x86\SymlinkInspector.wdx')
    $install=Join-Path $pkg 'Install_TC_Symbolic_Links.ps1'
    foreach($arch in @('x64','x86')){
        $profile=Join-Path $root ('profile-'+$arch)
        New-Item -ItemType Directory -Path $profile | Out-Null
        $ini=Join-Path $profile 'wincmd.ini'
        $bar=Join-Path $profile 'MY.BAR'
        $userCmd=Join-Path $profile 'usercmd.ini'
        # Original unrelated user commands, toolbars and custom fields.
        Set-Content -LiteralPath $ini -Encoding Unicode -Value @('[Buttonbar]','Buttonbar=MY.BAR','[ContentPlugins]','0=C:\Other\Unrelated.wdx','0_detect=','0_flags=0','[CustomFields]','Titles=Personal')
        Set-Content -LiteralPath $bar -Encoding Unicode -Value @('[Buttonbar]','Buttoncount=2','cmd1=cm_Copy','menu1=Keep Me')
        Set-Content -LiteralPath $userCmd -Encoding Unicode -Value @('[em_custom]','cmd=notepad.exe','param=Personal')
        $iniOld=Get-Content -LiteralPath $ini -Raw
        $barOld=Hash $bar
        $userOld=Hash $userCmd
        $plugin=Join-Path $profile ('plugins\wdx\TCSymbolicLinks\'+$arch)
        $name=if($arch -eq 'x64'){'SymlinkInspector.wdx64'}else{'SymlinkInspector.wdx'}
        $other=if($arch -eq 'x64'){'SymlinkInspector.wdx'}else{'SymlinkInspector.wdx64'}
        $args=@('-NoLogo','-NoProfile','-ExecutionPolicy','Bypass','-File',$install,
            '-Architecture',$arch,'-IniPath',$ini,'-Destination',$plugin,'-NonInteractive')
        & powershell.exe @args
        Check ($LASTEXITCODE -eq 0) ('Installer failed on '+$arch)
        Check (Test-Path -LiteralPath (Join-Path $plugin $name) -PathType Leaf) ('Missing '+$name)
        Check (-not (Test-Path -LiteralPath (Join-Path $plugin $other))) ('Wrong architecture installed for '+$arch)
        Check ((Hash (Join-Path $plugin $name)) -eq (Hash $(if($arch -eq 'x64'){$Win64}else{$Win32}))) ('Module hash mismatch '+$arch)
        Check ((Hash $bar) -eq $barOld) ('Existing buttons changed for '+$arch)
        Check ((Hash $userCmd) -eq $userOld) ('Personal usercmd changed for '+$arch)
        $text=Get-Content -LiteralPath $ini -Raw
        Check ($text.Contains('0=C:\Other\Unrelated.wdx')) 'An unrelated content plugin changed'
        Check ($text.Contains('Titles=Personal')) 'Unrelated custom columns changed'
        Check ($text.Contains('SymlinkInspector.wdx')) ('Missing WDX registration '+$arch)
        if($arch -eq 'x64'){Check ($text -match '(?im)^\s*1=1\s*$') 'Missing [ContentPlugins64] x64 flag'}
        Check (@(Get-ChildItem -LiteralPath $plugin -Filter 'SymlinkInspector.wdx*').Count -eq 1) 'Two DLL variants present!'
        # Idempotent reinstall must preserve plugin slot and untouched user bar.
        & powershell.exe @args
        Check ($LASTEXITCODE -eq 0) ('Second installer failed on '+$arch)
        Check (@(Get-ChildItem -LiteralPath $plugin -Filter 'SymlinkInspector.wdx*').Count -eq 1) 'Second install added wrong-arch DLL'
        Check ((Hash $bar) -eq $barOld -and (Hash $userCmd) -eq $userOld) 'Second install modified user files'
        $after=Get-Content -LiteralPath $ini -Raw
        $lines=$after.Split([char[]]@(13,10),[StringSplitOptions]::RemoveEmptyEntries)
        $registrations=@($lines | Where-Object { $_ -match '^[0-9]+[ ]*=.*SymlinkInspector[.]wdx(?:64)?$' })
        Write-Host ('REGISTERED_WDX_COUNT='+$registrations.Count+' ARCH='+$arch)
        Check ($registrations.Count -eq 1) ('Expected only one registered plugin DLL on '+$arch)
        Write-Host ('PASS: '+$arch+' installs ONLY '+$name+'; unrelated commands and columns unchanged; re-install idempotent')
    }
    Write-Host 'PASS: one outer installer, correct architecture for both TC variants, no extra DLL'
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
