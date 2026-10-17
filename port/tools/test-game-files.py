#!/usr/bin/env python3
"""Check package separation and safe preparation; real APK integration is optional."""
import hashlib
import os
from pathlib import Path
import runpy
import shutil
import struct
import tempfile
from zipfile import ZipFile

tools = Path(__file__).resolve().parent
prepare = runpy.run_path(str(tools / "prepare-game.py"))
pack = runpy.run_path(str(tools / "package-portmaster.py"))
prefix = "47947_GOF2CONTENT_ETC/assets/data/"


def write_obb(path, extra=()):
    with ZipFile(path, "w") as archive:
        archive.writestr(prefix + "audio/FMOD_GOF2.fev", b"fev")
        archive.writestr(prefix + "audio/bank.fsb", b"bank")
        archive.writestr(prefix + "shader/test.vs", b"shader")
        archive.writestr(prefix + "gof2_save_game", b"archive content")
        for name, content in extra:
            archive.writestr(prefix + name, content)


def expect_failure(root, message):
    try:
        prepare["prepare_game"](root)
    except ValueError as error:
        assert message in str(error), error
    else:
        raise AssertionError("Invalid input was accepted")


with tempfile.TemporaryDirectory(prefix="gof2hd-files-") as temporary:
    temporary = Path(temporary)
    build = temporary / "build"
    build.mkdir()
    for name in pack["PORT_BINARIES"]:
        (build / name).write_bytes(b"port file")
    for name in (*prepare["GAME_LIBRARIES"], "base.apk", "main.1.obb", "save.dat", "log.txt"):
        (build / name).write_bytes(b"must never be packaged")
    output = temporary / "gof2hd.zip"
    pack["package_port"](build, output)
    with ZipFile(output) as archive:
        files = set(archive.namelist())
        expected = {"gof2hd/port/run-native/" + name for name in pack["PORT_BINARIES"]}
        expected.update("gof2hd/port/tools/" + name for name in pack["TOOLS"])
        expected.update(("Galaxy on Fire 2 HD.sh", "gof2hd/README.md", "gof2hd/original/"))
        assert files == expected, files ^ expected
    previous = output.read_bytes()
    (build / "gof2hd").unlink()
    try:
        pack["package_port"](build, output)
    except ValueError:
        pass
    else:
        raise AssertionError("Missing port binary was accepted")
    assert output.read_bytes() == previous

    root = temporary / "game"
    expect_failure(root, "base.apk")
    original = root / "original"
    original.mkdir(parents=True)
    runtime = root / "port/run-native"
    runtime.mkdir(parents=True)
    for name in ("libc.so", "libm.so"):
        (runtime / name).touch()
    header = bytearray(52)
    header[:6] = b"\x7fELF\x01\x01"
    struct.pack_into("<HH", header, 16, 3, 40)
    apk = original / "base.apk"
    with ZipFile(apk, "w") as archive:
        for name in prepare["GAME_LIBRARIES"]:
            archive.writestr("lib/armeabi-v7a/" + name, header)
    obb = original / "main.1.obb"
    for unsafe in ("../escape", "/absolute", "shader/../../escape", "shader\\escape"):
        write_obb(obb, [(unsafe, b"bad")])
        expect_failure(root, "Unsafe OBB path")
        assert not (root / "data").exists()
        assert not (root / "gamedata").exists()
    write_obb(obb)
    outside = temporary / "outside"
    outside.mkdir()
    (root / "data").mkdir()
    (root / "data/shader").symlink_to(outside, target_is_directory=True)
    expect_failure(root, "escapes data/")
    assert list(outside.iterdir()) == []
    (root / "data/shader").unlink()
    (original / "main.2.obb").touch()
    expect_failure(root, "exactly one")
    (original / "main.2.obb").unlink()
    with ZipFile(apk, "w") as archive:
        for name in prepare["GAME_LIBRARIES"]:
            archive.writestr("lib/armeabi-v7a/" + name, b"not an ELF")
    expect_failure(root, "ARMv7 ELF32")

    real_apk = os.environ.get("GOF_TEST_APK")
    if real_apk:
        shutil.copyfile(real_apk, apk)
        real_build = Path(os.environ.get("GOF_OUT", tools.parent / "run-native"))
        for name in ("libc.so", "libm.so"):
            shutil.copyfile(real_build / name, runtime / name)
        save = root / "data/gof2_save_game"
        save.write_bytes(b"keep progress")
        original_hashes = {p: hashlib.sha256(p.read_bytes()).digest() for p in (apk, obb)}
        copy = shutil.copyfileobj

        def fail_copy(source, target):
            target.write(b"incomplete")
            raise OSError("simulated extraction failure")

        shutil.copyfileobj = fail_copy
        try:
            try:
                prepare["prepare_game"](root)
            except OSError as error:
                assert "simulated" in str(error)
            else:
                raise AssertionError("Extraction failure was ignored")
        finally:
            shutil.copyfileobj = copy
        assert not (root / "data/FMOD_GOF2.fev").exists()
        assert list((root / "data").glob(".prepare-*")) == []
        assert save.read_bytes() == b"keep progress"
        for _ in range(2):
            prepare["prepare_game"](root)
            assert save.read_bytes() == b"keep progress"
            assert (root / "data/FMOD_GOF2.fev").read_bytes() == b"fev"
            assert (root / "data/bank.fsb").read_bytes() == b"bank"
            assert (root / "data/shader/test.vs").read_bytes() == b"shader"
            assert not (root / "data/audio").exists()
            for p, digest in original_hashes.items():
                assert hashlib.sha256(p.read_bytes()).digest() == digest
            for name in prepare["GAME_LIBRARIES"]:
                binary = (root / "gamedata/lib" / name).read_bytes()
                assert struct.unpack_from("<I", binary, 36)[0] & 0x600 == 0x400
                assert not (runtime / name).exists()
        elf = runpy.run_path(str(tools / "patch-versions.py"))
        symbols = {s["name"]: s for s in elf["load_symbols_and_versions"](
            (root / "gamedata/lib/libgof2hdaa.so").read_bytes())}
        assert symbols["malloc"]["versym"] == 0
        for name in ("fopen", "cosf", "pthread_mutex_lock"):
            assert symbols[name]["versym"] > 1, name
        print("PASS: real APK ABI patches, resource layout, repeat preparation, original files and saves")
    else:
        print("SKIP: real APK integration (set GOF_TEST_APK and build the port first)")

print("PASS: package whitelist, input checks, archive paths, symlinks, failed package preserves previous ZIP")
