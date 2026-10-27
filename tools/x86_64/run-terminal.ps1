$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $ProjectRoot
$Qemu = if ($env:QEMU) { $env:QEMU } else { "qemu-system-x86_64" }
$Mem = if ($env:RIOOS_MEM) { $env:RIOOS_MEM } else { "128M" }
$Accel = if ($env:RIOOS_ACCEL) { $env:RIOOS_ACCEL } else { "tcg" }
$Display = if ($env:RIOOS_DISPLAY) { $env:RIOOS_DISPLAY } else { "sdl" }
if (-not (Get-Command $Qemu -ErrorAction SilentlyContinue)) { throw "qemu-system-x86_64 not found. Install QEMU with SDL/GTK display support." }
if (-not (Test-Path "build-x86_64/qemu-boot.elf") -or -not (Test-Path "storage/disk.img")) {
    Write-Host "[RioOS] Build output missing; building before launch..."
    & (Join-Path $ProjectRoot "windows\build.ps1") --skip-tests
    if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
}
if (-not (Test-Path "build-x86_64/qemu-boot.elf")) { throw "Kernel image was not generated: build-x86_64/qemu-boot.elf" }
if (-not (Test-Path "storage/disk.img")) { throw "Disk image was not generated: storage/disk.img" }
$DisplayHelp = (& $Qemu -display help 2>&1 | Out-String)
if (-not $DisplayHelp.Contains($Display)) {
    $Fallback = $null
    foreach ($Candidate in @("sdl", "gtk")) { if ($DisplayHelp.Contains($Candidate)) { $Fallback = $Candidate; break } }
    if (-not $Fallback) { throw "This QEMU build has no SDL/GTK display backend for a terminal window." }
    Write-Warning "Display '$Display' unavailable; using '$Fallback'."
    $Display = $Fallback
}
Write-Host "RioOS x86-64: opening a plain black QEMU terminal window."
Write-Host "This is the original serial shell, not the pixel UI Terminal app."
& $Qemu -machine "pc,accel=$Accel" -cpu qemu64 -m $Mem `
    -kernel build-x86_64/qemu-boot.elf `
    -drive "if=ide,index=0,format=raw,file=storage/disk.img" `
    -netdev "user,id=n1,ipv4=on,ipv6=off" -device "virtio-net-pci,netdev=n1,disable-modern=on" `
    -vga none -display $Display -serial "vc:100Cx32C" -monitor none -no-reboot -no-shutdown
if ($LASTEXITCODE -ne 0) { throw "QEMU text-console session failed ($LASTEXITCODE)" }
