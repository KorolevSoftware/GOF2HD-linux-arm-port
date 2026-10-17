# GOF2HD Linux Port — building and installing via PortMaster

English | [Русский](README.ru.md)

A port of **Galaxy on Fire 2 HD** (Android ARMv7) to ARM Linux devices,
with native hardware GPU rendering (Mali) and no QEMU during gameplay.
Build it in Docker on your PC, then copy the resulting package to your handheld.

- Current architecture and component responsibilities: [ARCHITECTURE.md](ARCHITECTURE.md) (Russian)
- Technical notes and ABI/FMOD debugging history: [GOF2HD_PORT_NOTES.md](GOF2HD_PORT_NOTES.md) (Russian)

## Requirements

To build on your PC:

- Git and a running Docker installation;
- support for `linux/arm/v7` containers: x86-64 and ARM64 hosts need ARMv7
  emulation through QEMU/binfmt. Docker Desktop includes it by default;
  with Docker Engine on Linux, configure it if missing
  ([Docker documentation](https://docs.docker.com/build/building/multi-platform/#qemu));
- original game files are not required for building;
- access to container image and Debian package repositories when creating the image for the first time.

To run on your handheld:

- PortMaster;
- ARM Linux with support for 32-bit ARMv7 hardfp programs;
- compatible **glibc** and 32-bit **SDL2** and **EGL/GLES2** libraries from the firmware;
  tested configurations include Mali/fbdev and ROCKNIX with Mesa/Panfrost/Wayland;
- your own APK/OBB files from the ARMv7 Android version of the game;
- Python 3.9+ for automatic first-launch preparation (available on the tested ROCKNIX system).
  You can also prepare the installation on your PC and copy the entire folder to the device.

Docker, compilers and SDL2 development headers are not required on the handheld.

## 1. Build on your PC with Docker

Run all commands from the project root on your PC:

```sh
bash port/tools/build-docker.sh
```

The script creates or updates the `gof2hd-build:armhf` image and builds the port for ARMv7.
If you get an `exec format error`, check ARMv7/binfmt support in Docker.

### Build output and rebuilding

The output is saved to `port/run-native/` on your PC: the `gof2hd` executable
and the port libraries.

The game engine and FMOD libraries from the APK are not part of the build output.
Old copies left in `run-native/` by earlier builds are not removed automatically;
the packaging script includes only an explicit list of port files.

To rebuild, run the same command again: only changed source files and their
dependencies are rebuilt. Game resources, saves and logs are left untouched.
For a clean rebuild, delete only `port/run-native/build/`.

The default is two parallel build jobs. To change this:

```sh
GOF_JOBS=6 bash port/tools/build-docker.sh
```

See [build verification](ARCHITECTURE.md#проверка-сборки) in `ARCHITECTURE.md` (Russian)
for the verification command and details.

### Create a PortMaster package

After building, run this from the project root on your PC:

```sh
docker run --rm --platform linux/arm/v7 \
  --user "$(id -u):$(id -g)" \
  --mount "type=bind,source=$(pwd),target=/work" \
  gof2hd-build:armhf python3 port/tools/package-portmaster.py
```

The result is `dist/gof2hd.zip`, ready to copy to your SD card.
The package contains only port files; original APK/OBB files and saves are not included.

## 2. Install via PortMaster

1. Extract `dist/gof2hd.zip` into the `ports/` directory on your game SD card.
2. Place your own `base.apk` and one `main.*.obb` in `gof2hd/original/`.
   Use the OBB file directly, without wrapping it in a RAR or ZIP archive.
3. Select **Galaxy on Fire 2 HD** in the Ports menu.
4. On first launch, wait for automatic preparation to finish. A progress bar is shown.
   This may take a few minutes. The game starts automatically afterwards;
   subsequent launches open the game directly.

Installation layout after the first launch:

```text
ports/
├── Galaxy on Fire 2 HD.sh
└── gof2hd/
    ├── original/               # original base.apk and main.*.obb
    ├── gamedata/lib/           # prepared game libraries
    ├── data/                  # resources and saves
    └── port/run-native/        # port files
```

Keep the APK and OBB files in place: the engine needs them on every launch.
The launch log is `gof2hd/log.txt`. Close the game before updating;
preserve `original/`, `gamedata/` and `data/` to keep your original files and saves.

See [port/PORTMASTER.md](port/PORTMASTER.md) (Russian) for detailed game file requirements.

## 3. Controls

The gamepad replaces the game's touch controls. PortMaster supplies the button mapping;
tested with the H700 Gamepad on ROCKNIX.

The D-pad mirrors the left stick: it moves the cursor in cursor mode
and steers the ship in gyroscope mode.

Button names follow the SDL/PortMaster mapping:

| Button | Action |
|---|---|
| Left stick / D-pad | Move the cursor or steer the ship in gyroscope mode |
| **A** | Reserved; action currently disabled |
| **B** | Autopilot |
| **Y** | Fast-forward time |
| **L2** | Fire secondary weapon |
| **L1** | Swipe left (dodge) |
| **R1** | Swipe right (dodge) |
| **R2** | Fire in gyroscope mode; touch at the cursor position in cursor mode |
| **Start** | Switch between cursor and gyroscope modes |
| **X** | Reserved |

On the tested H700/ROCKNIX system, press **M + Start** to exit the game.
