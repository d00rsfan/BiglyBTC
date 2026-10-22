"""Owned copied-provider identity fixtures; no configure, build or profile IO."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

C35 = "c35f1b0408508c019f002c438d70dd361a55be20286ded56711efd6bba2424e3"
INSTRUMENTED = {"3bb4f7666c002f4bdb0e303aebd235c6a39850f82813600c3a08a888f1a3f89d",
                "60ac6edd933f028f63f67f5f567066aefe069cda0693fe7c987448412fd444a3"}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stamp(path):
    value = path.stat()
    return (value.st_dev, value.st_ino, value.st_size, value.st_mode, value.st_mtime_ns)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", type=Path, required=True)
    parser.add_argument("--provider", type=Path, required=True)
    parser.add_argument("--mode", choices=("source", "strict"), required=True)
    parser.add_argument("--alternate", type=Path)
    parser.add_argument("--interposition-driver", type=Path)
    parser.add_argument("--preload")
    args = parser.parse_args()
    provider = args.provider.resolve(strict=True)
    before, original_sha = stamp(provider), digest(provider)
    if args.mode == "source":
        if original_sha != C35 or not args.alternate or not args.interposition_driver or not args.preload:
            raise ValueError("Source relocation requires exact c35f and existing rejection controls")
    elif original_sha not in INSTRUMENTED:
        raise ValueError("Strict relocation test requires an exact retained instrumented image")
    root = Path(tempfile.mkdtemp(prefix="biglybtc-runtime-deploy-results-", dir="/tmp"))
    rows = []
    cases = [("copied", "copy", "bound"), ("moved-before-load", "moved", "bound"),
             ("open-denied", "open", "unbound"), ("mapped-inode-mismatch", "inode", "unbound"),
             ("hash-content-mismatch", "content", "unbound"), ("actual-fifo", "fifo", "unbound"),
             ("actual-same-bytes-replaced-inode", "replace", "unbound"),
             ("actual-loaded-name-missing", "missing", "unbound"),
             ("actual-distro-copy", "alternate", "unbound"), ("preload", "preload", "unbound")]
    if args.mode == "strict":
        cases = [("strict-copy", "strict", "unbound"), ("strict-moved-before-load", "strict", "unbound")]
    for name, fault, expected in cases:
        area = Path(tempfile.mkdtemp(prefix="biglybtc-runtime-deploy-", dir="/tmp"))
        initial = area / ("before-move" if "moved-before-load" in name else "lib")
        initial.mkdir(mode=0o700)
        selected = args.alternate.resolve(strict=True) if fault == "alternate" else provider
        selected_sha, selected_stamp = digest(selected), stamp(selected)
        if fault == "alternate" and selected_sha != "bbe6713f187140db190cc86e1140de36d006954d9b1ee1c581eeeb5cd98af427":
            raise ValueError("Unexpected independently retained distro fixture")
        image = initial / "libtorrent-rasterbar.so.2.0.12"
        shutil.copy2(selected, image)
        (initial / "libtorrent-rasterbar.so.2.0").symlink_to(image.name)
        (initial / "libtorrent-rasterbar.so").symlink_to(image.name)
        if initial.name != "lib":
            initial.rename(area / "lib")
        image = area / "lib/libtorrent-rasterbar.so.2.0.12"
        if image.stat().st_ino == selected.stat().st_ino and image.stat().st_dev == selected.stat().st_dev:
            raise ValueError("Fixture did not create an independent inode")
        environment = os.environ.copy()
        environment.pop("LD_PRELOAD", None)
        environment.pop("LD_AUDIT", None)
        environment["LD_LIBRARY_PATH"] = str(area / "lib") + ":" + environment.get("LD_LIBRARY_PATH", "")
        if fault == "preload":
            environment["LD_PRELOAD"] = args.preload
        argv = [str(args.driver.resolve(strict=True)), str(area), fault, expected]
        result = subprocess.run(argv, env=environment, capture_output=True, timeout=15)
        (root / (name + ".stdout")).write_bytes(result.stdout)
        (root / (name + ".stderr")).write_bytes(result.stderr)
        if result.returncode or digest(image) != selected_sha or stamp(selected) != selected_stamp or digest(selected) != selected_sha:
            raise ValueError("Copied runtime fixture failed: " + name + ": " + result.stderr.decode(errors="replace"))
        if b"original_opens=0" not in result.stdout or b"mapped_copy=" not in result.stdout:
            raise ValueError("Missing actual copied-origin/original-path controls: " + name)
        rows.append({"case": name, "fault": fault, "expected": expected, "argv": argv,
                     "owned_root": str(area), "selected_provider_sha256": selected_sha,
                     "copied_provider_sha256": digest(image), "source_stamp_unchanged": True,
                     "exit_code": result.returncode, "stdout_sha256": digest(root / (name + ".stdout")),
                     "stderr_sha256": digest(root / (name + ".stderr"))})
        (root / "cases.json").write_text(json.dumps(rows, indent=2) + "\n")
        if fault == "preload":
            # Existing fixture explicitly unsets LD_PRELOAD after it is mapped,
            # proves original/default exports differ, then requires rejection.
            argv = [str(args.interposition_driver.resolve(strict=True))]
            mapped = subprocess.run(argv, env=environment, capture_output=True, timeout=15)
            (root / "mapped-interposition.stdout").write_bytes(mapped.stdout)
            (root / "mapped-interposition.stderr").write_bytes(mapped.stderr)
            if mapped.returncode:
                raise ValueError("Copied-provider actual mapped-interposition rejection failed")
            rows.append({"case": "mapped-interposition-after-env-removed", "argv": argv,
                         "owned_root": str(area), "exit_code": mapped.returncode})
    if stamp(provider) != before or digest(provider) != original_sha:
        raise ValueError("Retained provider was changed")
    receipt = {"scope": "Actual copied-provider identity plus explicit synthetic IO rejection seams",
               "provider": str(provider), "provider_sha256": original_sha, "mode": args.mode,
               "cases": rows, "provider_unchanged": True, "owner_or_profile_created": False,
               "causal_or_writer_credit": False}
    (root / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"receipt": str(root / "result.json"), "cases": len(rows), "mode": args.mode}))


if __name__ == "__main__":
    main()
