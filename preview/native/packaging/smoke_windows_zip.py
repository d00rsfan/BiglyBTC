#!/usr/bin/env python3
"""Exercise the packaged Windows binaries with an isolated Unicode profile."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import zipfile

from windows_bundle import MACHINE, inventory


def relocated_environment(bin_dir, environment):
    # Windows environment names are case-insensitive. Preserve that rule
    # after copying os.environ into an ordinary dictionary.
    env = {key.upper(): value for key, value in environment.items()
           if not key.upper().startswith(("QT_", "QML"))}
    system = Path(env["SYSTEMROOT"])
    env.update(PATH=os.pathsep.join(map(str, (bin_dir, system / "System32", system))),
               QT_QPA_PLATFORM="windows", QT_FORCE_STDERR_LOGGING="1")
    return env


def smoke(archive, architecture):
    with tempfile.TemporaryDirectory(prefix="biglybtc-windows-smoke-") as temporary:
        work = Path(temporary).resolve()
        bundle = work / "relocated app Ω"
        with zipfile.ZipFile(archive) as package:
            package.extractall(bundle)
        installed = inventory(bundle, architecture)
        recorded = dict((name, digest) for digest, name in (
            line.split("  ", 1) for line in (bundle / "SHA256SUMS").read_text(encoding="utf-8").splitlines()))
        if recorded != {name: digest for name, digest in installed.items() if name != "SHA256SUMS"}:
            raise ValueError("The extracted Windows package differs from its checksums")

        profile = work / "profile 测试"
        (profile / "active").mkdir(parents=True)
        (profile / "biglybt.config").write_bytes(b"de")
        (profile / "downloads.config").write_bytes(b"d9:downloadslee")
        bin_dir = bundle / "bin"
        env = relocated_environment(bin_dir, os.environ)
        for binary in ("BiglyBTC.exe", "BiglyBTC-headless.exe"):
            result = subprocess.check_output([bin_dir / binary, "--check-profile", profile], cwd=work, env=env, timeout=30)
            report = json.loads(result)
            if (report["status"] != "METADATA_READABLE" or report["payload_reads"] or report["writes"]
                    or (profile / ".azlock").exists()):
                raise RuntimeError(f"{binary} preflight failed: {report}")

        log = work / "startup.log"
        with log.open("wb") as output, subprocess.Popen([bin_dir / "BiglyBTC.exe", "--profile", profile],
                cwd=work, env=env, stdout=output, stderr=subprocess.STDOUT) as process:
            try:
                deadline = time.monotonic() + 45
                while process.poll() is None and time.monotonic() < deadline:
                    started = log.read_bytes()
                    if (b"Library opening: ready" in started and b"Public chat transport: ready" in started
                            and (profile / ".azlock").is_file()):
                        print(started.decode(errors="replace"), end="")
                        return
                    time.sleep(0.2)
                raise RuntimeError(f"Relocated Windows app failed to start (exit {process.poll()}): {log.read_text(errors='replace')}")
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("architecture", choices=MACHINE)
    args = parser.parse_args()
    smoke(args.archive.resolve(strict=True), args.architecture)
