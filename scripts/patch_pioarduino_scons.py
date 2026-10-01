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

# Arduino-ESP32 3.3.x unconditionally performs one reconnect after the first
# STA_DISCONNECTED event, even when setAutoReconnect(false) was requested.
# That hidden retry makes a "single authentication attempt" diagnostic
# impossible. Patch it so the first retry also honors auto-reconnect.
OLD_FIRST_RECONNECT = '''    } else if (first_connect) {               //Retry once for all failure reasons
      first_connect = false;
      DoReconnect = true;
      log_d("WiFi Reconnect Running");
'''
NEW_FIRST_RECONNECT = '''    } else if (first_connect && _sta_network_if->getAutoReconnect()) {
      first_connect = false;
      DoReconnect = true;
      log_d("WiFi Reconnect Running");
'''

def main():
    core_dir = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    target = core_dir / "packages" / "framework-arduinoespressif32" / "tools" / "pioarduino-build.py"

    if not target.is_file():
        print(f"ERROR: Arduino-ESP32 PlatformIO build helper not found: {target}")
        return 1

    text = target.read_text(encoding="utf-8")
    original = text

    if OLD in text:
        text = text.replace(OLD, NEW, 1)
        print(f"Patched ElfToBin action for SCons 4.11.x: {target}")
    elif NEW in text:
        print(f"ElfToBin action already patched: {target}")
    else:
        print(f"ERROR: Unexpected ElfToBin helper contents; refusing to patch: {target}")
        return 1

    if OLD_FIRST_RECONNECT in text:
        text = text.replace(OLD_FIRST_RECONNECT, NEW_FIRST_RECONNECT, 1)
        print(f"Patched STA first-retry behavior for single-attempt diagnostics: {target}")
    elif NEW_FIRST_RECONNECT in text:
        print(f"STA first-retry behavior already patched: {target}")
    else:
        print(f"ERROR: Unexpected STA reconnect logic; refusing to patch: {target}")
        return 1

    if text != original:
        target.write_text(text, encoding="utf-8")

    return 0

if __name__ == "__main__":
    sys.exit(main())
