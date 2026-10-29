#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Explicit reviewed admin fixture only. No fallback or skipped Public gate."""
import os
import pathlib
import sys

from azureus_public_admin_launcher import ENV_KEYS, launch


def main():
    if len(sys.argv) < 2 or any(not os.environ.get(key) for key in ENV_KEYS):
        raise RuntimeError("Explicit measured admin helper/mode/fresh receipt required; no fallback")
    driver = str(pathlib.Path(sys.argv[1]).resolve(strict=True))
    return launch(os.environ[ENV_KEYS[0]], pathlib.Path(os.environ[ENV_KEYS[1]]),
                  os.environ[ENV_KEYS[2]], pathlib.Path(os.environ[ENV_KEYS[3]]),
                  [driver, *sys.argv[2:]])


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"FAIL explicit Public namespace: {error}", file=sys.stderr)
        sys.exit(1)
