#!/bin/bash

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
if [ -d /PortMaster ]; then
    controlfolder=/PortMaster
elif [ -d /opt/system/Tools/PortMaster ]; then
    controlfolder=/opt/system/Tools/PortMaster
elif [ -d /opt/tools/PortMaster ]; then
    controlfolder=/opt/tools/PortMaster
elif [ -d "$XDG_DATA_HOME/PortMaster" ]; then
    controlfolder="$XDG_DATA_HOME/PortMaster"
else
    controlfolder=/roms/ports/PortMaster
fi
source "$controlfolder/control.txt" || exit 1
export PORT_32BIT=Y
[ -f "$controlfolder/mod_${CFW_NAME}.txt" ] && source "$controlfolder/mod_${CFW_NAME}.txt"
get_controls
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

GAMEDIR="${GOF_ROOT:-/$directory/ports/gof2hd}"
RUN_DIR="$GAMEDIR/port/run-native"
APK="$GAMEDIR/original/base.apk"
[ -f "$APK" ] || APK="$GAMEDIR/base.apk"
GAME_LIBS="$GAMEDIR/gamedata/lib"
[ -d "$GAME_LIBS" ] || GAME_LIBS="$RUN_DIR"
DATA="$GAMEDIR/data"
OBB="${GOF_OBB:-$(find "$GAMEDIR/original" "$GAMEDIR/obb" -type f -name 'main.*.obb' -print -quit 2>/dev/null || true)}"

launch_game() {
    if [ ! -d "$GAMEDIR" ]; then
        pm_show_error "GOF2HD: game directory not found: $GAMEDIR"
        return 1
    fi
    exec > >(tee "${GOF_LOG:-$GAMEDIR/log.txt}") 2>&1
    if [ ! -f "$APK" ] || [ ! -f "$OBB" ] || [ ! -d "$DATA" ] || [ ! -f "$RUN_DIR/gof2hd" ]; then
        pm_show_error "GOF2HD: put base.apk and main.*.obb in original/, then run: python3 $GAMEDIR/port/tools/prepare-game.py $GAMEDIR"
        return 1
    fi
    for library in libgof2hdaa.so libfmodex.so libfmodevent.so; do
        if [ ! -f "$GAME_LIBS/$library" ]; then
            pm_show_error "GOF2HD: missing $library. Run: python3 $GAMEDIR/port/tools/prepare-game.py $GAMEDIR"
            return 1
        fi
    done
    cd "$RUN_DIR" || return 1
    chmod +x ./gof2hd || return 1

    # FMOD looks for banks beside the FEV; PortMaster handles exFAT without symlinks.
    for bank in "$DATA"/audio/*.fsb; do
        [ -f "$bank" ] || continue
        [ -e "$DATA/${bank##*/}" ] || bind_files "$DATA/${bank##*/}" "$bank" || return 1
    done
    if [ "$CFW_NAME" = ROCKNIX ]; then
        export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/var/run/0-runtime-dir}"
        export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-1}"
        export SDL_VIDEODRIVER=wayland
        export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-pulseaudio}"
    fi
    export GOF_SHOW_CURSOR="${GOF_SHOW_CURSOR:-1}"
    $GPTOKEYB "gof2hd" &
    pm_platform_helper "$RUN_DIR/gof2hd"
    # Confine the ARMv7 ABI shims to the game, including on 64-bit firmware.
    LD_LIBRARY_PATH="$GAME_LIBS:$RUN_DIR:/usr/lib32${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    LD_PRELOAD="$RUN_DIR/libm.so:$RUN_DIR/libfmod_filesystem.so:$RUN_DIR/cpuinfo_fake.so:$RUN_DIR/pthread_bionic.so" \
        ./gof2hd "$APK" "$OBB" "$DATA/"
}

launch_game
status=$?
pm_finish
exit "$status"
