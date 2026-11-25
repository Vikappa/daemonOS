"""Project-local ESP-IDF setup. Explicit downloads only; never modifies system packages."""
import argparse
import json
import os
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[1]
cache = root / ".cache"
sdk = cache / "esp-idf"
tools = cache / "idf-tools"
commit = "fcae32885b0296b32044cb99ecbdc50d98dddb83"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--check", action="store_true", help="Validate existing local setup without downloading")
args = parser.parse_args()
if sys.version_info[:2] != (3, 12):
    sys.exit("Use Python 3.12 for the pinned Ubuntu 24.04 embedded environment.")
env = dict(os.environ, IDF_TOOLS_PATH=str(tools), PIP_CACHE_DIR=str(cache / "pip"))
def run(command):
    subprocess.run(command, check=True, cwd=root, env=env)
if not sdk.exists() and not args.check:
    run(["git", "clone", "--depth", "1", "--branch", "v5.5.1", "--recursive", "--shallow-submodules",
         "https://github.com/espressif/esp-idf.git", str(sdk)])
if not sdk.exists() or subprocess.check_output(["git", "-C", str(sdk), "rev-parse", "HEAD"], text=True).strip() != commit:
    sys.exit("Local ESP-IDF does not match the pinned v5.5.1 commit; refusing to replace it.")
environment = tools / "python_env/idf5.5_py3.12_env"
python = environment / "bin/python"
if not args.check:
    run([sys.executable, str(sdk / "tools/idf_tools.py"), "install", "xtensa-esp-elf", "esp32ulp-elf"])
    if not python.exists():
        run([sys.executable, "-m", "venv", str(environment)])
    run([str(python), "-m", "pip", "install", "--no-deps", "--requirement", str(root / "tools/esp-idf-python.lock")])
if not python.exists() or not list((tools / "tools/xtensa-esp-elf").glob("*/*/bin/xtensa-esp32s3-elf-g++")):
    sys.exit("Local Python environment or Xtensa toolchain is missing.")
run([str(python), "-m", "pip", "check"])
normalize = lambda name: name.lower().replace("_", "-")
locked = {normalize(line.split("==")[0]): line.split("==")[1] for line in (root / "tools/esp-idf-python.lock").read_text().splitlines() if "==" in line}
installed = json.loads(subprocess.check_output([str(python), "-m", "pip", "list", "--disable-pip-version-check", "--format", "json"], env=env, text=True))
actual = {normalize(package["name"]): package["version"] for package in installed}
if actual != locked:
    sys.exit("Embedded Python packages do not match tools/esp-idf-python.lock.")
print("PASS: project-local ESP-IDF v5.5.1 and Xtensa toolchain available; no global configuration changed")
