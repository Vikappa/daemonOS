"""Habitat controls exercise the same view/controller and bounded runtime as firmware."""
import json
import pathlib
import sqlite3
import subprocess
import sys
import tempfile

binary = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="daemon-habitat-") as directory:
    root = pathlib.Path(directory)
    (root / "agent.md").write_text("A gentle watch familiar. Prefer quiet observations to unnecessary speech.\n")
    (root / "scenario").write_text("""click open_creature
click suspend
wait 20
advance 259200
wait 20
click wake
click pressure
click network
click think
click pressure
click think
wait 30
click audio_lock
click contract
""")
    report = root / "report.json"
    result = subprocess.run([binary, "--headless", "--frames", "240", "--epoch", "1700000000",
        "--seed", "42", "--data-dir", str(root / "device"), "--name", "Nyx", "--agent", str(root / "agent.md"),
        "--script", str(root / "scenario"), "--report", str(report)], capture_output=True, text=True, timeout=20)
    assert result.returncode == 0, result.stdout + result.stderr
    state = json.loads(report.read_text())
    assert state["name"] == "Nyx" and state["age_seconds"] >= 3 * 86400, state
    assert state["asleep_flushes"] == 0, state
    assert state["last_sleep_ms"] >= 3 * 86400000, state
    assert state["simulation_updates"] < 30, state
    assert state["mock_reply"] and state["cognitive_bytes"] == 0, state
    assert state["power"] == "SPEAKING" and state["wake_locks"] == 1, state
    assert state["memory_peak_external"] < 6 * 1024 * 1024 and state["queue_peak"] <= 64, state
    connection = sqlite3.connect(root / "device/sd/creature.sqlite")
    try:
        assert connection.execute("select name from creature").fetchone()[0] == "Nyx"
        assert connection.execute("pragma integrity_check").fetchone()[0] == "ok"
    finally:
        connection.close()
print("PASS: three-day suspension, zero asleep rendering, memory pressure, silent-capable v2 mock, resource telemetry and configured identity")
