"""Run Clang's C++ static analyzer using the actual CMake include paths/defines."""
import argparse
import json
import pathlib
import shlex
import shutil
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build-dir", type=pathlib.Path, default=root / "build/desktop")
parser.add_argument("--core-only", action="store_true")
parser.add_argument("--clang", default=shutil.which("clang++"))
args = parser.parse_args()
if not args.clang:
    sys.exit("clang++ is required for C++ static analysis.")
database = json.loads((args.build_dir / "compile_commands.json").read_text())
count = 0
for unit in database:
    source = pathlib.Path(unit["file"]).resolve()
    if not source.is_relative_to(root) or source.suffix != ".cpp":
        continue
    relative = source.relative_to(root)
    if relative.parts[0] not in {"core", "presentation", "simulator"}:
        continue
    if args.core_only and (relative.parts[0] != "core" or "platform" in relative.parts):
        continue
    original = unit.get("arguments") or shlex.split(unit["command"])
    flags = []
    iterator = iter(original[1:])
    for flag in iterator:
        if flag in {"-I", "-isystem", "-D"}:
            flags.extend([flag, next(iterator)])
        elif flag.startswith(("-I", "-D", "-std=")):
            flags.append(flag)
    result = subprocess.run([args.clang, "--analyze", "-Xanalyzer", "-analyzer-output=text",
        *flags, str(source), "-o", "/dev/null"], cwd=unit["directory"], capture_output=True, text=True)
    if result.returncode or "warning:" in result.stderr or "error:" in result.stderr:
        sys.stderr.write(result.stdout + result.stderr)
        sys.exit(result.returncode or 1)
    count += 1
print(f"PASS: Clang static analyzer, {count} first-party C++ translation units")
