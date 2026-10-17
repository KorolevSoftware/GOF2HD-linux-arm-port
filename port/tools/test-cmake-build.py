#!/usr/bin/env python3
"""Run in the ARMv7 build environment with the same variables as build-native.sh."""
import json
import os
from pathlib import Path
import runpy
import struct
import subprocess
import tempfile

tools = Path(__file__).resolve().parent
out = Path(os.environ.get("GOF_OUT", tools.parent / "run-native")).resolve()
command = ["bash", str(tools / "build-native.sh")]
with tempfile.TemporaryDirectory(prefix="gof2hd-gamepad-") as temporary:
    test = str(Path(temporary) / "test-gamepad")
    subprocess.run([os.environ.get("CC", "arm-linux-gnueabihf-gcc"),
                    "-ffunction-sections", "-fdata-sections",
                    str(tools / "test-gamepad.c"), "-Wl,--gc-sections",
                    "-lSDL2", "-o", test], check=True)
    subprocess.run([test], check=True)
subprocess.run(command, check=True)

compiled = ["gof2hd", "libc.so", "libm.so", "liblog.so", "libandroid.so",
            "libdl.so", "libGLESv2.so", "libOpenSLES.so", "libstdc++.so",
            "pthread_bionic.so", "cpuinfo_fake.so", "libfmod_filesystem.so"]
for name in compiled + ["libgof2hdaa.so", "libfmodex.so", "libfmodevent.so"]:
    data = (out / name).read_bytes()
    assert data[:6] == b"\x7fELF\x01\x01", (name, "expected ELF32 little-endian")
    assert struct.unpack_from("<H", data, 18)[0] == 40, (name, "expected ARM")
    assert struct.unpack_from("<I", data, 36)[0] & 0x600 == 0x400, (name, "expected hardfp")

elf = runpy.run_path(str(tools / "patch-versions.py"))
symbols = {s["name"]: s for s in elf["load_symbols_and_versions"](
    (out / "libgof2hdaa.so").read_bytes())}
assert symbols["malloc"]["versym"] == 0, "malloc must bind directly to glibc"
for name in ["fopen", "cosf", "pthread_mutex_lock"]:
    assert symbols[name]["versym"] > 1, (name, "must retain the shim version")

commands = json.loads((out / "build/compile_commands.json").read_text())
assert all("dobby" not in (c["file"] + c["command"]).lower() for c in commands), "Dobby must not be compiled"
assert not any(c["file"].endswith("/engine_hook_test.c") for c in commands), "the removed test hook must not be compiled"
host_symbols = subprocess.check_output(["nm", str(out / "gof2hd")], text=True)
assert "Dobby" not in host_symbols and "engine_install_test_hook" not in host_symbols, "removed hooks must not be linked"
shim = next(c for c in commands if c["file"].endswith("/libstdcxx_stub.c"))
assert "g++" in shim["command"], "the C++ shim must use g++"

tracked = [out / name for name in compiled] + list((out / "build").rglob("*.o"))
before = {p: p.stat().st_mtime_ns for p in tracked}
with tempfile.NamedTemporaryFile(dir=out) as sentinel:
    sentinel.write(b"keep runtime files")
    sentinel.flush()
    subprocess.run(command, check=True)
    assert Path(sentinel.name).read_bytes() == b"keep runtime files"
assert before == {p: p.stat().st_mtime_ns for p in tracked}, "unchanged code was rebuilt"
print("PASS: ARM hardfp outputs, engine versions, no hook dependency, C++ flags, incremental build, runtime files")
