# GOF2HD Linux Port — installation and controls

English | [Русский](README.ru.md)

A port of **Galaxy on Fire 2 HD** (Android ARMv7) to ARM Linux devices,
with native hardware GPU rendering (Mali) and no QEMU during gameplay.
Install the ready-to-use package via PortMaster. To build it yourself, see [build.md](build.md#english).

## Requirements

- PortMaster;
- ARM Linux with support for 32-bit ARMv7 hardfp programs;
- compatible **glibc** and 32-bit **SDL2** and **EGL/GLES2** libraries from the firmware;
  tested configurations include Mali/fbdev and ROCKNIX with Mesa/Panfrost/Wayland;
- your own APK/OBB files from the ARMv7 Android version of the game;
- Python 3.9+ for automatic first-launch preparation (available on the tested ROCKNIX system).
  You can also prepare the installation on your PC and copy the entire folder to the device.

Docker, compilers and SDL2 development headers are not required on the handheld.

## 1. Install via PortMaster

1. Download `gof2hd.zip` from [Releases](https://github.com/KorolevSoftware/GOF2HD-linux-arm-port/releases)
   and extract it into the `ports/` directory on your game SD card.
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

The port emulates purchase restoration locally. On reaching the main menu for
the first time each launch, it unlocks Valkyrie, Kaamo Club, Supernova, VIP and
the full package through the engine's purchase interface. Google Play and an
internet connection are not needed. The in-game store offers free packs of
100,000, 300,000, 1,000,000, 3,000,000 and 10,000,000 credits. Select a pack to
receive its amount through the engine with autosave; packs can be obtained
repeatedly. Story requirements for missions still apply.

## 2. Controls

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
