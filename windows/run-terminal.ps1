# Run RioOS and show its shell in a QEMU window (framebuffer terminal).
#   run.ps1           -> RioOS in this PowerShell window (serial, -nographic)
#   run-terminal.ps1  -> RioOS in a QEMU window: shell only, mouse-wheel scrollback
$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $ProjectRoot

$Qemu = if ($env:QEMU) { $env:QEMU } else { "qemu-system-x86_64" }
$Kernel = if ($env:KERNEL) { $env:KERNEL } else { "build/qemu-boot.elf" }
$Disk = if ($env:DISK) { $env:DISK } else { "storage/disk.img" }
$Mem = if ($env:RIOOS_MEM) { $env:RIOOS_MEM } else { "128M" }
$Accel = if ($env:RIOOS_ACCEL) { $env:RIOOS_ACCEL } else { "tcg" }
$Display = if ($env:RIOOS_DISPLAY) { $env:RIOOS_DISPLAY } else { "sdl" }

if (-not (Get-Command $Qemu -ErrorAction SilentlyContinue)) {
    throw "qemu-system-x86_64 not found. Install QEMU with SDL or GTK display support."
}
if (-not (Test-Path $Kernel)) { throw "$Kernel not found. Run .\windows\build.ps1 first." }
if (-not (Test-Path $Disk)) { throw "$Disk not found. Run .\windows\build.ps1 first." }

$DisplayHelp = (& $Qemu -display help 2>&1 | Out-String)
if (-not $DisplayHelp.Contains($Display)) {
    $Fallback = $null
    foreach ($Candidate in @("sdl", "gtk")) {
        if ($DisplayHelp.Contains($Candidate)) { $Fallback = $Candidate; break }
    }
    if (-not $Fallback) { throw "This QEMU build has no SDL/GTK graphical display backend." }
    Write-Warning "Display '$Display' unavailable; using '$Fallback'."
    $Display = $Fallback
}

$DisplayArg = if ($Display -eq "gtk") { "gtk,grab-on-hover=on" } else { $Display }

Write-Host "RioOS terminal window: QEMU ($Display). Type in the window; scroll with the mouse wheel."

& $Qemu `
    -machine "pc,accel=$Accel" `
    -cpu qemu64 `
    -m $Mem `
    -vga std `
    -display $DisplayArg `
    -serial stdio `
    -monitor none `
    -no-reboot `
    -kernel $Kernel `
    -drive "if=ide,index=0,format=raw,file=$Disk" `
    -netdev "user,id=n1,ipv4=on,ipv6=off" `
    -device "virtio-net-pci,netdev=n1,disable-modern=on"
if ($LASTEXITCODE -ne 0) { throw "QEMU terminal window exited with code $LASTEXITCODE" }
