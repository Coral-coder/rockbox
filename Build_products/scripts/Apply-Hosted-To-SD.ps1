<#
.SYNOPSIS
  Deploy hosted Rockbox from Build_products to an SD card.

.DESCRIPTION
  Copies hosted rockbox.erosq (+ optional BT bring-up script) to /.rockbox/
  and removes dangerous non-hosted files from the SD card.

.EXAMPLE
  .\Apply-Hosted-To-SD.ps1 -Drive E: -CopyBtBringup
#>
param(
    [string]$Drive = "",
    [switch]$CopyBtBringup
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$kitRoot = Split-Path -Parent $PSScriptRoot
$hostedApp = Join-Path $kitRoot "hosted-on-sd\rockbox.erosq"
$btScript = Join-Path $kitRoot "hosted-on-sd\device-bt-bringup.sh"
$expectedAppMd5 = "1C9812963E9899F54D207CCD3F80085B"

if (-not (Test-Path -LiteralPath $hostedApp)) {
    throw "Missing kit file: $hostedApp"
}

$actual = (Get-FileHash -LiteralPath $hostedApp -Algorithm MD5).Hash
if ($actual -ne $expectedAppMd5) {
    Write-Warning "rockbox.erosq MD5 is $actual (expected $expectedAppMd5). Continue only if intentional."
}

function Resolve-RockboxDir {
    param([string]$RootDrive)
    $d = $RootDrive.TrimEnd('\', '/')
    if (-not $d.EndsWith(':')) { $d += ':' }
    $rb = Join-Path ($d + '\') ".rockbox"
    if (-not (Test-Path -LiteralPath $rb)) {
        New-Item -ItemType Directory -Path $rb -Force | Out-Null
    }
    return $rb
}

if ($Drive) {
    $sdRoot = $Drive.TrimEnd('\', '/')
    if (-not $sdRoot.EndsWith(':')) { $sdRoot += ':' }
} else {
    $vol = @(Get-CimInstance Win32_LogicalDisk | Where-Object { $_.DriveType -eq 2 })
    if ($vol.Count -eq 1) {
        $sdRoot = $vol[0].DeviceID
    } elseif (Test-Path -LiteralPath "E:\") {
        $sdRoot = "E:\"
    } else {
        throw "No SD drive. Run: .\Apply-Hosted-To-SD.ps1 -Drive E:"
    }
}

if (-not (Test-Path -LiteralPath $sdRoot)) {
    throw "Drive not found: $sdRoot"
}

$rockboxDir = Resolve-RockboxDir -RootDrive $sdRoot
Write-Host "Kit:     $kitRoot" -ForegroundColor Cyan
Write-Host "SD root: $sdRoot" -ForegroundColor Yellow
Write-Host "Rockbox: $rockboxDir" -ForegroundColor Yellow

$remove = @(
    (Join-Path $sdRoot "bootloader.erosq"),
    (Join-Path $sdRoot "spl.erosq"),
    (Join-Path $rockboxDir "bootloader.erosq"),
    (Join-Path $rockboxDir "spl.erosq"),
    (Join-Path $rockboxDir "rockbox_main.aigo_erosqn"),
    (Join-Path $rockboxDir "bcm43438.fw")
)
foreach ($p in $remove) {
    if (Test-Path -LiteralPath $p) {
        Remove-Item -LiteralPath $p -Force
        Write-Host "  REMOVED $p" -ForegroundColor DarkYellow
    }
}

Copy-Item -LiteralPath $hostedApp -Destination (Join-Path $rockboxDir "rockbox.erosq") -Force
$hash = (Get-FileHash -LiteralPath (Join-Path $rockboxDir "rockbox.erosq") -Algorithm MD5).Hash
Write-Host "  OK rockbox.erosq MD5=$hash" -ForegroundColor Green

if ($CopyBtBringup) {
    if (-not (Test-Path -LiteralPath $btScript)) {
        throw "Missing $btScript"
    }
    Copy-Item -LiteralPath $btScript -Destination (Join-Path $rockboxDir "device-bt-bringup.sh") -Force
    Write-Host "  OK device-bt-bringup.sh" -ForegroundColor Green
}

Write-Host ""
Write-Host "Hosted SD deploy done." -ForegroundColor Green
Write-Host "  Boot: HiBy launcher -> Rockbox (not bare-metal SD boot)." -ForegroundColor Cyan
Write-Host "  Guides: Build_products\04-RESTORE-TO-HOSTED.md" -ForegroundColor DarkGray
