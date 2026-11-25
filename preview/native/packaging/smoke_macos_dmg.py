#!/usr/bin/env python3
"""Check a macOS candidate after copying it from its disk image."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def run(*command, **kwargs):
    return subprocess.check_output(command, text=True, timeout=90, **kwargs)


def smoke_app(app, work):
    run("/usr/bin/codesign", "--verify", "--deep", "--strict", app)
    binary = app / "Contents/MacOS/BiglyBTC"
    run("/usr/bin/lipo", "-verify_arch", "arm64", binary)
    home = work / "test home"
    profile = home / "Library/Application Support/BiglyBT"
    (profile / "active").mkdir(parents=True)
    (profile / "biglybt.config").write_bytes(b"de")
    (profile / "downloads.config").write_bytes(b"d9:downloadslee")
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(("DYLD_", "QT_", "QML"))}
    environment.update(HOME=str(home), QT_QPA_PLATFORM="cocoa")
    report = json.loads(run(binary, "--check-profile", profile, cwd=home, env=environment))
    if report["status"] != "METADATA_READABLE" or report["payload_reads"] or report["writes"]:
        raise RuntimeError(f"Packaged profile preflight failed: {report}")

    log = work / "startup.log"
    with log.open("wb") as output, subprocess.Popen([binary], cwd=home, env=environment,
                                                     stdout=output, stderr=subprocess.STDOUT) as app_process:
        try:
            deadline = time.monotonic() + 30
            while app_process.poll() is None and time.monotonic() < deadline:
                started = log.read_bytes()
                if (b"Library opening: ready" in started and b"Public chat transport: ready" in started
                        and (profile / ".azlock").is_file()):
                    print(started.decode(errors="replace"), end="")
                    return
                time.sleep(0.2)
            raise RuntimeError(f"Relocated app did not start: {log.read_text(errors='replace')}")
        finally:
            if app_process.poll() is None:
                app_process.terminate()
                try:
                    app_process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    app_process.kill()
                    app_process.wait()


def smoke_dmg(image):
    with tempfile.TemporaryDirectory(prefix="biglybtc-macos-smoke-") as temporary:
        work = Path(temporary).resolve()
        mount = work / "disk image"
        mount.mkdir()
        app = work / "relocated app" / "BiglyBTC.app"
        app.parent.mkdir()
        run("/usr/bin/hdiutil", "attach", "-readonly", "-nobrowse", "-mountpoint", mount, image)
        try:
            run("/usr/bin/ditto", mount / "BiglyBTC.app", app)
        finally:
            run("/usr/bin/hdiutil", "detach", mount)
        smoke_app(app, work)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    smoke_dmg(parser.parse_args().image.resolve(strict=True))
