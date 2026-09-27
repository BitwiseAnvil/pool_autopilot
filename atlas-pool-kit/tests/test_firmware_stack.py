"""Check the compiled OTA callback's stack budget before an Atlas upload.

The portable engine tests cannot detect overflow of ESPHome's 8 KiB loop task
when encrypted OTA calls into the adapter. Inspect the actual Xtensa prologue.
"""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("firmware", type=Path)
parser.add_argument("--objdump", default="xtensa-esp-elf-objdump")
args = parser.parse_args()
assembly = subprocess.check_output(
    [args.objdump, "-d", "-C", str(args.firmware)], text=True
)
symbol = "esphome::atlas_pool_device::AtlasPool::snapshot_(unsigned long)"
match = re.search(
    r"^[0-9a-f]+ <" + re.escape(symbol) + r">:\n[^\n]*\bentry\s+a1, (0x[0-9a-f]+)",
    assembly, re.MULTILINE,
)
assert match, "Cannot resolve snapshot stack frame; review the new compiler output"
frame = int(match.group(1), 16)
assert frame <= 1024, f"OTA snapshot stack frame is {frame} bytes; maximum is 1024"
print(f"Compiled OTA snapshot stack budget passed: {frame} / 1024 bytes.")
