#!/usr/bin/env python3
# Post-build script: converts the compiled .hex to a .uf2 file suitable for
# drag-and-drop flashing via the nRF52840 Adafruit bootloader (UF2 mass storage).
#
# Family ID 0xADA52840 is the Adafruit-specific nRF52840 UF2 family.

import sys
from os.path import basename

Import("env")


def nrf52_hex_to_uf2(source, target, env):
    hex_path = target[0].get_abspath()
    uf2_path = hex_path.replace(".hex", ".uf2")
    env.Execute(
        env.VerboseAction(
            f'"{sys.executable}" bin/uf2conv.py "{hex_path}" -c -f 0xADA52840 -o "{uf2_path}"',
            f"Generating UF2: {basename(uf2_path)}",
        )
    )


env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", nrf52_hex_to_uf2)
