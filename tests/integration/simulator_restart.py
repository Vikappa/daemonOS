"""Drive actual SDL mouse events through LVGL; inspect process restart and storage."""
import json
import pathlib
import sqlite3
import struct
import subprocess
import sys
import tempfile

binary = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="daemonos-ui-") as directory:
    root = pathlib.Path(directory)
    def run(name, epoch, commands, frames=240, backup=False):
        script = root / f"{name}.script"
        script.write_text(commands)
        report = root / f"{name}.json"
        screenshot = root / f"{name}.bmp"
        args = [binary, "--headless", "--data-dir", str(root / "device"),
                "--frames", str(frames), "--epoch", str(epoch), "--seed", "42",
                "--script", str(script), "--report", str(report), "--screenshot", str(screenshot)]
        if backup:
            args += ["--backup", str(root / "export.sqlite")]
        completed = subprocess.run(args, capture_output=True, text=True, timeout=20)
        assert completed.returncode == 0, completed.stdout + completed.stderr
        image = screenshot.read_bytes()
        assert image[:2] == b"BM"
        assert struct.unpack_from("<ii", image, 18) == (410, 502)
        offset = struct.unpack_from("<I", image, 10)[0]
        assert len(set(image[offset:])) > 32, "LVGL must render content, not a blank buffer"
        return json.loads(report.read_text())

    first = run("first", 1700000000, """click open_clock
click home
click open_settings
click brightness_down
click home
click open_creature
click feed
click pet
click rest
advance 3600
click move
click charge
click network
click think
wait 30
""", backup=True)
    assert first["app"] == 3 and first["mouse_clicks"] == 13, first
    assert first["hunger"] < 10 and first["energy"] > 90, first
    assert first["steps"] == 100 and first["charging"] and first["connected"], first
    assert first["brightness"] == 65 and first["mock_reply"], first
    assert first["rendered_frames"] > 5, first
    second = run("second", 1700046800, "click open_creature\n", frames=40)
    assert first["id"] == second["id"], (first, second)
    assert second["age_seconds"] > first["age_seconds"] and second["hunger"] > first["hunger"], second
    assert second["brightness"] == 65, second
    with sqlite3.connect(root / "export.sqlite") as db:
        assert db.execute("pragma integrity_check").fetchone()[0] == "ok"
        assert db.execute("select id from creature").fetchone()[0] == first["id"]
        assert db.execute("select count(*) from memories").fetchone()[0] >= 4
    fault = run("fault", 1700046801, """click open_creature
click storage
click feed
advance 600
click storage
click wake
""", frames=100)
    assert fault["id"] == first["id"]
    with sqlite3.connect(root / "device/sd/creature.sqlite") as db:
        assert db.execute("pragma user_version").fetchone()[0] == 2
        assert db.execute("select count(*) from memories").fetchone()[0] >= 5
    # A damaged card recovers only the independently stored minimal identity.
    (root / "device/sd/creature.sqlite").write_bytes(b"corrupt card")
    recovered = run("recovered", 1700046802, "click open_creature\n", frames=40)
    assert recovered["id"] == first["id"], recovered
print("PASS: SDL/LVGL mouse navigation, interactions, mock streaming, restart, export, storage faults and checkpoint recovery")
