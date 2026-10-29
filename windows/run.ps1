$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $ProjectRoot

$Qemu = if ($env:QEMU) { $env:QEMU } else { "qemu-system-x86_64" }
$Kernel = if ($env:KERNEL) { $env:KERNEL } else { "build/qemu-boot.elf" }
$Disk = if ($env:DISK) { $env:DISK } else { "storage/disk.img" }
$Mem = if ($env:RIOOS_MEM) { $env:RIOOS_MEM } else { "128M" }
$Accel = if ($env:RIOOS_ACCEL) { $env:RIOOS_ACCEL } else { "tcg" }

if (-not (Get-Command $Qemu -ErrorAction SilentlyContinue)) {
    throw "qemu-system-x86_64 not found"
}
if (-not (Test-Path $Kernel)) {
    throw "$Kernel not found. Run .\windows\build.ps1 first."
}
if (-not (Test-Path $Disk)) {
    throw "$Disk not found. Run .\windows\build.ps1 first."
}

& $Qemu `
    "-machine" "pc,accel=$Accel" `
    "-cpu" "qemu64" `
    "-m" $Mem `
    "-nographic" `
    "-vga" "none" `
    "-monitor" "none" `
    "-no-reboot" `
    "-kernel" $Kernel `
    "-drive" "if=ide,index=0,format=raw,file=$Disk" `
    "-netdev" "user,id=n1,ipv4=on,ipv6=off" `
    "-device" "virtio-net-pci,netdev=n1,disable-modern=on"

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
