"""PlatformIO pre-script: writes src/fw_version.cpp with the git version and the build time.

Only that one file changes per build, so it alone recompiles. It is gitignored.
"""
import datetime
import subprocess
from pathlib import Path

Import("env")  # noqa: F821  (SCons builtin)

root = Path(env["PROJECT_DIR"])  # noqa: F821
try:
    v = subprocess.check_output(["git", "describe", "--always", "--dirty"], cwd=root, text=True).strip()
except Exception:
    v = "unknown"
stamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
(root / "src" / "fw_version.cpp").write_text(
    f'#include "fw_version.h"\n\nconst char kFwVersion[] = "{v} {stamp}";\n'
)
