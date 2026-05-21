# Load hosted-path bootloader into RAM via jztool (USB boot).
param(
    [string]$Bootloader = "",
    [switch]$SkipPrompt
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$jzDir = $PSScriptRoot
$jztool = Join-Path $jzDir "jztool.exe"
if (-not $Bootloader) {
    $Bootloader = Join-Path (Split-Path -Parent $jzDir) "nand-recovery\bootloader.erosq"
}

if (-not (Test-Path -LiteralPath $jztool)) {
    throw "Missing $jztool"
}
if (-not (Test-Path -LiteralPath $Bootloader)) {
    throw "Missing $Bootloader"
}

$hash = (Get-FileHash -LiteralPath $Bootloader -Algorithm MD5).Hash
Write-Host "Bootloader: $Bootloader" -ForegroundColor Cyan
Write-Host "  MD5: $hash (expect CF5BF9028B6DA9A985A98FE272C8DC3A)" -ForegroundColor DarkGray
Write-Host ""
Write-Host "USB boot: power OFF, hold MENU, plug USB, release MENU." -ForegroundColor Yellow
Write-Host "Zadig: WinUSB on device A108:1000. See ..\05-JZTOOL-EMERGENCY.md" -ForegroundColor Yellow
if (-not $SkipPrompt) {
    Read-Host "Press Enter when ready"
}

Push-Location $jzDir
try {
    & ".\jztool.exe" -v erosq load $Bootloader
    $ec = $LASTEXITCODE
} finally {
    Pop-Location
}

if ($ec -ne 0) {
    Write-Host "jztool failed (exit $ec)." -ForegroundColor Red
    exit $ec
}

Write-Host ""
Write-Host "jztool load OK — bootloader in RAM." -ForegroundColor Green
Write-Host "Next: copy nand-recovery\bootloader.erosq to SD ROOT -> Install or update -> remove file." -ForegroundColor Yellow
