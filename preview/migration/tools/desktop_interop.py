#!/usr/bin/env python3
"""Original Java partial -> real offscreen BiglyBTC Qt shell -> original Java."""

import argparse
import json
import os
import pathlib
import subprocess
import time

from handoff import decode
from oracle import verify_classes
from peer_interop import jvm, unused_port, owned_peer_port
from sources import ROOT, save, sha256


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test", type=pathlib.Path, required=True)
    parser.add_argument("--peer", type=pathlib.Path, required=True)
    parser.add_argument("--type", type=int, choices=(1, 2, 3), required=True)
    parser.add_argument("--action", choices=("complete", "cancel", "close"), default="complete")
    parser.add_argument("--work", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    if work.parent != pathlib.Path("/tmp") or not work.name.startswith("biglybtc-"):
        raise ValueError("Only newly generated /tmp/biglybtc-* test roots are accepted")
    oracle = verify_classes(pathlib.Path("/tmp/biglybtc-oracle/classes"))
    preparation = work / "preparation.json"
    subprocess.run(["python3", str(ROOT / "migration/tools/prepare_profile.py"), "--type", str(args.type),
        "--work", str(work), "--output", str(preparation)], check=True, timeout=240)
    prepared = json.loads(preparation.read_text())
    profile = work / "java/profile"
    config = (profile / "biglybt.config").read_bytes()
    inventory, _ = decode((profile / "downloads.config").read_bytes())
    identity = inventory[b"downloads"][0][b"torrent_hash"].hex().upper()
    state_path = profile / "active" / (identity + ".dat")
    original_state, original_spans = decode(state_path.read_bytes())
    expected = pathlib.Path(prepared["expected"]).read_bytes()
    payload = pathlib.Path(prepared["payload"])
    original_inode = (payload.stat().st_dev, payload.stat().st_ino)
    native_sources = {str(p.relative_to(ROOT)): sha256(p.read_bytes()) for p in (ROOT / "native").rglob("*")
        if p.is_file() and (p.suffix in (".cpp", ".hpp") or p.name in ("CMakeLists.txt", "CMakePresets.json"))}
    for name in ("qt-cache", "qt-config", "qt-runtime"):
        (work / name).mkdir(mode=0o700)
    env = os.environ.copy()
    env.update(QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1", XDG_CACHE_HOME=str(work / "qt-cache"),
        XDG_CONFIG_HOME=str(work / "qt-config"), XDG_RUNTIME_DIR=str(work / "qt-runtime"),
        BIGLYBTC_TEST_PROFILE=str(profile), BIGLYBTC_TEST_ACTION=args.action,
        BIGLYBTC_TEST_EXPECTED=prepared["expected"], BIGLYBTC_TEST_CAPTURE=str(work / "BiglyBTC-desktop.png"))
    started = time.monotonic()
    with (work / "seed.log").open("w") as log:
        peer = subprocess.Popen([str(args.peer), "--seed", str(work / "generation/fixtures"), str(args.type), str(work / "seed")],
            stdout=log, stderr=subprocess.STDOUT)
        try:
            ready = work / "seed/ready"
            env["BIGLYBTC_TEST_PEER_PORT"] = str(owned_peer_port(ready, peer))
            gui_log = work / "desktop.log"
            with gui_log.open("w") as output:
                result = subprocess.run([str(args.test), "realProfile"], env=env, stdout=output,
                    stderr=subprocess.STDOUT, timeout=180)
            if result.returncode or "DESKTOP_PROFILE_VERIFIED" not in gui_log.read_text(errors="replace"):
                raise ValueError("Actual native desktop profile test failed; see " + str(gui_log))
        finally:
            if (work / "seed").exists():
                (work / "seed/stop").touch()
            try:
                peer.wait(timeout=15)
            except subprocess.TimeoutExpired:
                peer.terminate()  # Exact test Popen created above.
                peer.wait(timeout=10)
    if (profile / "biglybt.config").read_bytes() != config:
        raise ValueError("Native desktop changed original global settings")
    native_state, native_spans = decode(state_path.read_bytes())
    if native_spans[(b"info",)] != original_spans[(b"info",)] or native_state[b"unknown.future.plugin"] != original_state[b"unknown.future.plugin"]:
        raise ValueError("Native desktop changed torrent info or unknown plugin data")
    complete = payload.with_name("generated.bin")
    actual = complete if complete.exists() else payload
    data = actual.read_bytes()
    if (actual.stat().st_dev, actual.stat().st_ino) != original_inode:
        raise ValueError("Desktop continuation replaced the original payload inode")
    pieces = sum(data[i:i + 16384] == expected[i:i + 16384] for i in range(0, len(expected), 16384))
    if pieces < 1 or (args.action == "complete" and (pieces != 8 or payload.exists())):
        raise ValueError("Desktop lost original data or failed completion/rename")
    java = work / "java"
    listen = unused_port()
    classes = str(work / "classes") + ":/tmp/biglybtc-oracle/classes"
    role = "reopen" if pieces == 8 else "inspect-partial"
    command = jvm(java, classes, (listen,)) + ["EngineOracle", str(listen), "0", str(java / "input.torrent"),
        prepared["expected"], role, str(pieces)]
    java_log = work / "java-reopen.log"
    with java_log.open("w") as output:
        result = subprocess.run(command, cwd=java, stdout=output, stderr=subprocess.STDOUT, timeout=150)
    text = java_log.read_text(errors="replace")
    marker = "ENGINE_REOPEN_PIECES 8" if pieces == 8 else "ENGINE_PARTIAL_RECHECK " + str(pieces)
    if result.returncode or marker not in text or "ENGINE_STATE_PERSISTED" not in text:
        raise ValueError("Original Java could not reopen the same desktop profile; see " + str(java_log))
    native_profile = {str(p.relative_to(profile)): sha256(p.read_bytes()) for p in profile.rglob("*") if p.is_file()}
    save(args.output, {"status": "passed", "source_commit": oracle["source_commit"],
        "core_class_digest": oracle["core_class_digest"], "torrent_type": args.type, "action": args.action,
        "same_profile_at_every_stage": True, "profile": str(profile), "verified_pieces_after_gui": pieces,
        "gui_test_binary_sha256": sha256(args.test.read_bytes()), "peer_binary_sha256": sha256(args.peer.read_bytes()),
        "native_sources_sha256": native_sources, "profile_after_java_sha256": native_profile,
        "screenshot": str(work / "BiglyBTC-desktop.png"), "screenshot_sha256": sha256((work / "BiglyBTC-desktop.png").read_bytes()),
        "logs": [str(gui_log), str(java_log)], "elapsed_seconds": time.monotonic() - started,
        "assertions": ["real Qt model/window/native worker", "profile lease held during idle UI",
            "backend rejects stale generation", "original inode/data/settings/info/unknown fields preserved",
            "continue/cancel/close outcome reopens in original Java"],
        "limitations": ["Offscreen generated single-file local workload only", "No full desktop/plugin/platform/accessibility/performance qualification"]})
    print("PASS actual Qt desktop -> original Java; type", args.type, "action", args.action)


if __name__ == "__main__":
    main()
