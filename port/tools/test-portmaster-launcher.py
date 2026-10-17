#!/usr/bin/env python3
"""Check the launcher with fake PortMaster helpers and a fake game (no ARM needed)."""
import os
from pathlib import Path
import subprocess
import tempfile

launcher = Path(__file__).with_name("Galaxy on Fire 2 HD.sh")
subprocess.run(["bash", "-n", str(launcher)], check=True)

with tempfile.TemporaryDirectory(prefix="gof2hd-pm-") as temporary:
    root = Path(temporary)
    pm = root / "xdg/PortMaster"
    pm.mkdir(parents=True)
    events = root / "events"
    (pm / "control.txt").write_text('''
directory="${TEST_ROOT#/}"
CFW_NAME="$TEST_CFW"
get_controls() { sdl_controllerconfig="test-controller"; }
pm_show_error() { echo "error: $1" >> "$TEST_EVENTS"; }
bind_files() { cp "$2" "$1"; }
pm_platform_helper() {
    [ -z "$LD_PRELOAD" ] || exit 90
    echo helper >> "$TEST_EVENTS"
}
pm_finish() {
    [ -z "$LD_PRELOAD" ] || exit 91
    echo finish >> "$TEST_EVENTS"
}
GPTOKEYB="$controlfolder/gptokeyb"
''')
    (pm / "mod_ROCKNIX.txt").write_text('''
[ "$PORT_32BIT" = Y ] || exit 92
export PIPEWIRE_MODULE_DIR=test-armhf-pipewire
''')
    gptokeyb = pm / "gptokeyb"
    gptokeyb.write_text('''#!/bin/bash
[ "$1" = gof2hd ] && [ -z "$LD_PRELOAD" ] || exit 93
echo hotkey >> "$TEST_EVENTS"
''')
    gptokeyb.chmod(0o755)
    game_root = root / "ports/gof2hd"
    run = game_root / "port/run-native"
    run.mkdir(parents=True)
    (game_root / "data/audio").mkdir(parents=True)
    (game_root / "data/audio/bank.fsb").write_bytes(b"audio")
    (game_root / "base.apk").touch()
    obb = game_root / "obb/package/main.123.test.obb"
    obb.parent.mkdir(parents=True)
    obb.touch()
    game = run / "gof2hd"
    game.write_text('''#!/bin/bash
printf '%s\\n' "$PWD" "$@" "$SDL_GAMECONTROLLERCONFIG" "$PORT_32BIT" \
    "$LD_LIBRARY_PATH" "$LD_PRELOAD" "$SDL_VIDEODRIVER" "$SDL_AUDIODRIVER" \
    "$PIPEWIRE_MODULE_DIR" > "$TEST_CAPTURE"
echo game >> "$TEST_EVENTS"
exit "${TEST_GAME_STATUS:-0}"
''')
    game.chmod(0o644)
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(("GOF_", "SDL_", "LD_", "PIPEWIRE_", "WAYLAND_", "XDG_"))}
    env.update(XDG_DATA_HOME=str(root / "xdg"), TEST_ROOT=str(root),
               TEST_EVENTS=str(events), TEST_CAPTURE=str(root / "capture"), TEST_CFW="test")

    def launch():
        events.write_text("")
        result = subprocess.run(["bash", str(launcher)], env=env,
                                capture_output=True, text=True, timeout=10)
        assert "finish" in events.read_text().splitlines(), result.stderr
        return result

    for cfw in ("test", "ROCKNIX"):
        env["TEST_CFW"] = cfw
        result = launch()
        assert result.returncode == 0, result.stderr
        captured = (root / "capture").read_text().splitlines()
        assert captured[:6] == [str(run), str(game_root / "base.apk"), str(obb),
                                str(game_root / "data") + "/", "test-controller", "Y"]
        assert captured[6].startswith(str(run) + ":/usr/lib32")
        assert captured[7] == ":".join(str(run / name) for name in (
            "libm.so", "libfmod_filesystem.so", "cpuinfo_fake.so", "pthread_bionic.so"))
        if cfw == "ROCKNIX":
            assert captured[8:] == ["wayland", "pulseaudio", "test-armhf-pipewire"]
        lines = events.read_text().splitlines()
        assert lines.index("game") < lines.index("finish"), lines
        assert "helper" in lines and "hotkey" in lines, lines
        assert (game_root / "data/bank.fsb").read_bytes() == b"audio"
        assert (game_root / "log.txt").exists()

    env.update(TEST_GAME_STATUS="7", SDL_VIDEODRIVER="custom", SDL_AUDIODRIVER="alsa")
    env["TEST_CFW"] = "test"
    assert launch().returncode == 7
    assert (root / "capture").read_text().splitlines()[8:10] == ["custom", "alsa"]
    obb.unlink()
    assert launch().returncode == 1
    assert "error:" in events.read_text() and "game\n" not in events.read_text()
    env["GOF_ROOT"] = str(root / "missing")
    assert launch().returncode == 1

print("PASS: launcher paths, controllers, ARMv7 environment, audio banks, ROCKNIX, cleanup and errors")
