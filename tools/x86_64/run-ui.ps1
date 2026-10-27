$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $ProjectRoot
$Qemu = if ($env:QEMU) { $env:QEMU } else { "qemu-system-x86_64" }
$Mem = if ($env:RIOOS_MEM) { $env:RIOOS_MEM } else { "128M" }
$Accel = if ($env:RIOOS_ACCEL) { $env:RIOOS_ACCEL } else { "tcg" }
$Display = if ($env:RIOOS_DISPLAY) { $env:RIOOS_DISPLAY } else { "sdl" }
if (-not (Get-Command $Qemu -ErrorAction SilentlyContinue)) { throw "qemu-system-x86_64 not found." }
if (-not (Test-Path "build-x86_64/qemu-boot.elf") -or -not (Test-Path "storage/disk.img")) {
    Write-Host "[RioOS] Build output missing; building before launch..."
    & (Join-Path $ProjectRoot "windows\build.ps1") --skip-tests
    if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
}
if (-not (Test-Path "build-x86_64/qemu-boot.elf")) { throw "Kernel image was not generated: build-x86_64/qemu-boot.elf" }
if (-not (Test-Path "storage/disk.img")) { throw "Disk image was not generated: storage/disk.img" }
Write-Host "RioOS x86-64 UI: QEMU standard VGA + PS/2 keyboard and mouse (arrows/WASD/mouse; Enter selects)."
& $Qemu -machine "pc,accel=$Accel" -cpu qemu64 -m $Mem `
    -kernel build-x86_64/qemu-boot.elf `
    -drive "if=ide,index=0,format=raw,file=storage/disk.img" `
    -netdev "user,id=n1,ipv4=on,ipv6=off" -device "virtio-net-pci,netdev=n1,disable-modern=on" `
    -vga std -display $Display -serial stdio -monitor none -no-reboot -no-shutdown
if ($LASTEXITCODE -ne 0) { throw "QEMU UI session failed ($LASTEXITCODE)" }
