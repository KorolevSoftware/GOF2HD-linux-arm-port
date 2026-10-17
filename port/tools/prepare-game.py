#!/usr/bin/env python3
"""Prepare user-supplied libraries and APK/OBB separately from the compiled port."""
import argparse
from pathlib import Path, PurePosixPath
import runpy
import shutil
import struct
import tempfile
from zipfile import BadZipFile, ZipFile

GAME_LIBRARIES = ("libgof2hdaa.so", "libfmodex.so", "libfmodevent.so")


def prepare_game(root, progress=False):
    root = Path(root).resolve()
    original = root / "original"
    apk = original / "base.apk"
    archives = sorted(original.glob("main.*.obb"))
    if not apk.is_file() or len(archives) != 1:
        raise ValueError("Place base.apk and exactly one main.*.obb in " + str(original))
    library_files = [original / name for name in GAME_LIBRARIES]
    use_extracted = any(path.exists() for path in library_files)
    if use_extracted and not all(path.is_file() for path in library_files):
        raise ValueError("Extract all three ARMv7 libraries from the same APK into " + str(original))
    runtime = root / "port/run-native"
    for name in ("libc.so", "libm.so"):
        if not (runtime / name).is_file():
            raise ValueError("Missing port library: " + str(runtime / name))

    with ZipFile(apk) as package, ZipFile(archives[0]) as resources:
        binaries = {}
        for name in GAME_LIBRARIES:
            binary = bytearray((original / name).read_bytes() if use_extracted
                               else package.read("lib/armeabi-v7a/" + name))
            if (len(binary) < 52 or binary[:6] != b"\x7fELF\x01\x01"
                    or struct.unpack_from("<HH", binary, 16) != (3, 40)):
                raise ValueError(name + " must be an ARMv7 ELF32 shared library")
            flags = struct.unpack_from("<I", binary, 36)[0]
            struct.pack_into("<I", binary, 36, (flags & ~0x200) | 0x400)
            binaries[name] = binary

        marker = "audio/FMOD_GOF2.fev"
        prefixes = [i.filename[:-len(marker)] for i in resources.infolist()
                    if i.filename.endswith("assets/data/" + marker)]
        if len(prefixes) != 1:
            raise ValueError("OBB must contain one assets/data/audio/FMOD_GOF2.fev")
        data = root / "data"
        members = []
        for info in resources.infolist():
            if not info.filename.startswith(prefixes[0]) or info.is_dir():
                continue
            relative = PurePosixPath(info.filename[len(prefixes[0]):])
            if relative.is_absolute() or ".." in relative.parts or "\\" in str(relative):
                raise ValueError("Unsafe OBB path: " + info.filename)
            # FMOD resolves banks beside the FEV; store them together without symlinks.
            if relative.parent == PurePosixPath("audio") and relative.suffix in (".fsb", ".fev"):
                relative = PurePosixPath(relative.name)
            destination = data / str(relative)
            if not destination.resolve().is_relative_to(data.resolve()):
                raise ValueError("OBB path escapes data/: " + info.filename)
            members.append((info, destination))

        library_dir = root / "gamedata/lib"
        library_dir.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=library_dir.parent, prefix=".prepare-") as temporary:
            temporary = Path(temporary)
            for name, binary in binaries.items():
                (temporary / name).write_bytes(binary)
            patch = runpy.run_path(str(Path(__file__).with_name("patch-versions.py")))["patch"]
            patch(temporary / "libgof2hdaa.so", runtime / "libc.so", runtime / "libm.so")
            extracted = 0
            last_percent = 0
            if progress:
                print("GOF_PROGRESS 0", flush=True)
            for completed, (info, destination) in enumerate(members, 1):
                if not destination.exists():
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    # Publish only complete files so interrupted preparation can be retried.
                    with tempfile.TemporaryDirectory(dir=destination.parent, prefix=".prepare-") as resource_temp:
                        resource_copy = Path(resource_temp) / "resource"
                        with resources.open(info) as source, resource_copy.open("wb") as target:
                            shutil.copyfileobj(source, target)
                        if not destination.exists():
                            resource_copy.replace(destination)
                            extracted += 1
                percent = completed * 100 // len(members)
                if progress and percent != last_percent and percent < 100:
                    print(f"GOF_PROGRESS {percent}", flush=True)
                    last_percent = percent
            for name in GAME_LIBRARIES:
                (temporary / name).replace(library_dir / name)
            if progress:
                print("GOF_PROGRESS 100", flush=True)
    print(f"Prepared {library_dir}; extracted {extracted} resources into {data}")
    print("Original files and existing data files were left unchanged.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, help="Game directory containing original/ and port/run-native/")
    parser.add_argument("--progress", action="store_true", help="Report preparation percentage for the PortMaster launcher")
    args = parser.parse_args()
    try:
        prepare_game(args.root, progress=args.progress)
    except (OSError, ValueError, KeyError, BadZipFile, struct.error) as error:
        parser.exit(1, f"Preparation failed: {error}\n")
