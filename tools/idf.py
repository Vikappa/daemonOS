"""Run the pinned project-local ESP-IDF without changing shell profiles or system Python."""
import os
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[1]
sdk = pathlib.Path(os.environ.get("IDF_PATH", root / ".cache/esp-idf"))
tools = pathlib.Path(os.environ.get("IDF_TOOLS_PATH", root / ".cache/idf-tools"))
environments = sorted((tools / "python_env").glob("idf5.5_py*_env"))
if not (sdk / "tools/idf.py").exists() or not environments:
    sys.exit("Missing local SDK/toolchain. Follow docs/embedded-build.md; no global installation is required.")
environment = environments[-1]
revision = subprocess.check_output(["git", "-C", str(sdk), "rev-parse", "HEAD"], text=True).strip()
if revision != "fcae32885b0296b32044cb99ecbdc50d98dddb83":
    sys.exit("ESP-IDF must match the pinned v5.5.1 commit; refusing an unverified SDK.")
python = environment / "bin/python"
bins = [str(p) for p in (tools / "tools").glob("*/*/*/bin")]
env = dict(os.environ, IDF_PATH=str(sdk), IDF_TOOLS_PATH=str(tools),
           IDF_PYTHON_ENV_PATH=str(environment), IDF_COMPONENT_MANAGER="0",
           IDF_PYTHON_CHECK_CONSTRAINTS="no",
           PATH=os.pathsep.join([str(environment / "bin"), *bins, os.environ.get("PATH", "")]))
command = [str(python), str(sdk / "tools/idf.py"), "-C", str(root / "firmware"),
           "-B", str(root / "build/firmware"), *sys.argv[1:]]
sys.exit(subprocess.call(command, env=env))
