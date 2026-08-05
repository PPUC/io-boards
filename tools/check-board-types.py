#!/usr/bin/env python3
"""Checks that PlatformIO environments and protocol board types agree.

The firmware carries two static_asserts: that PPUC_BOARD_TYPE is a type the
protocol knows, and that PPUCBoardTypes.h has a profile for it. Neither can
catch a *forgotten* flag - an environment with no -D PPUC_BOARD_TYPE compiles
happily and reports whatever the default in PPUC.h says, which means a board
would announce itself as the wrong type and be handed the wrong firmware.

Nor can they catch a mismatch between the environment name and the value: the
host pairs an image on disk to a board by name, so `[env:Opto_16]` compiled
with the IO_16_8_1 value produces a file that will be flashed to the wrong
hardware.

This checks both, plus that every board type the protocol defines actually has
an environment to build it.

Exit status is non-zero on any disagreement, so CI fails rather than shipping
mismatched images.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def board_types_from_protocol() -> dict:
    """name -> value, read from BoardTypeName() in the protocol header."""
    text = (ROOT / "src" / "PPUCProtocolV2.h").read_text()

    values = dict(
        re.findall(r"(kBoardType\w+)\s*=\s*0x([0-9A-Fa-f]+)", text)
    )
    values = {k: int(v, 16) for k, v in values.items()}

    names = {}
    for symbol, name in re.findall(
        r"case\s+(kBoardType\w+):\s*return\s+\"([^\"]+)\";", text
    ):
        if symbol not in values:
            sys.exit(f"BoardTypeName() returns a name for unknown {symbol}")
        names[name] = values[symbol]
    return names


def envs_from_platformio() -> dict:
    """env name -> PPUC_BOARD_TYPE value, or None when the flag is absent."""
    text = (ROOT / "platformio.ini").read_text()
    envs = {}
    for block in re.split(r"^\[", text, flags=re.M)[1:]:
        header, _, body = block.partition("]")
        if not header.startswith("env:"):
            continue
        name = header[len("env:"):].strip()
        if name == "native":  # host test environment, not a board
            continue
        match = re.search(r"-D\s*PPUC_BOARD_TYPE\s*=\s*(\d+)", body)
        envs[name] = int(match.group(1)) if match else None
    return envs


def main() -> int:
    protocol = board_types_from_protocol()
    envs = envs_from_platformio()
    problems = []

    for env, value in sorted(envs.items()):
        if value is None:
            problems.append(
                f"[env:{env}] has no -D PPUC_BOARD_TYPE, so it would report "
                f"the default from PPUC.h and be handed another board's firmware"
            )
            continue
        if env not in protocol:
            problems.append(
                f"[env:{env}] is not a board type name the protocol knows; "
                f"the host pairs images to boards by this name"
            )
            continue
        if protocol[env] != value:
            problems.append(
                f"[env:{env}] builds PPUC_BOARD_TYPE={value} but the protocol "
                f"calls {value if value not in protocol.values() else ''}"
                f"that name {protocol[env]}"
            )

    for name in sorted(protocol):
        if name not in envs:
            problems.append(
                f"board type {name} has no [env:{name}] to build it"
            )

    if problems:
        print("Board type / environment mismatch:\n")
        for problem in problems:
            print(f"  - {problem}")
        return 1

    print(f"Board types and environments agree ({len(envs)} board(s)):")
    for env, value in sorted(envs.items(), key=lambda kv: kv[1]):
        print(f"  {value}  {env}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
