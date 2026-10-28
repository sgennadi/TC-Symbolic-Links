# Windows-only acceptance attempt against the *real* Total Commander 11.58 x64.
# Uses a pinned, publisher-signed shareware installer in an isolated test folder.
# Reports GUI checks separately: launching a process is not proof of a visible desktop.
# This test does not suppress or bypass Total Commander's shareware prompt.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$WdxPath,
    [string]$OutputPath = 'tc-smoke-results',
    [switch]$RequireFullGui
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$wdx = (Resolve-Path -LiteralPath $WdxPath).Path
if (-not $env:RUNNER_TEMP) { throw 'RUNNER_TEMP is required: this script must run on a disposable Windows CI runner.' }
$work = Join-Path $env:RUNNER_TEMP 'TC-Symbolic-Links-1158-Smoke'
$package = Join-Path $work 'tcmd1158x64.exe'
$unpacked = Join-Path $work 'installer'
$tcDir = Join-Path $work 'totalcmd'
$output = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Force -Path $work, $unpacked, $tcDir, $output | Out-Null

$report = [ordered]@{
    tc_version = '11.58'
    tc_installer_verified = $false
    tc_executable_found = $false
    tc_process_started = $false
    tc_window_found = $false
    tc_wdx_registered = $false
    tc_wdx_loaded_in_process = $false
    tc_custom_columns_requested = $false
    maker_process_started = $false
    maker_host = "rundll32.exe"
    maker_window_found = $false
    maker_hebrew_hardlink_created = $false
    interactive_gui_verified = $false
    errors = @()
    notes = @()
}
$tcProcess = $null
$makerProcess = $null
$failure = $null

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class TcSmokeNative {
    [DllImport("user32.dll", EntryPoint = "FindWindowW", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindow(string className, string windowName);
    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool PostMessage(IntPtr handle, uint message, UIntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", EntryPoint = "SendMessageW", SetLastError = true)]
    public static extern IntPtr SendMessage(IntPtr handle, uint message, UIntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr GetDlgItem(IntPtr parent, int itemId);
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
}
'@

function Wait-ForWindow([string]$className, [int]$processId, [int]$seconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($seconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $hwnd = [TcSmokeNative]::FindWindow($className, $null)
        if ($hwnd -ne [IntPtr]::Zero) {
            [uint32]$found = 0
            [void][TcSmokeNative]::GetWindowThreadProcessId($hwnd, [ref]$found)
            if ($found -eq $processId) { return $hwnd }
        }
        Start-Sleep -Milliseconds 300
    }
    return [IntPtr]::Zero
}
function Modules-ContainWdx([int]$processId) {
    try {
        $p = Get-Process -Id $processId -ErrorAction Stop
        return [bool](@($p.Modules | Where-Object { $_.ModuleName -ieq 'SymlinkInspector.wdx64' }).Count)
    } catch { return $false }
}
# The official trial build displays a reminder on startup. If UI Automation
# can read the displayed request, invoke the *requested* numbered button, as a
# user would. Never select a button if the required number is ambiguous.
# A missing UIA session is logged and does not masquerade as a GUI pass.
function Try-ContinueSharewarePrompt([int]$processId) {
    try {
        Add-Type -AssemblyName UIAutomationClient -ErrorAction Stop
        $root = [System.Windows.Automation.AutomationElement]::RootElement
        $condition = [System.Windows.Automation.PropertyCondition]::new(
            [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $processId)
        $all = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $condition)
        $names = [System.Collections.Generic.List[string]]::new()
        $buttons = [System.Collections.Generic.List[object]]::new()
        for ($i = 0; $i -lt $all.Count; $i++) {
            $element = $all.Item($i)
            $name = $element.Current.Name
            if ([string]::IsNullOrWhiteSpace($name)) { continue }
            $names.Add($name)
            if ($element.Current.ControlType -eq [System.Windows.Automation.ControlType]::Button -and
                $name.Trim() -match '^[123]$') {
                $buttons.Add($element)
            }
        }
        $text = [string]::Join(' ', $names)
        $requested = [regex]::Match($text,
            '(?i)(?:press|click|нажмите|הקש|לחץ).{0,65}?(?:number\s*|button\s*|кнопку\s*|[#№]\s*)?([123])\b')
        if ($requested.Success) {
            foreach ($button in $buttons) {
                if ($button.Current.Name.Trim() -ne $requested.Groups[1].Value) { continue }
                $pattern = $button.GetCurrentPattern(
                    [System.Windows.Automation.InvokePattern]::Pattern)
                $pattern.Invoke()
                Write-Host "TC shareware reminder acknowledged using requested button $($requested.Groups[1].Value)"
                return $true
            }
        }
    } catch { Write-Warning "TC trial reminder cannot be handled via UI Automation: $_" }
    return $false
}
function Write-Screenshot([string]$file) {
    try {
        Add-Type -AssemblyName System.Windows.Forms
        Add-Type -AssemblyName System.Drawing
        $screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
        if ($screen.Width -le 0 -or $screen.Height -le 0) { return }
        $bitmap = [System.Drawing.Bitmap]::new($screen.Width, $screen.Height)
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            try { $graphics.CopyFromScreen($screen.X, $screen.Y, 0, 0, $bitmap.Size) }
            finally { $graphics.Dispose() }
            $bitmap.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
        } finally { $bitmap.Dispose() }
    } catch { Write-Warning "Screenshot unavailable (likely non-interactive runner): $_" }
}

try {
    # Pin the Total Commander 11.58 x64 installer. Hash from ScoopInstaller/Extras
    # totalcommander.json, plus an independent Authenticode publisher check.
    $url = 'https://totalcommander.ch/1158/tcmd1158x64.exe'
    $sha256 = '2281f3827a8746a597a75287f521690f1291bdb535d7e1269768dbe077a13ed4'
    Write-Host "Downloading original Total Commander 11.58 x64 from $url"
    Invoke-WebRequest -Uri $url -OutFile $package -MaximumRedirection 4
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $package).Hash.ToLowerInvariant()
    if ($actual -ne $sha256) { throw "Publisher installer checksum mismatch: $actual" }
    $signature = Get-AuthenticodeSignature -FilePath $package
    if ($signature.Status -ne 'Valid' -or -not $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -notmatch 'Ghisler Software') {
        throw "Total Commander publisher signature is not trusted: $($signature.Status)"
    }
    $report.tc_installer_verified = $true

    # Official installer -> INSTALL.CAB -> e/* contains the portable x64 tree.
    # This is the same extraction scheme used by Scoop. No registration key or
    # user-specific profile is copied into this temporary installation.
    & 7z x -y "-o$unpacked" $package | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Cannot extract Total Commander installer' }
    $cab = Join-Path $unpacked 'INSTALL.CAB'
    if (-not (Test-Path -LiteralPath $cab)) { throw 'INSTALL.CAB missing in original installer' }
    & 7z x -y "-o$tcDir" $cab | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Cannot extract INSTALL.CAB' }
    $e = Join-Path $tcDir 'e'
    if (-not (Test-Path -LiteralPath $e)) { throw 'Portable Total Commander folder e/ not present in CAB' }
    Get-ChildItem -LiteralPath $e -Force | Move-Item -Destination $tcDir -Force
    $tcExe = Join-Path $tcDir 'TOTALCMD64.EXE'
    if (-not (Test-Path -LiteralPath $tcExe)) { throw 'TOTALCMD64.EXE missing from extracted TC' }
    $report.tc_executable_found = $true

    $source = Join-Path $work 'מקורות בעברית 2026'
    $dest = Join-Path $work 'יעד בעברית 2026'
    New-Item -ItemType Directory -Force -Path $source, $dest | Out-Null
    $hebrewFile = Join-Path $source 'קובץ שָלוֹם 42.txt'
    [IO.File]::WriteAllText($hebrewFile, 'Hebrew RTL + niqqud Total Commander smoke test',
        [Text.UTF8Encoding]::new($false))

    # Private temporary Total Commander profile with real WDX registration and
    # an actual custom-column definition. Does not touch any user TC settings.
    $ini = Join-Path $work 'wincmd-ci.ini'
    $content = @"
[Configuration]
OnlyOnce=0
UseIniInProgramDir=7
AutoInstallPlugins=0
[ContentPlugins]
0=$wdx
0_detect=
0_flags=0
[ContentPlugins64]
0=1
[CustomFields]
AutoLoad=0
Titles=TC Symbolic Links
Widths1=150,260,85,110
Headers1=Type\nTarget\nExists\nHardlinks
Contents1=[=SymlinkInspector.Link type]\n[=SymlinkInspector.Link target]\n[=SymlinkInspector.Target exists]\n[=SymlinkInspector.Hardlink count]
Options1=-1|1
"@
    [IO.File]::WriteAllText($ini, $content, [Text.Encoding]::ASCII)
    if (-not ((Get-Content -LiteralPath $ini -Raw).Contains($wdx))) {
        throw 'WDX registration failed'
    }
    $report.tc_wdx_registered = $true

    # Actually launch the real Total Commander process with Hebrew paths in
    # *both* panels. Its trial/shareware reminder is NOT bypassed.
    $tcArgs = @('/N', ('/I="' + $ini + '"'), ('/L="' + $source + '"'), ('/R="' + $dest + '"'), '/P=L')
    $tcProcess = Start-Process -FilePath $tcExe -ArgumentList $tcArgs -WorkingDirectory $tcDir -PassThru
    Start-Sleep -Seconds 5
    $tcProcess.Refresh()
    if ($tcProcess.HasExited) { throw "Total Commander exited immediately (code $($tcProcess.ExitCode))" }
    $report.tc_process_started = $true
    Write-Host "PASS: real TOTALCMD64.EXE started (PID $($tcProcess.Id)) with Hebrew source and target folders"
    $report.notes += 'Trial reminder is processed only by selecting the number requested in its visible UI; a TC license is never supplied or bypassed.'
    [void](Try-ContinueSharewarePrompt $tcProcess.Id)

    # Real TC internal command: cm_SrcCustomView1 = 271. When a live main
    # TTOTAL_CMD window exists, ask TC itself to select the WDX column set.
    # WM_USER + 51 (0x433) is TC's external internal-command message.
    $tcWnd = Wait-ForWindow 'TTOTAL_CMD' $tcProcess.Id 10
    if ($tcWnd -ne [IntPtr]::Zero) {
        $report.tc_window_found = $true
        $report.tc_custom_columns_requested = [TcSmokeNative]::PostMessage(
            $tcWnd, 0x433, [UIntPtr]::new(271), [IntPtr]::Zero)
        Start-Sleep -Seconds 4
        $report.tc_wdx_loaded_in_process = Modules-ContainWdx $tcProcess.Id
        if ($report.tc_wdx_loaded_in_process) {
            Write-Host 'PASS: WDX loaded inside the REAL Total Commander process'
        } else {
            $report.notes += 'TC main window exists, but WDX module load not proven. A shareware dialog or column configuration may block loading.'
        }
        Write-Screenshot (Join-Path $output 'totalcmd.png')
    } else {
        $report.notes += 'No TTOTAL_CMD main window found. A shareware prompt or non-interactive Windows runner may block the GUI.'
    }

    # Launch the Win32 maker GUI INSIDE the WDX module, using only Windows rundll32.exe.
    # TC selection list is UTF-16LE+BOM, just like the real %WL output.
    $listFile = Join-Path $work 'selection-עברית.wl'
    $utf16le = [Text.UnicodeEncoding]::new($false, $true, $true)
    [IO.File]::WriteAllBytes($listFile,
        [byte[]]($utf16le.GetPreamble() + $utf16le.GetBytes($hebrewFile + "`r`n")))
    $rundll32 = Join-Path $env:WINDIR 'System32\rundll32.exe'
    if (-not (Test-Path -LiteralPath $rundll32)) { throw 'Native x64 rundll32.exe missing' }
    $makerArgs = '"' + $wdx + '",TCLinkCreate --list "' + $listFile + '" --dest "' + $dest + '"'
    $makerProcess = Start-Process -FilePath $rundll32 -ArgumentList $makerArgs -PassThru
    Start-Sleep -Seconds 2
    $makerProcess.Refresh()
    if ($makerProcess.HasExited) { throw "rundll32 WDX creator exited early (code $($makerProcess.ExitCode))" }
    $report.maker_process_started = $true

    $makerWnd = Wait-ForWindow 'TCSymbolicLinksMakerWindow' $makerProcess.Id 8
    if ($makerWnd -ne [IntPtr]::Zero) {
        $report.maker_window_found = $true
        # Combo 102, selection 4 = hardlink. Button 105 = Create links.
        $combo = [TcSmokeNative]::GetDlgItem($makerWnd, 102)
        if ($combo -eq [IntPtr]::Zero) { throw 'Maker link-type combo box missing' }
        $selectionResult = [TcSmokeNative]::SendMessage($combo, 0x14E,
            [UIntPtr]::new(4), [IntPtr]::Zero) # CB_SETCURSEL
        if ($selectionResult.ToInt64() -ne 4) { throw 'Maker failed to select Hardlink' }
        if (-not [TcSmokeNative]::PostMessage($makerWnd, 0x111,
            [UIntPtr]::new(105), [IntPtr]::Zero)) { throw 'Maker create command was not posted' }
        $expectedLink = Join-Path $dest ([IO.Path]::GetFileName($hebrewFile))
        $deadline = [DateTime]::UtcNow.AddSeconds(12)
        while ([DateTime]::UtcNow -lt $deadline -and -not (Test-Path -LiteralPath $expectedLink)) {
            Start-Sleep -Milliseconds 250
        }
        if (-not (Test-Path -LiteralPath $expectedLink)) {
            throw 'Maker GUI launched but did not create the Hebrew hardlink'
        }
        if ([IO.File]::ReadAllText($expectedLink) -ne [IO.File]::ReadAllText($hebrewFile)) {
            throw 'Maker GUI hardlink contents mismatch'
        }
        $report.maker_hebrew_hardlink_created = $true
        Write-Host 'PASS: single-WDX Maker window via rundll32 created a Hebrew/niqqud hardlink'
        Write-Screenshot (Join-Path $output 'maker.png')
    } else {
        $report.notes += 'Maker process started; GUI HWND unavailable, so no GUI creation test was claimed.'
    }

    $report.interactive_gui_verified = $report.tc_window_found -and
        $report.tc_wdx_loaded_in_process -and $report.maker_hebrew_hardlink_created
    if ($RequireFullGui -and -not $report.interactive_gui_verified) {
        throw 'Strict GUI acceptance requested but not all GUI checks passed'
    }
}
catch {
    $failure = $_
    $report.errors += $_.Exception.Message
    Write-Error -ErrorAction Continue "TC smoke failed: $_"
}
finally {
    # Never leave third-party GUI processes running in the ephemeral runner.
    foreach ($process in @($makerProcess, $tcProcess)) {
        if ($null -eq $process) { continue }
        try {
            $process.Refresh()
            if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction Stop }
        } catch { Write-Warning "Could not close test process: $_" }
    }
    $json = Join-Path $output 'tc-smoke.json'
    $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $json -Encoding utf8
    Get-Content -LiteralPath $json
}
if ($null -ne $failure) { exit 1 }
if (-not $report.tc_process_started -or -not $report.maker_process_started) { exit 1 }
exit 0
