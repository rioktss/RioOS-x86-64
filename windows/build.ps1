$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $ProjectRoot

$VerboseBuild = $false
$Clean = $false
$RunAfterBuild = $false
foreach ($arg in $args) {
    switch ($arg) {
        "-v" { $VerboseBuild = $true }
        "--verbose" { $VerboseBuild = $true }
        "--clean" { $Clean = $true }
        "--run" { $RunAfterBuild = $true }
        "-h" { Write-Host "Usage: .\windows\build.ps1 [--clean] [--verbose] [--run]"; exit 0 }
        "--help" { Write-Host "Usage: .\windows\build.ps1 [--clean] [--verbose] [--run]"; exit 0 }
        default { throw "Unknown argument: $arg" }
    }
}

function Require-Command([string]$name) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        throw "Required command not found: $name"
    }
}
function Invoke-Tool {
    param([string]$Exe, [string[]]$ToolArgs)
    if ($VerboseBuild) { Write-Host "  -> $Exe $($ToolArgs -join ' ')" }
    & $Exe @ToolArgs
    if ($LASTEXITCODE -ne 0) { throw "Command failed ($LASTEXITCODE): $Exe" }
}

Write-Host ""
Write-Host "=================================="
Write-Host "       RioOS v1.13.0"
Write-Host "=================================="
Write-Host ""

$Clang = if ($env:CLANG) { $env:CLANG } else { "clang" }
$Cxx = if ($env:CXX) { $env:CXX } else { "clang++" }
$Ld = if ($env:LD) { $env:LD } else { "ld.lld" }
$ReadElf = if ($env:READELF) { $env:READELF } else { "llvm-readelf" }
$Python = if ($env:PYTHON) { $env:PYTHON } else { $null }
Require-Command $Clang
Require-Command $Cxx
Require-Command $Ld
if (-not $Python) {
    if (Get-Command python3 -ErrorAction SilentlyContinue) { $Python = "python3" }
    elseif (Get-Command python -ErrorAction SilentlyContinue) { $Python = "python" }
    elseif (Get-Command py -ErrorAction SilentlyContinue) { $Python = "py" }
    else { throw "Python 3 is required for tests/verify_x86_64.py" }
}

New-Item -ItemType Directory -Force build, storage, "storage/home" | Out-Null
$HostTools = if ($env:MYKERNEL_TOOLS_DIR) { $env:MYKERNEL_TOOLS_DIR } else { Join-Path $HOME ".mykernel-tools" }
New-Item -ItemType Directory -Force $HostTools | Out-Null

if ($Clean) {
    Get-ChildItem build -File -Filter *.o -ErrorAction SilentlyContinue | Remove-Item -Force
    foreach ($artifact in @("kernel.elf","kernel.bin","kernel.entry","qemu-boot.elf","user_init.elf","nano.elf","calc.elf")) {
        Remove-Item (Join-Path "build" $artifact) -Force -ErrorAction SilentlyContinue
    }
    foreach ($name in @("fs_host_test","net_host_test","mem_host_test","syscall_host_test","diag_host_test","vfs_host_test","sched_host_test","env_host_test","fd_host_test","process_host_test","scheduler_process_host_test","smp_host_test","console_host_test")) {
        Remove-Item (Join-Path $HostTools $name), (Join-Path $HostTools "$name.exe") -Force -ErrorAction SilentlyContinue
    }
}

function Source-Of([string]$name) {
    foreach ($dir in @("kernel","drivers","memory","process","fs","net")) {
        $p = Join-Path $dir "$name.cpp"
        if (Test-Path $p) { return $p }
    }
    throw "Source for $name not found"
}

$CxxFlags = @(
    "-Iinclude", "--target=x86_64-unknown-none-elf", "-DMYKERNEL_X86_64=1",
    "-ffreestanding", "-fno-exceptions", "-fno-rtti", "-fno-stack-protector", "-fno-builtin",
    "-fno-pic", "-fno-pie", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
    "-mno-red-zone", "-mno-sse", "-mno-sse2", "-mno-mmx", "-mno-80387", "-mcmodel=small",
    "-nostdinc++", "-std=c++17", "-O1",
    "-Wall", "-Wextra", "-Werror"
)
$AsmFlags = @("-Iinclude", "--target=x86_64-unknown-none-elf", "-ffreestanding", "-fno-pic", "-fno-pie", "-mno-red-zone")
$Modules = @(
    "uart","memory","string","fs","vfs","process","sched_policy","scheduler","timer","interrupt","keyboard",
    "commands","version","shell","history","env","rtc","exceptions","syscall","user","mmu",
    "storage","thread","fd","fb","console","console_font","elf","virtio_net","netstack","page_alloc","fault","diag","security","smp","gdt","runtime",
    "kernel"
)

Write-Host "[1/9] Bundling userspace (init + nano + calc)"
Invoke-Tool $Clang ($AsmFlags + @("-c","userspace/user_init.S","-o","build/user_init.o"))
Invoke-Tool $Ld @("-T","userspace/user_init.ld","-nostdlib","-z","max-page-size=0x1000","build/user_init.o","-o","build/user_init.elf")
$UserFlags = @("-Iinclude","--target=x86_64-unknown-none-elf","-DMYKERNEL_X86_64=1","-ffreestanding","-fno-exceptions","-fno-rtti","-fno-stack-protector","-fno-builtin","-fno-pic","-fno-pie","-fno-unwind-tables","-fno-asynchronous-unwind-tables","-mno-red-zone","-mno-sse","-mno-sse2","-mno-mmx","-mno-80387","-mcmodel=small","-nostdinc++","-nostdlib","-std=c++17","-O1","-Wall","-Wextra","-Werror")
foreach ($prog in @("nano","calc")) {
    Invoke-Tool $Cxx ($UserFlags + @("-c","userspace/$prog.cpp","-o","build/$prog.o"))
    Invoke-Tool $Ld @("-T","userspace/user_init.ld","-nostdlib","-z","max-page-size=0x1000","build/$prog.o","-o","build/$prog.elf")
}

Write-Host "[2/9] Building host tools"
Invoke-Tool $Cxx @("-std=c++17","-O2","-Wall","-Wextra","-pedantic","fs/mkfs_myfs.cpp","-o",(Join-Path $HostTools "mkfs_myfs.exe"))

Write-Host "[3/9] Preparing disk image"
$DiskPath = Join-Path (Get-Location) "storage/disk.img"
if (-not (Test-Path $DiskPath)) {
    $fs = [System.IO.File]::Create($DiskPath)
    $fs.SetLength(32768L * 512L)
    $fs.Dispose()
}
$AppElfs = @("build/user_init.elf")
if (Test-Path build/nano.elf) { $AppElfs += "build/nano.elf" }
if (Test-Path build/calc.elf) { $AppElfs += "build/calc.elf" }
Invoke-Tool (Join-Path $HostTools "mkfs_myfs.exe") (@("storage/disk.img") + $AppElfs)

Write-Host "[4/9] Compiling boot and context-switch assembly"
Invoke-Tool $Clang ($AsmFlags + @("-c","boot/boot.S","-o","build/boot.o"))
Invoke-Tool $Clang ($AsmFlags + @("-c","boot/exceptions.S","-o","build/exception_vectors.o"))
Invoke-Tool $Clang ($AsmFlags + @("-c","process/user_entry.S","-o","build/user_entry.o"))
Invoke-Tool $Clang ($AsmFlags + @("-c","process/ctx_switch.S","-o","build/ctx_switch.o"))

Write-Host "[5/9] Compiling kernel ($($Modules.Count) modules)"
foreach ($f in $Modules) {
    if ($VerboseBuild) { Write-Host "  -> $f.cpp" } else { Write-Host "." -NoNewline }
    $src = Source-Of $f
    Invoke-Tool $Cxx ($CxxFlags + @("-c",$src,"-o","build/$f.o"))
}
if (-not $VerboseBuild) { Write-Host " done" }

Write-Host "[6/9] Linking"
$linkObjects = @("build/boot.o","build/exception_vectors.o","build/user_entry.o","build/ctx_switch.o")
foreach ($f in $Modules) { $linkObjects += "build/$f.o" }
Invoke-Tool $Ld (@("-T","boot/linker.ld","-nostdlib","-z","max-page-size=0x1000") + $linkObjects + @("-o","build/kernel.elf"))
if ($Python -eq "py") { & py -3 tools/make_boot_image.py build/kernel.elf build/kernel.bin build/kernel.entry } else { & $Python tools/make_boot_image.py build/kernel.elf build/kernel.bin build/kernel.entry }
if ($LASTEXITCODE -ne 0) { throw "make_boot_image failed" }
$KernelEntry = (Get-Content build/kernel.entry -Raw).Trim()
Invoke-Tool $Clang @("--target=i386-unknown-none-elf","-m32","-ffreestanding","-fno-pic","-fno-pie","-DKERNEL_ENTRY_ADDRESS=$KernelEntry","-c","boot/qemu_multiboot32.S","-o","build/qemu_multiboot32.o")
Invoke-Tool $Ld @("-m","elf_i386","-T","boot/qemu_multiboot32.ld","-nostdlib","build/qemu_multiboot32.o","-o","build/qemu-boot.elf")

Write-Host "[7/9] Verifying ELF"
if (Get-Command $ReadElf -ErrorAction SilentlyContinue) {
    & $ReadElf -h build/kernel.elf | Select-String "Class:|Machine:|Entry point"
}

function Invoke-HostTest([string]$name, [string[]]$sources, [bool]$strict = $false) {
    $exe = Join-Path $HostTools "$name.exe"
    $flags = @("-std=c++17","-O2","-Wall","-Wextra","-pedantic","-iquote","include","-DHOST_TEST")
    if ($strict) { $flags += "-Werror" }
    Invoke-Tool $Cxx ($flags + $sources + @("-o",$exe))
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "$name failed" }
}
Write-Host "[8/9] Running 13 host tests (including the framebuffer terminal)"
Invoke-HostTest "fs_host_test" @("tests/fs_host_test.cpp","fs/fs.cpp","kernel/string.cpp","kernel/rtc.cpp")
Invoke-HostTest "net_host_test" @("tests/net_host_test.cpp","net/netstack.cpp") $true
Invoke-HostTest "mem_host_test" @("tests/mem_host_test.cpp","memory/page_alloc.cpp","kernel/fault.cpp")
Invoke-HostTest "syscall_host_test" @("tests/syscall_host_test.cpp","kernel/syscall.cpp","kernel/fd.cpp")
Invoke-HostTest "diag_host_test" @("tests/diag_host_test.cpp","kernel/diag.cpp")
Invoke-HostTest "vfs_host_test" @("tests/vfs_host_test.cpp","fs/vfs.cpp","fs/fs.cpp","kernel/string.cpp","kernel/rtc.cpp") $true
Invoke-HostTest "sched_host_test" @("tests/sched_host_test.cpp","process/sched_policy.cpp") $true
Invoke-HostTest "env_host_test" @("tests/env_host_test.cpp","kernel/env.cpp","kernel/string.cpp") $true
Invoke-HostTest "fd_host_test" @("tests/fd_host_test.cpp","kernel/fd.cpp","fs/fs.cpp","kernel/string.cpp","kernel/rtc.cpp") $true
Invoke-HostTest "process_host_test" @("tests/process_host_test.cpp","process/process.cpp","kernel/string.cpp") $true
Invoke-HostTest "scheduler_process_host_test" @("tests/scheduler_process_host_test.cpp","process/scheduler.cpp","process/process.cpp","kernel/string.cpp") $true
Invoke-HostTest "smp_host_test" @("tests/smp_host_test.cpp","kernel/smp.cpp") $true
Invoke-HostTest "console_host_test" @("tests/console_host_test.cpp","drivers/console.cpp","drivers/console_font.cpp","drivers/fb.cpp") $true

Write-Host "[9/9] Release verification"
if ($Python -eq "py") { & py -3 tests/verify_x86_64.py } else { & $Python tests/verify_x86_64.py }
if ($LASTEXITCODE -ne 0) { throw "Release verification failed" }

Write-Host ""
Write-Host "=================================="
Write-Host "       BUILD SUCCESS (v1.13.0)"
Write-Host "=================================="
Write-Host "  Kernel: build/qemu-boot.elf (x86-64)"
Write-Host "  Disk  : storage/disk.img"
Write-Host "  Tests : 13 host tests passed"
Write-Host "  Window   : windows\run-terminal.ps1 (QEMU window, wheel scrollback)"
Write-Host "  Terminal : windows\run.ps1 (plain serial console)"
Write-Host "  Timer IRQ: 100 Hz system tick, active at boot and during ring 3 programs"
Write-Host ""
if ($RunAfterBuild) { & (Join-Path $PSScriptRoot "run.ps1") }
