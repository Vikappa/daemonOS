"""Write clean ELF/map-derived evidence without interpreting host RSS as device memory."""
import json
import pathlib
import re
import subprocess

root = pathlib.Path(__file__).resolve().parents[1]
build = root / "build/firmware"
python = root / ".cache/idf-tools/python_env/idf5.5_py3.12_env/bin/python"
map_file = build / "daemonos_firmware.map"
mapping = map_file.read_text()
summary = json.loads(subprocess.check_output([str(python), "-m", "esp_idf_size", "--format", "json", str(map_file)], text=True))
components = json.loads(subprocess.check_output([str(python), "-m", "esp_idf_size", "--format", "json", "--archives", str(map_file)], text=True))
sections = {}
for name in [".dram0.data", ".dram0.bss", ".iram0.vectors", ".iram0.text", ".flash.text", ".flash.rodata", ".ext_ram.bss"]:
    match = re.search(r"^" + re.escape(name) + r"\s+0x[0-9a-f]+\s+(0x[0-9a-f]+)", mapping, re.MULTILINE)
    if match:
        sections[name] = int(match.group(1), 16)
summary["map_sections"] = sections
summary["application_binary_bytes"] = (build / "daemonos_firmware.bin").stat().st_size
summary["ota_slot_bytes"] = 4 * 1024 * 1024
summary["hardware_validated"] = False
assert sections.get(".ext_ram.bss", 0) >= 512 * 1024, "LVGL arena must reside in PSRAM"
assert summary["application_binary_bytes"] < summary["ota_slot_bytes"]
assert "libdaemon_presentation.a(watch_view.cpp.obj)" in mapping and "libdaemon_core.a(runtime.cpp.obj)" in mapping
(build / "size-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
(build / "size-components.json").write_text(json.dumps(components, indent=2) + "\n")
print(json.dumps(summary, indent=2))
print("PASS: embedded core and shared watch view linked; PSRAM arena and OTA slot bounds verified")
