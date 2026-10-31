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
SELECTED = "d367afd6499859c5a57d66bddd352a977ff76c7fb76b5925302c2c9008c7ddbd"
NEXT_MANIFEST = "3342ec8ae72ecafa4a1b887e664d0d066fffce4f7d864b0b130ca903be953a3a"
NEXT_IMAGE = "b0e55bfbdc2be5dc2ebbefc953837d64c9e5151b0305332335071e37b6956937"
WEBSEED_MANIFEST = "ea82f78982a1428eb64067719cf30bbca7cfd09dd2abfcf8f76024f6e6ed41d5"
WEBSEED_IMAGE = "d33c17c73c3c377cedd88256b0b48e472b2de55f0a95a890e2837556e1d1002a"
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
    parser.add_argument("--mode", choices=("source", "selected", "provider-next", "webseed", "queue", "traffic", "socks", "mainline", "original-identity", "mainline-routing", "coexistence", "strict"), required=True)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--alternate", type=Path)
    parser.add_argument("--interposition-driver", type=Path)
    parser.add_argument("--preload")
    args = parser.parse_args()
    provider = args.provider.resolve(strict=True)
    before, original_sha = stamp(provider), digest(provider)
    if args.mode == "source":
        if original_sha != C35 or not args.alternate or not args.interposition_driver or not args.preload:
            raise ValueError("Source relocation requires exact c35f and existing rejection controls")
    elif args.mode == "selected":
        if (original_sha != SELECTED or provider.stat().st_size != 9072560
                or not args.alternate or not args.interposition_driver or not args.preload):
            raise ValueError("Selected relocation requires exact d367 and existing rejection controls")
    elif args.mode == "provider-next":
        if (len(NEXT_MANIFEST) != 64 or not args.manifest or digest(args.manifest) != NEXT_MANIFEST
                or original_sha != NEXT_IMAGE or provider.stat().st_size != 9085072
                or not args.alternate or not args.interposition_driver or not args.preload):
            raise ValueError("UNAPPROVED next provider or missing exact relocation rejection controls")
        declared = json.loads(args.manifest.read_bytes())
        if (Path(declared["artifact"]).resolve(strict=True) != provider
                or declared["artifact_sha256"] != NEXT_IMAGE or declared["artifact_bytes"] != 9085072):
            raise ValueError("Next-provider relocation input differs from the exact manifest")
    elif args.mode == "webseed":
        if (not args.manifest or digest(args.manifest) != WEBSEED_MANIFEST
                or original_sha != WEBSEED_IMAGE or provider.stat().st_size != 9085128
                or not args.alternate or not args.interposition_driver or not args.preload):
            raise ValueError("Webseed relocation requires exact reviewed source/image and all rejection controls")
        declared = json.loads(args.manifest.read_bytes())
        if (Path(declared["artifact"]).resolve(strict=True) != provider
                or declared["artifact_sha256"] != WEBSEED_IMAGE or declared["artifact_bytes"] != 9085128):
            raise ValueError("Webseed relocation input differs from the exact manifest")
    elif args.mode == "queue":
        if (not args.manifest or digest(args.manifest) != "f95542706a3b7324c6f2589d90d51a4374c92e7e245319b156f044aeec76a1d8"
                or original_sha != "7449ec6f503f4b8da26e61f4c0926ca414774f6b859405439eec2ebe227fbd9e" or provider.stat().st_size != 9085816
                or not args.alternate or not args.interposition_driver or not args.preload):
            raise ValueError("Queue observer relocation requires exact source/image and all rejection controls")
        declared = json.loads(args.manifest.read_bytes())
        if (Path(declared["artifact"]).resolve(strict=True) != provider
                or declared["artifact_sha256"] != original_sha or declared["artifact_bytes"] != provider.stat().st_size):
            raise ValueError("Queue observer relocation differs from the reviewed manifest")
    elif args.mode == "traffic":
        if (not args.manifest or digest(args.manifest) != "5901b34eeb25370106bdbf6185a80f0c86c62a837bf2e455f5619921db665760"
                or original_sha != "98279a468e0fa5147876b6f22f8947f432c294dfc02fb50cf179fc9472b3b51f" or provider.stat().st_size != 9090088
                or not args.alternate or not args.interposition_driver or not args.preload):
            raise ValueError("Traffic V3 relocation requires exact source/image and all rejection controls")
        declared = json.loads(args.manifest.read_bytes())
        if (Path(declared["artifact"]).resolve(strict=True) != provider
                or declared["artifact_sha256"] != original_sha or declared["artifact_bytes"] != provider.stat().st_size):
            raise ValueError("Traffic V3 relocation differs from the reviewed manifest")
    elif args.mode == "socks":
        if (not args.manifest or digest(args.manifest) != "2a5e56dd4987756e74871c3aad015ad93974e6d47aad8754b0ad05d09f3ca94e"
                or original_sha != "a5b78ccb4ee17a6773899e797d867844c8a24a45358e260b7ab65a93e2954187" or provider.stat().st_size != 9139888
                or not args.alternate or not args.interposition_driver or not args.preload):
            raise ValueError("SOCKS cancellation relocation requires exact source/image and all rejection controls")
        declared = json.loads(args.manifest.read_bytes())
        if (Path(declared["artifact"]).resolve(strict=True) != provider
                or declared["artifact_sha256"] != original_sha or declared["artifact_bytes"] != provider.stat().st_size):
            raise ValueError("SOCKS cancellation relocation differs from the reviewed manifest")
    elif args.mode == "mainline":
        if (not args.manifest or digest(args.manifest) != "1267bf58a235dc6c32d9ffadab8bbd994ec81f2aa958f0cf621f8a71e4f6c84c"
                or original_sha != "9fbb550bbc1f877ce34ce21849deab97971f5d3eacfbc531a7b777d28a3b2eea" or provider.stat().st_size != 9153448
                or not args.alternate or not args.interposition_driver or not args.preload):
            raise ValueError("Mainline PORT relocation requires exact source/image and all rejection controls")
        declared = json.loads(args.manifest.read_bytes())
        if (Path(declared["artifact"]).resolve(strict=True) != provider
                or declared["artifact_sha256"] != original_sha or declared["artifact_bytes"] != provider.stat().st_size):
            raise ValueError("Mainline PORT relocation differs from the reviewed manifest")
    elif args.mode == "original-identity":
        if (not args.manifest or digest(args.manifest) != "68c25c3ac2680513deef488f7f74dc0c6788b1802df43361ef00886c962bde58"
                or original_sha != "3e7bb59a4b419bc8f5198dd8ebf4b1a6cd2c1b106a7f032759bdbf552faa1890" or provider.stat().st_size != 9184016
                or not args.alternate or not args.interposition_driver or not args.preload):
            raise ValueError("Original identity/tracker relocation requires exact source/image and all rejection controls")
        declared = json.loads(args.manifest.read_bytes())
        if (Path(declared["artifact"]).resolve(strict=True) != provider
                or declared["artifact_sha256"] != original_sha or declared["artifact_bytes"] != provider.stat().st_size):
            raise ValueError("Original identity/tracker relocation differs from the reviewed manifest")
    elif args.mode == "mainline-routing":
        if (not args.manifest or digest(args.manifest) != "f746fd279755ea90f31e20b0c9131c46e5f0871c4aab3665f232975f98feb56e"
                or original_sha != "355f0c7bca7404c3199e85d4741a3734ac460c79d641a2539c049c5a8ad0c443" or provider.stat().st_size != 9198832
                or not args.alternate or not args.interposition_driver or not args.preload):
            raise ValueError("Original identity/tracker relocation requires exact source/image and all rejection controls")
        declared = json.loads(args.manifest.read_bytes())
        if (Path(declared["artifact"]).resolve(strict=True) != provider
                or declared["artifact_sha256"] != original_sha or declared["artifact_bytes"] != provider.stat().st_size):
            raise ValueError("Original identity/tracker relocation differs from the reviewed manifest")
    elif args.mode == "coexistence":
        if (not args.manifest or digest(args.manifest) != "62f7545f96af46806e7a7708213319ffd1e91c7a5228f0c35065eaa249a91343"
                or original_sha != "7ffbd4c63d7770168bcbdf8e8c19644e18d1509d57ac148331a3c4bd094310c4" or provider.stat().st_size != 9659328
                or not args.alternate or not args.interposition_driver or not args.preload):
            raise ValueError("Coexistence relocation requires the approved all34 source/image and every rejection fixture")
        declared = json.loads(args.manifest.read_bytes())
        if (Path(declared["artifact"]).resolve(strict=True) != provider
                or declared["artifact_sha256"] != original_sha or declared["artifact_bytes"] != provider.stat().st_size):
            raise ValueError("Coexistence relocation differs from the reviewed manifest")
    elif original_sha not in INSTRUMENTED:
        raise ValueError("Strict relocation test requires an exact retained instrumented image")
    root = Path(tempfile.mkdtemp(prefix="runtime-deployment-results-", dir=Path.cwd()))
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
        # Temporary image trees are removed on success, assertion, timeout or exception.
        with tempfile.TemporaryDirectory(prefix="biglybtc-runtime-deploy-", dir="/tmp") as area_name:
            area = Path(area_name)
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
            if args.mode == "coexistence":
                proof = b"proof=coexistence34 retained_pin=" + (b"1" if expected == "bound" else b"0")
                if proof not in result.stdout:
                    raise ValueError("Missing retained complete34-export proof: " + name)
            if args.mode == "mainline-routing":
                proof = b"proof=mainline_routing27 retained_pin=" + (b"1" if expected == "bound" else b"0")
                if proof not in result.stdout:
                    raise ValueError("Missing retained complete27-export proof: " + name)
            if args.mode == "original-identity":
                proof = b"proof=original_identity_tracker25 retained_pin=" + (b"1" if expected == "bound" else b"0")
                if proof not in result.stdout:
                    raise ValueError("Missing retained complete25-export proof: " + name)
            if args.mode == "mainline":
                proof = b"proof=mainline20 retained_pin=" + (b"1" if expected == "bound" else b"0")
                if proof not in result.stdout:
                    raise ValueError("Missing actual retained full20-export PORT proof: " + name)
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
               "owned_fixture_directories_removed": True,
               "causal_or_writer_credit": False}
    (root / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"receipt": str(root / "result.json"), "cases": len(rows), "mode": args.mode}))


if __name__ == "__main__":
    main()
