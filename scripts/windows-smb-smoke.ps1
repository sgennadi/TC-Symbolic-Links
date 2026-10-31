<#
.SYNOPSIS
    Run isolated real SMB file/directory symlink smoke tests in Windows CI.
.DESCRIPTION
    Creates a uniquely named temporary LOCAL folder and an SMB share restricted
    to the current Windows identity. All test files are created inside this
    temporary share. Removes the mapped drive, share and folder in finally.
    An unavailable SMB service is reported as SKIPPED, not silently PASS.
    Never changes symlink-evaluation policy or existing shares/configuration.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Win64Test,
    [Parameter(Mandatory)][string]$Win32Test,
    [string]$OutputDirectory = 'smb-smoke-results'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$identifier = [Guid]::NewGuid().ToString('N').Substring(0, 12)
$shareName = "TCS$identifier"
$runTemp = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP }
$folder = Join-Path $runTemp "TCS-SMB-$identifier"
$createdShare = $false
$mappedDrive = $null
$report = [ordered]@{
    schema = 1
    environment = 'Windows ephemeral SMB loopback share'
    share_status = 'not-started'
    mapped_drive_status = 'not-started'
    win64 = 'not-started'
    win32 = 'not-started'
    cleanup = 'not-started'
}
$allErrors = [System.Collections.Generic.List[string]]::new()

function Invoke-SmbBinary {
    param([string]$Label, [string]$Binary)
    $binaryPath = (Resolve-Path -LiteralPath $Binary -ErrorAction Stop).ProviderPath
    $rawOutput = @(& $binaryPath 2>&1 | ForEach-Object { [string]$_ })
    $exitCode = $LASTEXITCODE
    $rawOutput | Set-Content -LiteralPath (Join-Path $OutputDirectory "$Label.log") -Encoding utf8
    foreach ($line in $rawOutput) { Write-Host "$Label : $line" }
    if ($exitCode -eq 0) { return 'passed' }
    if ($exitCode -eq 77) {
        Write-Warning "$Label: SMB operations unsupported, denied or blocked by test endpoint."
        return 'skipped-by-server-or-policy'
    }
    throw "$Label: SMB smoke executable failed (exit $exitCode)."
}

try {
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent().Name
    if (-not (Get-Command New-SmbShare -ErrorAction SilentlyContinue)) {
        $report.share_status = 'skipped-no-New-SmbShare-cmdlet'
        Write-Warning 'SMB loopback smoke skipped: New-SmbShare is not available.'
    } else {
        try {
            # The shared path is unique and accessible only to this CI identity.
            New-SmbShare -Name $shareName -Path $folder -FullAccess $identity -CachingMode None -ErrorAction Stop | Out-Null
            $createdShare = $true
            $report.share_status = 'created'
        } catch {
            $report.share_status = 'skipped-provisioning-denied'
            Write-Warning "SMB loopback share could not be created: $($_.Exception.Message)"
        }
        if ($createdShare) {
            $unc = "\\localhost\$shareName"
            if (-not (Test-Path -LiteralPath $unc -PathType Container)) {
                $report.share_status = 'skipped-share-inaccessible'
                Write-Warning 'SMB loopback share exists but the SMB client cannot access it.'
            } else {
                $report.share_status = 'accessible'
                $env:TC_SMB_SMOKE_ROOT = $unc

                # Optional isolated drive mapping to exercise UNC-to-drive normalization.
                $report.mapped_drive_status = 'no-free-drive-or-mapping-blocked'
                foreach ($letter in @('X', 'Y', 'Z', 'W', 'V')) {
                    $drive = "$letter" + ':'
                    if (Test-Path -LiteralPath ($drive + '\')) { continue }
                    & net.exe use $drive $unc /persistent:no *> $null
                    if ($LASTEXITCODE -eq 0) {
                        $mappedDrive = $drive
                        $env:TC_SMB_MAPPED_ROOT = $drive + '\'
                        $report.mapped_drive_status = 'mapped'
                        break
                    }
                }

                $report.win64 = Invoke-SmbBinary -Label 'win64' -Binary $Win64Test
                $report.win32 = Invoke-SmbBinary -Label 'win32' -Binary $Win32Test
            }
        }
    }
} catch {
    $allErrors.Add($_.Exception.Message)
    Write-Warning $_.Exception.Message
} finally {
    Remove-Item Env:\TC_SMB_SMOKE_ROOT -ErrorAction SilentlyContinue
    Remove-Item Env:\TC_SMB_MAPPED_ROOT -ErrorAction SilentlyContinue
    if ($mappedDrive) {
        & net.exe use $mappedDrive /delete /y *> $null
        if ($LASTEXITCODE -ne 0) { $allErrors.Add('Could not remove temporary network drive.') }
    }
    if ($createdShare) {
        try {
            Remove-SmbShare -Name $shareName -Force -Confirm:$false -ErrorAction Stop
        } catch { $allErrors.Add("Could not remove temporary SMB share: $($_.Exception.Message)") }
    }
    if (Test-Path -LiteralPath $folder) {
        try {
            Remove-Item -LiteralPath $folder -Force -Recurse -ErrorAction Stop
        } catch { $allErrors.Add("Could not remove temporary SMB folder: $($_.Exception.Message)") }
    }
    $report.cleanup = if ($allErrors.Count -eq 0) { 'passed' } else { 'failed' }
    $report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (
        Join-Path $OutputDirectory 'result.json') -Encoding utf8
    Write-Host ('SMB smoke status: ' + ($report | ConvertTo-Json -Compress))
}
if ($allErrors.Count -gt 0) {
    throw ('SMB smoke test/cleanup failed: ' + ($allErrors -join '; '))
}
