# TC Symbolic Links Update Manager 0.2.15
# Trusted script embedded as WDX RCDATA; downloaded ZIP contents are DATA ONLY.
# No bundled or downloaded EXE is executed. Windows PowerShell is an OS component.
[CmdletBinding()]
param(
    [ValidateSet('Manual','Auto','Apply','SelfTest')][string]$Mode='Manual',
    [string]$PluginPath,
    [string]$CurrentVersion='0.2.15-preview.0',
    [string]$StageDirectory,
    [string]$TestX86,
    [string]$TestX64
)
Set-StrictMode -Version 2
$ErrorActionPreference='Stop'
$script:RegistryPath='Software\TCSymbolicLinks\Updates'
$script:Api='https://api.github.com/repos/sgennadi/TC-Symbolic-Links/releases?per_page=30'
$script:Package='TC-Symbolic-Links-win32-win64.zip'
$script:Root=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'TCSymbolicLinks\Updates'
$script:Lang=[Globalization.CultureInfo]::CurrentUICulture.TwoLetterISOLanguageName
$script:InjectCopyFailure=$false # Offline CI testing only; not a user setting.

function Say([string]$en,[string]$ru,[string]$he) {
    if ($script:Lang -eq 'ru') { return $ru }
    if ($script:Lang -eq 'he') { return $he }
    return $en
}
function Get-Setting([string]$name,$fallback) {
    $key=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($script:RegistryPath)
    if ($null -eq $key) { return $fallback }
    try { return $key.GetValue($name,$fallback) } finally { $key.Close() }
}
function Set-Setting([string]$name,$value,[Microsoft.Win32.RegistryValueKind]$kind) {
    $key=[Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($script:RegistryPath)
    if ($null -eq $key) { throw 'Cannot write updater preferences.' }
    try { $key.SetValue($name,$value,$kind) } finally { $key.Close() }
}
function Write-Log([string]$message) {
    try {
        New-Item -ItemType Directory -Force -Path $script:Root | Out-Null
        Add-Content -LiteralPath (Join-Path $script:Root 'updater.log') -Encoding UTF8 -Value (
            ('{0:u} {1}' -f [DateTime]::UtcNow,$message))
    } catch { }
}
function Parse-Version([string]$tag) {
    $m=[regex]::Match($tag,'^(?:v)?(\d{1,5})\.(\d{1,5})\.(\d{1,5})(?:-preview(?:[.-](\d{1,8}))?)?$',
        [Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if (-not $m.Success) { return $null }
    try {
        return [pscustomobject]@{
            Number=[version]::new([int]$m.Groups[1].Value,[int]$m.Groups[2].Value,[int]$m.Groups[3].Value)
            Preview=($tag -match '-preview')
            Iteration=$(if ($m.Groups[4].Success) {[int]$m.Groups[4].Value} else {0})
            Tag=$tag
        }
    } catch { return $null }
}
function Compare-Version($a,$b) {
    if ($null -eq $a -or $null -eq $b) { throw 'Unrecognized release version.' }
    $c=$a.Number.CompareTo($b.Number)
    if ($c -ne 0) { return $c }
    if ($a.Preview -and -not $b.Preview) { return -1 }
    if (-not $a.Preview -and $b.Preview) { return 1 }
    if ($a.Iteration -gt $b.Iteration) { return 1 }
    if ($a.Iteration -lt $b.Iteration) { return -1 }
    return 0
}
function Get-PinnedAsset($release) {
    if ($null -eq $release -or $null -eq $release.assets) { return $null }
    $foundAssets=@($release.assets | Where-Object { $_.name -ceq $script:Package })
    if ($foundAssets.Count -ne 1) { return $null }
    $asset=$foundAssets[0]
    $digest=[string]$asset.digest
    if ($digest -cnotmatch '^sha256:([a-fA-F0-9]{64})$') { return $null }
    $hash=$Matches[1].ToLowerInvariant()
    $url=[string]$asset.browser_download_url
    if ($url -cnotmatch '^https://github[.]com/sgennadi/TC-Symbolic-Links/releases/download/[^/?#]+/TC-Symbolic-Links-win32-win64[.]zip$') {
        return $null
    }
    return [pscustomobject]@{Url=$url;Sha256=$hash}
}
function Select-Release($releases,[string]$channel,[string]$currentVersion) {
    $current=Parse-Version $currentVersion
    if ($null -eq $current) { throw 'Invalid installed plugin version.' }
    $best=$null
    foreach($release in @($releases)) {
        if ($null -eq $release -or $release.draft) { continue }
        $version=Parse-Version ([string]$release.tag_name)
        if ($null -eq $version) { continue }
        if ($channel -eq 'Stable' -and ($release.prerelease -or $version.Preview)) { continue }
        if ((Compare-Version $version $current) -le 0) { continue }
        $asset=Get-PinnedAsset $release
        if ($null -eq $asset) { continue }
        if ($null -eq $best -or (Compare-Version $version $best.Version) -gt 0) {
            $best=[pscustomobject]@{Version=$version;Tag=[string]$release.tag_name;AssetUrl=$asset.Url;Sha256=$asset.Sha256}
        }
    }
    return $best
}
function Find-Release([string]$channel) {
    [Net.ServicePointManager]::SecurityProtocol =
        [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    $headers=@{'User-Agent'='TC-Symbolic-Links-UpdateManager/0.2.15';
        Accept='application/vnd.github+json';'X-GitHub-Api-Version'='2022-11-28'}
    $releases=@(Invoke-RestMethod -Uri $script:Api -Headers $headers -TimeoutSec 25 -UseBasicParsing)
    return Select-Release $releases $channel $CurrentVersion
}
function Verify-Hash([string]$path,[string]$expected) {
    if ($expected -cnotmatch '^[a-f0-9]{64}$') { throw 'A trusted SHA-256 is required.' }
    $actual=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -cne $expected) { throw 'SHA-256 mismatch: package rejected before extraction.' }
}
function Test-PE([string]$path,[int]$machine) {
    $stream=[IO.File]::OpenRead($path)
    try {
        if ($stream.Length -lt 512 -or $stream.Length -gt 30000000) { return $false }
        $reader=[IO.BinaryReader]::new($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) { return $false }
        [void]$stream.Seek(0x3c,[IO.SeekOrigin]::Begin)
        $offset=$reader.ReadInt32()
        if ($offset -lt 64 -or $offset -gt $stream.Length-24) { return $false }
        [void]$stream.Seek($offset,[IO.SeekOrigin]::Begin)
        if ($reader.ReadUInt32() -ne 0x00004550) { return $false }
        return ($reader.ReadUInt16() -eq $machine)
    } finally { $stream.Dispose() }
}
function Unpack-Verified([string]$zipPath,[string]$digest,[string]$folder) {
    Verify-Hash $zipPath $digest
    Add-Type -AssemblyName System.IO.Compression
    $required=@('SymlinkInspector.wdx','SymlinkInspector.wdx64','pluginst.inf')
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
    $stream=[IO.File]::OpenRead($zipPath)
    try {
        $archive=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Read,$false)
        try {
            if ($archive.Entries.Count -gt 500) { throw 'Too many ZIP entries.' }
            $entries=@{}
            foreach($entry in $archive.Entries) {
                if ($required -ccontains $entry.FullName) {
                    if ($entries.ContainsKey($entry.FullName)) { throw 'Duplicate archive filename.' }
                    if ($entry.Length -lt 16 -or $entry.Length -gt 30000000) { throw 'Invalid archive entry size.' }
                    $entries.Add($entry.FullName,$entry)
                }
            }
            foreach($name in $required) {
                if (-not $entries.ContainsKey($name)) { throw "Missing expected ZIP payload: $name" }
                $destination=Join-Path $folder $name
                $target=[IO.File]::Open($destination,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
                try {
                    $source=$entries[$name].Open()
                    try { $source.CopyTo($target) } finally { $source.Dispose() }
                } finally { $target.Dispose() }
            }
        } finally { $archive.Dispose() }
    } finally { $stream.Dispose() }
    if (-not (Test-PE (Join-Path $folder 'SymlinkInspector.wdx') 0x014c)) { throw 'Wrong x86 DLL architecture.' }
    if (-not (Test-PE (Join-Path $folder 'SymlinkInspector.wdx64') 0x8664)) { throw 'Wrong x64 DLL architecture.' }
    $inf=Get-Content -LiteralPath (Join-Path $folder 'pluginst.inf') -Raw
    if ($inf -notmatch '(?im)^type=wdx\s*$' -or $inf -notmatch '(?im)^file=SymlinkInspector[.]wdx\s*$') {
        throw 'Unexpected pluginst.inf installer.'
    }
    $hashes=@{}
    foreach($name in $required) {
        $hashes[$name]=(Get-FileHash -LiteralPath (Join-Path $folder $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    return $hashes
}
function Verify-Stage([string]$folder) {
    $manifest=Get-Content -LiteralPath (Join-Path $folder 'manifest.json') -Raw | ConvertFrom-Json
    foreach($name in @('SymlinkInspector.wdx','SymlinkInspector.wdx64','pluginst.inf')) {
        Verify-Hash (Join-Path $folder $name) ([string]$manifest.Files.$name)
    }
    if (-not (Test-PE (Join-Path $folder 'SymlinkInspector.wdx') 0x014c) -or
        -not (Test-PE (Join-Path $folder 'SymlinkInspector.wdx64') 0x8664)) { throw 'Staged PE machine mismatch.' }
}
function Stage-Release($release) {
    if ($null -eq $release) { throw 'No verified update selected.' }
    $tag=$release.Tag -replace '[^0-9A-Za-z_.-]','_'
    $folder=Join-Path $script:Root ('stage-'+$tag+'-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $folder -Force | Out-Null
    try {
        $zipPath=Join-Path $folder $script:Package
        Invoke-WebRequest -Uri $release.AssetUrl -OutFile $zipPath -TimeoutSec 90 -UseBasicParsing -MaximumRedirection 5
        $hashes=Unpack-Verified $zipPath $release.Sha256 $folder
        $manifest=@{Tag=$release.Tag;ZipSha256=$release.Sha256;Files=$hashes}
        $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $folder 'manifest.json') -Encoding UTF8
        # Run only the trusted embedded updater, never scripts inside the ZIP.
        Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $folder 'apply.ps1')
        return $folder
    } catch {
        Remove-Item -LiteralPath $folder -Recurse -Force -ErrorAction SilentlyContinue
        throw
    }
}
function Report-Result([string]$folder,[string]$text) {
    Write-Log $text
    try { Set-Content -LiteralPath (Join-Path $folder 'result.txt') -Value $text -Encoding UTF8 } catch { }
}
function Apply-Staged([string]$folder,[string]$installedPath) {
    Verify-Stage $folder
    $install=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($installedPath))
    if (-not $install -or -not (Test-Path -LiteralPath $installedPath -PathType Leaf)) { throw 'Installed WDX not found.' }
    $names=@('SymlinkInspector.wdx','SymlinkInspector.wdx64','pluginst.inf')
    $backup=Join-Path $install ('.tcslinks-update-backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' +
        [Guid]::NewGuid().ToString('N').Substring(0,6))
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    $existing=@{}
    foreach($name in $names) {
        $path=Join-Path $install $name
        $existing[$name]=Test-Path -LiteralPath $path -PathType Leaf
        if ($existing[$name]) { Copy-Item -LiteralPath $path -Destination (Join-Path $backup $name) -ErrorAction Stop }
    }
    $changed=[Collections.Generic.List[string]]::new()
    try {
        $ready=$false
        # Never terminate TC or silently replace loaded binaries.
        for($i=0;$i -lt 90;$i++) {
            $running=@(Get-Process -Name 'TOTALCMD','TOTALCMD64' -ErrorAction SilentlyContinue)
            if ($running.Count -eq 0) {
                $ready=$true
                foreach($name in $names) {
                    $path=Join-Path $install $name
                    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
                    try {
                        $lock=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
                        $lock.Dispose()
                    } catch { $ready=$false; break }
                }
                if ($ready) { break }
            }
            Start-Sleep -Seconds 2
        }
        if (-not $ready) { throw 'Total Commander or its DLL remains loaded. Close both TC architectures and retry.' }
        foreach($name in $names) {
            $changed.Add($name)
            Copy-Item -LiteralPath (Join-Path $folder $name) -Destination (Join-Path $install $name) -Force -ErrorAction Stop
            # Simulate a mid-update exception to assert byte-for-byte rollback.
            if ($script:InjectCopyFailure) { throw 'Injected offline rollback test failure.' }
        }
        $manifest=Get-Content -LiteralPath (Join-Path $folder 'manifest.json') -Raw | ConvertFrom-Json
        foreach($name in $names) { Verify-Hash (Join-Path $install $name) ([string]$manifest.Files.$name) }
        Report-Result $folder ("SUCCESS: Updated to "+$manifest.Tag+"; backup="+$backup)
        return $backup
    } catch {
        $details=$_.Exception.Message
        $rollback=@()
        foreach($name in $changed) {
            try {
                $target=Join-Path $install $name
                if ($existing[$name]) { Copy-Item -LiteralPath (Join-Path $backup $name) -Destination $target -Force }
                elseif (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
            } catch { $rollback+=($name+': '+$_.Exception.Message) }
        }
        $reason="FAILED: "+$details+"; backup="+$backup
        if ($rollback.Count) { $reason+="; ROLLBACK INCOMPLETE: "+($rollback -join '; ') }
        Report-Result $folder $reason
        throw $reason
    }
}
function Message([string]$text) {
    Add-Type -AssemblyName System.Windows.Forms
    [void][Windows.Forms.MessageBox]::Show($text,'TC Symbolic Links',
        [Windows.Forms.MessageBoxButtons]::OK,[Windows.Forms.MessageBoxIcon]::Information)
}
function Can-WritePluginDirectory([string]$directory) {
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) { return $false }
    $probe=Join-Path $directory ('TCS-WriteTest-'+[Guid]::NewGuid().ToString('N')+'.tmp')
    try {
        $stream=[IO.File]::Open($probe,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        $stream.Dispose()
        return $true
    } catch { return $false }
    finally { Remove-Item -LiteralPath $probe -Force -ErrorAction SilentlyContinue }
}
function Show-Manager($initial) {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    [Windows.Forms.Application]::EnableVisualStyles()
    $f=[Windows.Forms.Form]::new()
    $f.Text='TC Symbolic Links - Update Manager'
    $f.ClientSize=[Drawing.Size]::new(760,345)
    $f.MinimumSize=[Drawing.Size]::new(710,380)
    $f.StartPosition='CenterScreen'
    $f.BackColor=[Drawing.Color]::FromArgb(247,249,253)
    $f.Font=[Drawing.Font]::new('Segoe UI',10)
    $f.AutoScaleMode=[Windows.Forms.AutoScaleMode]::Dpi
    $f.MaximizeBox=$false
    $banner=[Windows.Forms.Panel]::new()
    $banner.Dock='Top';$banner.Height=90;$banner.BackColor=[Drawing.Color]::FromArgb(16,43,86)
    $f.Controls.Add($banner)
    $heading=[Windows.Forms.Label]::new()
    $heading.Text=Say 'Update Manager' 'Менеджер обновлений' 'מנהל העדכונים'
    $heading.Font=[Drawing.Font]::new('Segoe UI',18,[Drawing.FontStyle]::Bold)
    $heading.ForeColor=[Drawing.Color]::White
    $heading.SetBounds(25,12,695,40);$heading.Anchor='Top,Left,Right'
    $banner.Controls.Add($heading)
    $subtitle=[Windows.Forms.Label]::new()
    $subtitle.Text='TC Symbolic Links  -  '+$CurrentVersion
    $subtitle.ForeColor=[Drawing.Color]::FromArgb(183,217,255)
    $subtitle.SetBounds(26,56,695,25);$subtitle.Anchor='Top,Left,Right'
    $banner.Controls.Add($subtitle)
    $status=[Windows.Forms.Label]::new()
    $status.SetBounds(26,109,700,65);$status.Anchor='Top,Left,Right'
    $status.Text=Say 'Choose Check for Updates.' 'Нажмите «Проверить обновления».' 'בחר בדוק עדכונים.'
    $status.Font=[Drawing.Font]::new('Segoe UI',11)
    $f.Controls.Add($status)
    $label=[Windows.Forms.Label]::new()
    $label.Text=Say 'Channel:' 'Канал:' 'ערוץ:'
    $label.SetBounds(26,193,95,30);$f.Controls.Add($label)
    $channel=[Windows.Forms.ComboBox]::new()
    $channel.DropDownStyle=[Windows.Forms.ComboBoxStyle]::DropDownList
    [void]$channel.Items.Add('Preview');[void]$channel.Items.Add('Stable')
    $channel.SelectedItem=if ((Get-Setting 'Channel' 'Preview') -eq 'Stable') {'Stable'} else {'Preview'}
    $channel.SetBounds(124,188,160,30);$f.Controls.Add($channel)
    $never=[Windows.Forms.CheckBox]::new()
    $never.Text=Say 'Never check automatically' 'Никогда не проверять автоматически' 'לעולם לא לבדוק אוטומטית'
    $never.Checked=([int](Get-Setting 'Never' 0) -eq 1)
    $never.SetBounds(312,188,420,36);$never.Anchor='Top,Left,Right'
    $f.Controls.Add($never)
    $check=[Windows.Forms.Button]::new()
    $check.Text=Say 'Check for Updates' 'Проверить обновления' 'בדוק עדכונים'
    $check.BackColor=[Drawing.Color]::FromArgb(34,114,205)
    $check.ForeColor=[Drawing.Color]::White;$check.FlatStyle='Flat'
    $check.SetBounds(26,268,215,44);$check.Anchor='Bottom,Left';$f.Controls.Add($check)
    $install=[Windows.Forms.Button]::new()
    $install.Text=Say 'Install Update' 'Установить обновление' 'התקן עדכון'
    $install.BackColor=[Drawing.Color]::FromArgb(27,151,106)
    $install.ForeColor=[Drawing.Color]::White;$install.FlatStyle='Flat'
    $install.SetBounds(256,268,215,44);$install.Anchor='Bottom,Left'
    $install.Enabled=$false;$f.Controls.Add($install)
    $close=[Windows.Forms.Button]::new()
    $close.Text=Say 'Close' 'Закрыть' 'סגור'
    $close.SetBounds(583,268,145,44);$close.Anchor='Bottom,Right';$f.Controls.Add($close)
    $script:SelectedRelease=$initial
    $check.Add_Click({
        $script:SelectedRelease=$null
        $check.Enabled=$false;$install.Enabled=$false
        $status.Text=Say 'Checking GitHub Releases...' 'Проверка GitHub Releases...' 'בודק מהדורות GitHub...'
        $f.Refresh()
        try {
            $found=Find-Release ([string]$channel.SelectedItem)
            if ($null -eq $found) {
                $status.Text=Say 'No newer verified GitHub Release. CI artifacts are not installable updates.' 'Новых проверенных выпусков нет. Артефакты CI не устанавливаются автоматически.' 'אין גרסה מאומתת חדשה.'
            } else {
                $script:SelectedRelease=$found
                $status.Text=(Say 'Available: ' 'Доступна версия: ' 'גרסה זמינה: ')+$found.Tag+
                    [Environment]::NewLine+(Say 'GitHub SHA-256 will be checked before any file changes.' 'SHA-256 будет проверен до замены файлов.' 'בדיקת SHA-256 לפני עדכון.')
                $install.Enabled=$true
            }
        } catch { $status.Text=(Say 'Update check error: ' 'Ошибка проверки: ' 'שגיאת בדיקה: ')+$_.Exception.Message }
        finally { $check.Enabled=$true }
    })
    $never.Add_CheckedChanged({
        Set-Setting 'Never' ([int]$never.Checked) ([Microsoft.Win32.RegistryValueKind]::DWord)
    })
    $channel.Add_SelectedIndexChanged({
        Set-Setting 'Channel' ([string]$channel.SelectedItem) ([Microsoft.Win32.RegistryValueKind]::String)
        $script:SelectedRelease=$null;$install.Enabled=$false
    })
    $close.Add_Click({$f.Close()})
    $install.Add_Click({
        if ($null -eq $script:SelectedRelease) {return}
        $install.Enabled=$false
        $status.Text=Say 'Downloading and verifying both DLLs...' 'Загрузка и проверка обеих DLL...' 'מוריד ומאמת קבצים...'
        $f.Refresh()
        try {
            $stage=Stage-Release $script:SelectedRelease
            $start=[Diagnostics.ProcessStartInfo]::new()
            $start.FileName=Join-Path $PSHOME 'powershell.exe'
            $start.Arguments=('-NoProfile -NonInteractive -STA -ExecutionPolicy Bypass -File "{0}" -Mode Apply -PluginPath "{1}" -StageDirectory "{2}"' -f (Join-Path $stage 'apply.ps1'),$PluginPath,$stage)
            $targetDirectory=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($PluginPath))
            if (-not (Can-WritePluginDirectory $targetDirectory)) {
                # Program Files and some shared installations need explicit UAC.
                # Never elevate silently, change ACLs, or change user passwords.
                $question=Say 'The plugin directory is protected. Allow Windows UAC elevation to install the VERIFIED update?' 'Папка плагина защищена. Разрешить запрос UAC для установки ПРОВЕРЕННОГО обновления?' 'התיקייה מוגנת. לאשר הרשאות מנהל לעדכון המאומת?'
                $agree=[Windows.Forms.MessageBox]::Show(
                    $question,'TC Symbolic Links',
                    [Windows.Forms.MessageBoxButtons]::YesNo,
                    [Windows.Forms.MessageBoxIcon]::Question)
                if ($agree -ne [Windows.Forms.DialogResult]::Yes) { throw 'Elevation declined; no installed files changed.' }
                $start.UseShellExecute=$true
                $start.Verb='runas'
                $start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
            } else {
                $start.UseShellExecute=$false
                $start.CreateNoWindow=$true
            }
            $start.WorkingDirectory=$stage
            $proc=[Diagnostics.Process]::Start($start)
            if ($null -eq $proc) {throw 'After-close updater process could not start.'}
            Message (Say 'Verified update staged. Close ALL Total Commander windows. The updater will retain rollback backups.' 'Обновление проверено. Закройте ВСЕ окна Total Commander. Старые DLL сохранятся для отката.' 'העדכון מוכן. סגור את כל חלונות Total Commander. נשמר גיבוי.')
            $f.Close()
        } catch {
            $status.Text=(Say 'Update failed: ' 'Ошибка обновления: ' 'שגיאת עדכון: ')+$_.Exception.Message
            Write-Log ('DOWNLOAD_FAILED: '+$_.Exception.Message)
            $install.Enabled=$true
        }
    })
    if ($null -ne $initial) {
        $status.Text=(Say 'Available: ' 'Доступна версия: ' 'גרסה זמינה: ')+$initial.Tag
        $install.Enabled=$true
    }
    [void]$f.ShowDialog()
    $f.Dispose()
}
function Assert([bool]$condition,[string]$reason) {
    if (-not $condition) {throw ('SELFTEST FAIL: '+$reason)}
}
function Run-SelfTest {
    Assert ((Compare-Version (Parse-Version 'v0.2.15-preview.3') (Parse-Version 'v0.2.15-preview.0')) -gt 0) 'preview comparison'
    Assert ((Compare-Version (Parse-Version 'v0.2.15') (Parse-Version 'v0.2.15-preview.99')) -gt 0) 'stable beats preview'
    Assert ((Compare-Version (Parse-Version 'v0.2.14') (Parse-Version 'v0.2.15-preview.0')) -lt 0) 'downgrade prevented'
    Assert ($null -eq (Parse-Version 'v0.2.15-preview.bad')) 'invalid tags rejected'
    $goodAsset=[pscustomobject]@{
        name=$script:Package;digest=('sha256:'+('a'*64))
        browser_download_url='https://github.com/sgennadi/TC-Symbolic-Links/releases/download/v0.2.16-preview.1/TC-Symbolic-Links-win32-win64.zip'
    }
    $candidate=[pscustomobject]@{tag_name='v0.2.16-preview.1';prerelease=$true;draft=$false;assets=@($goodAsset)}
    Assert ($null -ne (Select-Release @($candidate) 'Preview' '0.2.15-preview.0')) 'preview update selection'
    Assert ($null -eq (Select-Release @($candidate) 'Stable' '0.2.15-preview.0')) 'stable channel filtering'
    $evil=[pscustomobject]@{name=$script:Package;digest=('sha256:'+('a'*64));browser_download_url='https://evil.example/unknown.zip'}
    Assert ($null -eq (Get-PinnedAsset ([pscustomobject]@{assets=@($evil)}))) 'foreign URL rejected'
    $unsigned=[pscustomobject]@{name=$script:Package;digest='';browser_download_url=$goodAsset.browser_download_url}
    Assert ($null -eq (Get-PinnedAsset ([pscustomobject]@{assets=@($unsigned)}))) 'missing digest rejected'
    if ($TestX86 -and $TestX64) {
        Assert (Test-PE $TestX86 0x014c) 'x86 PE architecture'
        Assert (Test-PE $TestX64 0x8664) 'x64 PE architecture'
        $temp=Join-Path ([IO.Path]::GetTempPath()) ('TCSUpdateSelfTest-'+[Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $temp -Force | Out-Null
        try {
            $input=Join-Path $temp 'source'
            New-Item -ItemType Directory -Path $input | Out-Null
            Copy-Item -LiteralPath $TestX86 -Destination (Join-Path $input 'SymlinkInspector.wdx')
            Copy-Item -LiteralPath $TestX64 -Destination (Join-Path $input 'SymlinkInspector.wdx64')
            Set-Content -LiteralPath (Join-Path $input 'pluginst.inf') -Encoding ASCII -Value @('[plugininstall]','type=wdx','file=SymlinkInspector.wdx','version=0.2.15-preview')
            Add-Type -AssemblyName System.IO.Compression.FileSystem
            $zip=Join-Path $temp $script:Package
            [IO.Compression.ZipFile]::CreateFromDirectory($input,$zip)
            $hash=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
            $out=Join-Path $temp 'output'
            $files=Unpack-Verified $zip $hash $out
            Assert ($files.Count -eq 3) 'ZIP payload count'
            Verify-Hash (Join-Path $out 'SymlinkInspector.wdx64') ([string]$files['SymlinkInspector.wdx64'])
            $manifest=@{Tag='v0.2.15-preview.0';ZipSha256=$hash;Files=$files}
            $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $out 'manifest.json') -Encoding UTF8
            $installed=Join-Path $temp 'already-installed'
            New-Item -ItemType Directory -Path $installed | Out-Null
            foreach($name in @('SymlinkInspector.wdx','SymlinkInspector.wdx64','pluginst.inf')) {
                Copy-Item -LiteralPath (Join-Path $input $name) -Destination (Join-Path $installed $name)
            }
            Set-Content -LiteralPath (Join-Path $installed 'pluginst.inf') -Encoding ASCII -Value @('[plugininstall]','type=wdx','file=SymlinkInspector.wdx','version=0.2.14-preview')
            $oldInfo=(Get-FileHash -LiteralPath (Join-Path $installed 'pluginst.inf') -Algorithm SHA256).Hash.ToLowerInvariant()
            $custom=Join-Path $installed 'usercmd.ini'
            Set-Content -LiteralPath $custom -Encoding UTF8 -Value 'Keep my custom commands unchanged'
            $oldCustom=(Get-FileHash -LiteralPath $custom -Algorithm SHA256).Hash.ToLowerInvariant()
            $script:InjectCopyFailure=$true
            $didRollback=$false
            try {
                Apply-Staged $out (Join-Path $installed 'SymlinkInspector.wdx64') | Out-Null
            } catch { $didRollback=$true }
            finally { $script:InjectCopyFailure=$false }
            Assert $didRollback 'mid-copy failure simulated'
            Verify-Hash (Join-Path $installed 'pluginst.inf') $oldInfo
            Verify-Hash $custom $oldCustom
            foreach($name in @('SymlinkInspector.wdx','SymlinkInspector.wdx64')) {
                Verify-Hash (Join-Path $installed $name) (Get-FileHash -LiteralPath (Join-Path $input $name) -Algorithm SHA256).Hash.ToLowerInvariant()
            }
            $backup=Apply-Staged $out (Join-Path $installed 'SymlinkInspector.wdx64')
            Assert (Test-Path -LiteralPath $backup -PathType Container) 'backup created'
            Verify-Hash (Join-Path $installed 'pluginst.inf') ([string]$files['pluginst.inf'])
            Verify-Hash $custom $oldCustom
            $wrong='0'+$hash.Substring(1)
            if ($wrong -eq $hash) { $wrong='1'+$hash.Substring(1) }
            $rejected=$false
            try { Unpack-Verified $zip $wrong (Join-Path $temp 'reject') | Out-Null } catch { $rejected=$true }
            Assert $rejected 'invalid digest must fail closed'
        } finally { Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue }
    }
    Write-Output 'PASS: Offline Update Manager version/channel/digest/URL/PE/ZIP tests'
}
try {
    switch($Mode) {
        'SelfTest' {Run-SelfTest;exit 0}
        'Apply' {
            $backup=Apply-Staged $StageDirectory $PluginPath
            Message ((Say 'Update installed. Restart Total Commander.' 'Обновление установлено. Перезапустите Total Commander.' 'העדכון הותקן. הפעל מחדש את Total Commander.')+[Environment]::NewLine+$backup)
            exit 0
        }
        'Auto' {
            if ([int](Get-Setting 'Never' 0) -eq 1) {exit 0}
            $now=[DateTime]::UtcNow.ToFileTimeUtc()
            [long]$last=Get-Setting 'LastCheckUtc' ([long]0)
            if ($last -gt 0 -and ($now-$last) -lt [long]864000000000) {exit 0}
            Set-Setting 'LastCheckUtc' ([long]$now) ([Microsoft.Win32.RegistryValueKind]::QWord)
            $release=Find-Release ([string](Get-Setting 'Channel' 'Preview'))
            if ($null -eq $release) {exit 0}
            Add-Type -AssemblyName System.Windows.Forms
            $answer=[Windows.Forms.MessageBox]::Show(
                ((Say 'Update available: ' 'Доступно обновление: ' 'גרסה חדשה: ')+$release.Tag+
                [Environment]::NewLine+(Say 'Open Update Manager?' 'Открыть менеджер обновлений?' 'לפתוח מנהל עדכונים?')),
                'TC Symbolic Links',[Windows.Forms.MessageBoxButtons]::YesNo,[Windows.Forms.MessageBoxIcon]::Information)
            if ($answer -eq [Windows.Forms.DialogResult]::Yes) {Show-Manager $release}
            exit 0
        }
        'Manual' {Show-Manager $null;exit 0}
    }
} catch {
    Write-Log ('ERROR ['+$Mode+']: '+$_.Exception.Message)
    if ($Mode -eq 'SelfTest') {
        Write-Output ('SELFTEST_FAIL: '+$_.Exception.ToString())
        Write-Output ('SELFTEST_STACK: '+$_.ScriptStackTrace)
    }
    if ($Mode -ne 'Auto' -and $Mode -ne 'SelfTest') {
        Message ((Say 'Update failed: ' 'Ошибка обновления: ' 'שגיאת עדכון: ')+$_.Exception.Message)
    }
    exit 1
}
