#!/usr/bin/env python3
"""Build and run all native host unit tests (used by build.ps1; build.sh has the same list)."""
import argparse, os, subprocess, sys
from pathlib import Path

ap = argparse.ArgumentParser(); ap.add_argument("--cxx", default="clang++"); ap.add_argument("--tools", required=True)
a = ap.parse_args()
root = Path(__file__).resolve().parents[1]; os.chdir(root); os.makedirs("build", exist_ok=True)
flags = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic", "-iquote", "include", "-DHOST_TEST"]
fb = ["arch/x86_64/fb.cpp", "drivers/font.cpp", "drivers/ui_font_data.cpp"]
S, R = "kernel/string.cpp", "kernel/rtc.cpp"
tests = {
    "fs_host_test": ["fs/fs.cpp", S, R], "net_host_test": ["net/netstack.cpp"],
    "mem_host_test": ["memory/page_alloc.cpp", "kernel/fault.cpp"], "syscall_host_test": ["kernel/syscall.cpp", "kernel/fd.cpp"],
    "diag_host_test": ["kernel/diag.cpp"], "vfs_host_test": ["fs/vfs.cpp", "fs/fs.cpp", S, R],
    "sched_host_test": ["process/sched_policy.cpp"], "env_host_test": ["kernel/env.cpp", S],
    "fd_host_test": ["kernel/fd.cpp", "fs/fs.cpp", S, R], "process_host_test": ["process/process.cpp", S],
    "scheduler_process_host_test": ["process/scheduler.cpp", "process/process.cpp", S], "smp_host_test": ["kernel/smp.cpp"],
    "fb_host_test": fb, "ui_host_test": fb + ["ui/boot.cpp", "ui/lock_screen.cpp", "ui/home_screen.cpp"],
    "ui_image_host_test": fb + ["ui/image.cpp", "ui/settings.cpp", S],
}
exe = ".exe" if os.name == "nt" else ""
for name, srcs in tests.items():
    out = os.path.join(a.tools, name + exe)
    subprocess.check_call([a.cxx, *flags, f"tests/{name}.cpp", *srcs, "-o", out])
    subprocess.check_call([out], stdout=subprocess.DEVNULL)
    print("PASS", name)
