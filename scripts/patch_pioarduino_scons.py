#!/usr/bin/env python3
"""Patch Arduino-ESP32 3.3.x for the project's SCons and Wi-Fi diagnostics."""

from pathlib import Path
import os
import re
import sys

OLD_ELF = '''action = deepcopy(env["BUILDERS"]["ElfToBin"].action)
action.cmd_list = env["BUILDERS"]["ElfToBin"].action.cmd_list.replace("-o", "--elf-sha256-offset 0xb0 -o")
env["BUILDERS"]["ElfToBin"].action = action
'''

NEW_ELF = '''env["BUILDERS"]["ElfToBin"].action.cmd_list = env["BUILDERS"]["ElfToBin"].action.cmd_list.replace(
    "-o", "--elf-sha256-offset 0xb0 -o"
)
'''

OLD_FIRST_RECONNECT_RE = re.compile(
    r'(?m)^(\s*)\} else if \(first_connect\) \{\s*//Retry once for all failure reasons\s*'
    r'first_connect = false;\s*'
    r'DoReconnect = true;\s*'
    r'log_d\("WiFi Reconnect Running"\);\s*$'
)

def replace_first_reconnect(match: re.Match) -> str:
    indent = match.group(1)
    return (
        indent + "} else if (first_connect && _sta_network_if->getAutoReconnect()) {\n"
        + indent + "  first_connect = false;\n"
        + indent + "  DoReconnect = true;\n"
        + indent + '  log_d("WiFi Reconnect Running");\n'
    )

def patch_elf_helper(target: Path) -> bool:
    text = target.read_text(encoding="utf-8")
    if OLD_ELF in text:
        text = text.replace(OLD_ELF, NEW_ELF, 1)
        target.write_text(text, encoding="utf-8")
        print(f"Patched ElfToBin action for SCons 4.11.x: {target}")
        return True
    if NEW_ELF in text:
        print(f"ElfToBin action already patched: {target}")
        return False
    print(f"ERROR: Unexpected ElfToBin helper contents; refusing to patch: {target}")
    return None

def patch_sta_retry(target: Path) -> bool:
    text = target.read_text(encoding="utf-8")
    matches = list(OLD_FIRST_RECONNECT_RE.finditer(text))
    if len(matches) == 1:
        text = OLD_FIRST_RECONNECT_RE.sub(replace_first_reconnect, text, count=1)
        target.write_text(text, encoding="utf-8")
        print(f"Patched STA first-retry behavior for single-attempt diagnostics: {target}")
        return True
    if "else if (first_connect && _sta_network_if->getAutoReconnect())" in text:
        print(f"STA first-retry behavior already patched: {target}")
        return False
    if len(matches) == 0:
        print(f"ERROR: Expected STA first-retry logic was not found: {target}")
    else:
        print(f"ERROR: Found {len(matches)} STA first-retry logic blocks; refusing to patch: {target}")
    return None

def main():
    core_dir = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    framework_dir = core_dir / "packages" / "framework-arduinoespressif32"

    elf_target = framework_dir / "tools" / "pioarduino-build.py"
    sta_target = framework_dir / "libraries" / "WiFi" / "src" / "STA.cpp"

    if not elf_target.is_file():
        print(f"ERROR: Arduino-ESP32 PlatformIO build helper not found: {elf_target}")
        return 1
    if not sta_target.is_file():
        print(f"ERROR: Arduino-ESP32 STA source not found: {sta_target}")
        return 1

    if patch_elf_helper(elf_target) is None:
        return 1
    if patch_sta_retry(sta_target) is None:
        return 1

    return 0

if __name__ == "__main__":
    sys.exit(main())
