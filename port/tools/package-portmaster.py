#!/usr/bin/env python3
"""Package only port-owned files; user game data never enters the ZIP."""
import argparse
import os
from pathlib import Path
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile

PORT_BINARIES = (
    "gof2hd", "libc.so", "libm.so", "liblog.so", "libandroid.so", "libdl.so",
    "libGLESv2.so", "libOpenSLES.so", "libstdc++.so", "pthread_bionic.so",
    "cpuinfo_fake.so", "libfmod_filesystem.so",
)
TOOLS = ("prepare-game.py", "patch-versions.py", "start-game.sh", "start-rocknix.sh")


def package_port(build, output):
    tools = Path(__file__).resolve().parent
    files = [(Path(build) / name, "gof2hd/port/run-native/" + name) for name in PORT_BINARIES]
    files += [(tools / name, "gof2hd/port/tools/" + name) for name in TOOLS]
    files += [(tools / "Galaxy on Fire 2 HD.sh", "Galaxy on Fire 2 HD.sh"),
              (tools.parent / "PORTMASTER.md", "gof2hd/README.md")]
    for source, _ in files:
        if not source.is_file():
            raise ValueError("Missing port file: " + str(source))
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent, prefix=".package-") as temporary:
        temporary = Path(temporary) / "gof2hd.zip"
        with ZipFile(temporary, "w", compression=ZIP_DEFLATED) as archive:
            for source, name in files:
                archive.write(source, name)
            archive.writestr("gof2hd/original/", "")
        # Keep the previous package intact until writing the new ZIP succeeds.
        temporary.replace(output)
    print(output)


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=os.environ.get("GOF_OUT", root / "port/run-native"))
    parser.add_argument("output", type=Path, nargs="?", default=root / "dist/gof2hd.zip")
    args = parser.parse_args()
    try:
        package_port(args.build_dir, args.output)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Packaging failed: {error}\n")
