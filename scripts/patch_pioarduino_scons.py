#!/usr/bin/env python3
"""Patch Arduino-ESP32 3.3.x's PlatformIO build helper for SCons 4.11.x."""

from pathlib import Path
import os
import sys

OLD = '''action = deepcopy(env["BUILDERS"]["ElfToBin"].action)
action.cmd_list = env["BUILDERS"]["ElfToBin"].action.cmd_list.replace("-o", "--elf-sha256-offset 0xb0 -o")
env["BUILDERS"]["ElfToBin"].action = action
'''

NEW = '''env["BUILDERS"]["ElfToBin"].action.cmd_list = env["BUILDERS"]["ElfToBin"].action.cmd_list.replace(
    "-o", "--elf-sha256-offset 0xb0 -o"
)
'''

def main():
    core_dir = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    target = core_dir / "packages" / "framework-arduinoespressif32" / "tools" / "pioarduino-build.py"

    if not target.is_file():
        print(f"ERROR: Arduino-ESP32 PlatformIO build helper not found: {target}")
        return 1

    text = target.read_text(encoding="utf-8")
    if NEW in text:
        print(f"Already patched: {target}")
        return 0
    if OLD not in text:
        print(f"ERROR: Unexpected pioarduino-build.py contents; refusing to patch: {target}")
        return 1

    target.write_text(text.replace(OLD, NEW, 1), encoding="utf-8")
    print(f"Patched for SCons 4.11.x: {target}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
