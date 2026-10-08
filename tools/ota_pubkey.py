"""PlatformIO pre-script: writes src/ota_pubkey.cpp with the Ed25519 public key that Bluetooth
firmware updates must be signed with.

The key is read from $OTA_PUBKEY, or ~/.config/rv-level/ota_ed25519.pub (64 hex digits), which
`uv run tools/ota/sign.py keygen` creates. The build stops if there is none, or if it is not a usable key. The output is gitignored.
"""
import os
import sys
from pathlib import Path

Import("env")  # noqa: F821  (SCons builtin)

path = Path(os.environ.get("OTA_PUBKEY", Path.home() / ".config" / "rv-level" / "ota_ed25519.pub"))
try:
    key = bytes.fromhex(path.read_text().strip())
except (OSError, ValueError) as e:
    key, err = None, str(e)
else:
    sys.path.insert(0, str(Path(env["PROJECT_DIR"]) / "tools" / "ota"))  # noqa: F821
    from ed25519_key import check_public_key
    err = check_public_key(key)
if err:
    sys.stderr.write(f"\nOTA signing key: cannot use {path}: {err}\n"
                     "Create one with:  uv run tools/ota/sign.py keygen\n\n")
    env.Exit(1)  # noqa: F821

out = Path(env["PROJECT_DIR"]) / "src" / "ota_pubkey.cpp"  # noqa: F821
body = ", ".join(f"0x{b:02x}" for b in key)
text = f'#include "ota_pubkey.h"\n\nconst uint8_t kOtaPublicKey[32] = {{{body}}};\n'
if not out.exists() or out.read_text() != text:  # rewrite only on change, so it isn't rebuilt every time
    out.write_text(text)
