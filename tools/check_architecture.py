"""Small dependency-boundary check; no third-party Python requirements."""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parents[1]
violations = []
for folder in [root / "core/include/daemon", root / "core/framework", root / "core/services", root / "core/apps"]:
    for path in folder.rglob("*"):
        if path.suffix not in {".cpp", ".hpp"} or "platform" in path.parts:
            continue
        for line_no, line in enumerate(path.read_text().splitlines(), 1):
            if re.search(r"#\s*include\s*[<\"](?:SDL|lvgl|sqlite|esp_|freertos|Arduino|unistd|sys/)", line):
                violations.append(f"{path.relative_to(root)}:{line_no}: platform dependency in portable core")
for path in (root / "protocol").rglob("*.json"):
    import json
    try:
        json.loads(path.read_text())
    except ValueError as error:
        violations.append(f"{path.relative_to(root)}: invalid JSON: {error}")
if violations:
    print("\n".join(violations), file=sys.stderr)
    sys.exit(1)
print("PASS: portable core dependency boundaries and protocol JSON syntax")
