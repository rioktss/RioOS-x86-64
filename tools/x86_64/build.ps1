# RioOS x86-64 build for Windows PowerShell (clang + lld + python3 required).
$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $ProjectRoot

$Clean = $false; $SkipTests = $false; $Run = $false
foreach ($arg in $args) {
    switch ($arg) {
        "--clean" { $Clean = $true }
        "--skip-tests" { $SkipTests = $true }
        "--run" { $Run = $true }
        "-v" { }
        "--verbose" { }
        { $_ -in "-h", "--help" } { Write-Host "Usage: .\windows\build.ps1 [--clean] [--skip-tests] [--run]"; exit 0 }
        default { throw "Unknown argument: $arg" }
    }
}
function Run([string]$exe, [string[]]$a) {
    & $exe @a
    if ($LASTEXITCODE -ne 0) { throw "Command failed ($LASTEXITCODE): $exe $($a -join ' ')" }
}
$Clang = if ($env:CLANG) { $env:CLANG } else { "clang" }
$Cxx = if ($env:CXX) { $env:CXX } else { "clang++" }
$Ld = if ($env:LD) { $env:LD } else { "ld.lld" }
$Py = if ($env:PYTHON) { $env:PYTHON } else { "python" }
foreach ($t in @($Clang, $Cxx, $Ld, $Py)) {
    if (-not (Get-Command $t -ErrorAction SilentlyContinue)) { throw "Required command not found: $t" }
}
$Out = "build-x86_64"
$Tools = if ($env:MYKERNEL_TOOLS_DIR) { $env:MYKERNEL_TOOLS_DIR } else { Join-Path $HOME ".mykernel-tools" }
if ($Clean -and (Test-Path $Out)) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Force $Out, "$Out/user", $Tools, "storage", "build" | Out-Null

$Cflags = @("--target=x86_64-unknown-none-elf", "-Iinclude", "-DMYKERNEL_X86_64=1",
    "-ffreestanding", "-fno-exceptions", "-fno-rtti", "-fno-stack-protector", "-fno-builtin",
    "-fno-pic", "-fno-pie", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
    "-mno-red-zone", "-mno-sse", "-mno-sse2", "-mno-mmx", "-mno-80387", "-mcmodel=small",
    "-nostdinc++", "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror")
$Asm = @("--target=x86_64-unknown-none-elf", "-Iinclude", "-ffreestanding", "-fno-pic", "-fno-pie", "-mno-red-zone")

Write-Host "MyKernel x86-64 build..."
$mkfs = Join-Path $Tools "mkfs_myfs.exe"
Run $Cxx @("-std=c++17", "-O2", "fs/mkfs_myfs.cpp", "-o", $mkfs)

# Userspace (ring 3)
Run $Clang @("--target=x86_64-unknown-none-elf", "-ffreestanding", "-fno-pic", "-fno-pie", "-c", "userspace/user_init.S", "-o", "$Out/user/user_init.o")
Run $Ld @("-T", "userspace/user_init.ld", "-nostdlib", "-z", "max-page-size=0x1000", "$Out/user/user_init.o", "-o", "$Out/user/user_init.elf")
foreach ($prog in @("nano", "calc")) {
    Run $Cxx ($Cflags + @("-nostdlib", "-c", "userspace/$prog.cpp", "-o", "$Out/user/$prog.o"))
    Run $Ld @("-T", "userspace/user_init.ld", "-nostdlib", "-z", "max-page-size=0x1000", "$Out/user/$prog.o", "-o", "$Out/user/$prog.elf")
}

# Disk image
if (-not (Test-Path "storage/disk.img")) {
    $fs = [System.IO.File]::Create("storage/disk.img"); $fs.SetLength(16MB); $fs.Close()
}
$seed = @("$Out/user/user_init.elf", "$Out/user/nano.elf", "$Out/user/calc.elf")
$seed += @(Get-ChildItem -Path "apps" -Filter "*.rpk" -File | ForEach-Object { "apps/$($_.Name)" })
$seed += @(Get-ChildItem -Path "assets/wallpapers" -Filter "*.bmp" -File -ErrorAction SilentlyContinue | ForEach-Object { "assets/wallpapers/$($_.Name)" })
$seed += @(Get-ChildItem -Path "assets/icons" -Filter "*.bmp" -File -ErrorAction SilentlyContinue | ForEach-Object { "assets/icons/$($_.Name)" })
Run $mkfs (@("storage/disk.img") + $seed)

# Kernel
$asmFiles = @("boot", "ctx_switch", "exceptions", "user_entry")
$objs = @()
foreach ($a in $asmFiles) {
    $o = "$Out/arch_$a.S.o"; Run $Clang ($Asm + @("-c", "arch/x86_64/$a.S", "-o", $o)); $objs += $o
}
$srcs = @(
    "kernel/commands", "kernel/diag", "kernel/env", "kernel/fault", "kernel/fd", "kernel/history",
    "kernel/kernel", "kernel/security", "kernel/shell", "kernel/string", "kernel/syscall", "kernel/version",
    "memory/memory", "memory/page_alloc", "fs/fs", "fs/vfs", "net/netstack",
    "process/elf", "process/process", "process/sched_policy", "process/scheduler", "process/thread", "process/user",
    "drivers/font", "drivers/ui_font_data",
    "ui/apps", "ui/boot", "ui/calculator", "ui/clock", "ui/file_manager", "ui/home_screen", "ui/image",
    "ui/launcher", "ui/lock_screen", "ui/settings", "ui/terminal", "ui/ui", "ui/wallpaper",
    "arch/x86_64/exceptions", "arch/x86_64/gdt", "arch/x86_64/fb", "arch/x86_64/interrupt", "arch/x86_64/mmu",
    "arch/x86_64/net", "arch/x86_64/platform", "arch/x86_64/runtime", "arch/x86_64/smp", "arch/x86_64/timer", "arch/x86_64/uart")
foreach ($s in $srcs) {
    $o = "$Out/" + ($s -replace "/", "_") + ".o"
    Run $Cxx ($Cflags + @("-c", "$s.cpp", "-o", $o)); $objs += $o
}
Run $Ld (@("-T", "arch/x86_64/linker.ld", "-nostdlib", "-z", "max-page-size=0x1000") + $objs + @("-o", "$Out/kernel.elf"))
Run $Py @("tests/make_boot_image.py", "$Out/kernel.elf", "$Out/kernel.bin", "$Out/qemu-boot.elf", "--clang", $Clang, "--ld", $Ld)

if (-not $SkipTests) {
    Write-Host "Running host tests..."
    Run $Py @("tests/run_host_tests.py", "--cxx", $Cxx, "--tools", $Tools)
    Run $Py @("tests/verify_x86_64.py")
}
Write-Host "Build OK: $Out/kernel.elf and $Out/qemu-boot.elf"
if ($Run) {
    & (Join-Path $ProjectRoot "windows\run.ps1")
    if ($LASTEXITCODE -ne 0) { throw "QEMU failed ($LASTEXITCODE)" }
}
