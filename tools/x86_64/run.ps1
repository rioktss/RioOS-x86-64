$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $ProjectRoot
$Qemu = if ($env:QEMU) { $env:QEMU } else { "qemu-system-x86_64" }
$Mem = if ($env:RIOOS_MEM) { $env:RIOOS_MEM } else { "128M" }
$Accel = if ($env:RIOOS_ACCEL) { $env:RIOOS_ACCEL } else { "tcg" }
if (-not (Get-Command $Qemu -ErrorAction SilentlyContinue)) {
    throw "qemu-system-x86_64 not found. Install QEMU and ensure it is on PATH."
}
if (-not (Test-Path "build-x86_64/qemu-boot.elf") -or -not (Test-Path "storage/disk.img")) {
    Write-Host "[RioOS] Build output missing; building before launch..."
    & (Join-Path $ProjectRoot "windows\build.ps1") --skip-tests
    if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
}
if (-not (Test-Path "build-x86_64/qemu-boot.elf")) { throw "Kernel image was not generated: build-x86_64/qemu-boot.elf" }
if (-not (Test-Path "storage/disk.img")) { throw "Disk image was not generated: storage/disk.img" }
& $Qemu -machine "pc,accel=$Accel" -cpu qemu64 -m $Mem `
    -kernel build-x86_64/qemu-boot.elf `
    -drive "if=ide,index=0,format=raw,file=storage/disk.img" `
    -netdev "user,id=n1,ipv4=on,ipv6=off" -device "virtio-net-pci,netdev=n1,disable-modern=on" `
    -vga none -display none -serial stdio -monitor none -no-reboot -no-shutdown
exit $LASTEXITCODE
