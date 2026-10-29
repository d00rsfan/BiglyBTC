#!/usr/bin/env python3
"""Open the offline BiglyBTC preview using fresh generated data only."""

import argparse
import datetime
import gzip
import hashlib
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile


def encode(value):
    if isinstance(value, str):
        value = value.encode("utf-8")
    if isinstance(value, bytes):
        return str(len(value)).encode() + b":" + value
    if isinstance(value, int):
        return b"i" + str(value).encode() + b"e"
    if isinstance(value, list):
        return b"l" + b"".join(map(encode, value)) + b"e"
    if isinstance(value, dict):
        return b"d" + b"".join(encode(key) + encode(value[key]) for key in sorted(value)) + b"e"
    raise TypeError(type(value))


def validate_root(root):
    if root.parent != Path("/tmp") or not root.name.startswith("biglybtc-") or root.resolve() != root:
        raise ValueError("The preview requires a generated /tmp/biglybtc-* directory")


def prepare(root):
    """Populate a new, private sample profile; never import an existing profile."""
    validate_root(root)
    profile = root / "profile"
    active = profile / "active"
    active.mkdir(parents=True, mode=0o700)
    payload = root / "payload"
    payload.mkdir(mode=0o700)
    downloads, cache, paused = [], [], []
    samples = [
        ("Welcome to BiglyBTC.txt", 0, 1, 1),
        ("Sample music — ש العربية", 70, 0, 2),
        ("Example documents", 75, 0, 2),
        ("Paused sample.bin", 0, 0, 1),
    ]
    for index, (name, state, complete, count) in enumerate(samples):
        size = (index + 1) * 32768
        info = {"name": name, "piece length": 16384,
                "pieces": hashlib.sha1(b"synthetic preview piece").digest() * (size // 16384)}
        if count == 1:
            info["length"] = size
        else:
            info["files"] = [{"length": size // 2, "path": ["Read me.txt"]},
                             {"length": size // 2, "path": ["Examples", "Unicode — ש.txt"]}]
        identity = hashlib.sha1(encode(info)).digest()
        completed = size if complete else size // 4
        attributes = {"displayname": name}
        # Selected-file totals are synthetic saved snapshots, not live progress.
        # Match the generated saved priorities; the last row deliberately lacks
        # a skipped-size snapshot, so its dependent saved metrics stay unknown.
        if index == 0:
            attributes.update({"skipfss": 0, "skipbdl": 0, "parameters": {"dndflags": 0}})
        elif index == 1:
            attributes.update({"skipfss": size // 2, "skipbdl": 0, "parameters": {"dndflags": 1}})
        elif index == 2:
            attributes.update({"skipfss": size, "skipbdl": completed, "parameters": {"dndflags": 3}})
        torrent = {"info": info, "attributes": attributes}
        torrent_path = active / (identity.hex().upper() + ".dat")
        torrent_path.write_bytes(encode(torrent))
        downloads.append({"torrent": str(torrent_path), "torrent_hash": identity,
                          "save_dir": str(payload), "save_file": name,
                          "persistent": 1, "state": state, "position": index + 1,
                          "completedbytes": completed,
                          "downloaded": size * 2, "uploaded": size // 2})
        if index in (1, 2):
            downloads[-1]["file_priorities"] = [1, 0] if index == 1 else [0, 0]
        cache.append({"hash": identity, "name": name, "size": size, "fc": count, "tt": 1,
                      "attributes": attributes})
        if index == 3:
            paused.append({"hash": identity, "force": 0, "owner": 1})
    (profile / "downloads.config").write_bytes(encode({"downloads": downloads, "pause_data": paused}))
    (active / "cache.dat").write_bytes(gzip.compress(encode({"state": cache}), mtime=0))
    # Saved service settings are synthetic display data. The preview never starts
    # a tracker, scans a share, resolves a cache path or creates payload files.
    (profile / "tracker.config").write_bytes(encode({"torrents": [
        {"hash": cache[0]["hash"], "status": 1, "persistent": 1, "passive": 0,
         "dateadded": 1700000000000, "torrent_file": str(active / "synthetic-host.torrent"),
         "stats": {"completed": 3, "announces": 12, "uploaded": 65536,
                   "downloaded": 32768, "scrapes": 4}},
        {"hash": hashlib.sha1(b"synthetic saved statistics").digest(),
         "persistent": 0, "passive": 0, "backup_time": 1700000000000,
         "stats": {"completed": 1, "announces": 5, "uploaded": 16384, "downloaded": 8192}},
    ]}))
    (profile / "sharing.config").write_bytes(encode({"resources": [
        {"type": 1, "file": str(payload / "Synthetic shared file.txt"),
         "ihash": hashlib.sha1(b"synthetic file fingerprint").digest(),
         "ifile": "synthetic-file-cache.torrent", "attributes": {"Category": "Preview"},
         "props": {"persistent": "true"}},
        {"type": 3, "file": str(payload / "Synthetic shared directory"), "recursive": 1,
         "props": {"persistent": "false", "tags": "101,102"}},
    ]}))
    # Original-format saved classic headers; data is generated, never imported.
    # Complete pane deliberately exercises the user-default fallback. Name keeps
    # the preview's responsive Stretch width; the stored width is left unapplied.
    def classic_table(state_width):
        return {"Column.name": [1, 0, 420, 0, 1],
                "Column.status": [1, 1, state_width, 0, 1],
                "Column.example-plugin": [1, 2, 200, 0, 1, {"opaque": b"\x00\xff"}],
                "SortColumns": ["name"], "preview.opaque": b"\x00\xff"}
    (profile / "tables.config").write_bytes(encode({
        "Table.MyTorrents": classic_table(124),
        "TableDefault.MySeeders": classic_table(132),
        "Table.UnknownPlugin": {"opaque": [b"\x00\xff", 7]},
    }))
    # Generated original-format saved SpeedManager example, never live pings.
    # The private ASN and map are display fixtures; no service is enabled.
    (profile / "biglybt.config").write_bytes(encode({
        "ASN Details": [{"as": "64512", "name": "Offline saved example",
                         "bgp": "192.0.2.0/24", "v": 4}],
        "Auto Upload Speed Version": 2, "Auto Upload Speed Enabled": 0,
        "Auto Upload Speed Seeding Enabled": 0,
        "preview.opaque": b"\x00\xff",
    }))
    network = profile / "net"
    network.mkdir(mode=0o700)
    def saved_estimate(speed):
        return {"s": speed, "m": "1.0", "t": "0.0", "h": 8, "w": 1700000000000}
    (network / "pm_64512.dat").write_bytes(encode({
        "upcap": saved_estimate(65536), "downcap": saved_estimate(262144),
        "pings": [{"x": x, "y": 4*x + 2*i, "m": metric}
                  for i, (x, metric) in enumerate(zip(
                      [16, 32, 48, 64, 80, 96, 80, 64],
                      [20, 35, 60, 90, 145, 220, 60, 30]))],
        "preview.opaque": b"\x00\xff",
    }))
    (profile / "biglybt.config").chmod(0o600)
    (network / "pm_64512.dat").chmod(0o600)
    # Generated last-saved original overall statistics; no live client is read.
    # Combined totals already include their data/protocol splits. DHT is separate.
    (profile / "BiglyBT.statistics").write_bytes(encode({"all": {
        "downloaded": 5368709120, "uploaded": 2147483648, "uptime": 270061,
        "mark_time": 1729987200000, "mark_downloaded": 1073741824,
        "mark_uploaded": 536870912, "mark_uptime": 86400,
        "dht_down": 8388608, "dht_up": 4194304,
        "p_uploaded": 1048576, "d_uploaded": 2146435072,
        "p_downloaded": 2097152, "d_downloaded": 5366611968},
        "preview.opaque": b"\x00\xff"}))
    # Generated original-format saved UTC daily records. These are sample saved
    # observations, independent of the empty live Activity/engine graphs.
    history_endings = {datetime.date(2024, 1, 2), datetime.datetime.now(datetime.timezone.utc).date()}
    for date in history_endings:
        start = int(datetime.datetime.combine(date, datetime.time(), datetime.timezone.utc).timestamp()) // 60
        records = []
        for hour, factor in [(1, 1), (6, 3), (12, 2), (18, 5)]:
            anchor = start + hour * 60
            records += [f"s,1,{anchor},100000,200000,300000,400000,500000,600000",
                        f"{factor*1024},{factor*65536},{factor*2048},{factor*1048576},{factor*128},{factor*256}",
                        f"e,1,{anchor+2},{factor*512},{factor*32768},{factor*1024},{factor*524288},{factor*64},{factor*128}"]
        # An observed zero hour is different from the untouched unknown hours.
        records += [f"s,1,{start+120},0,0,0,0,0,0", "0,0,0,0,0,0"]
        target = profile / "stats" / f"{date.year:04d}" / f"{date.month:02d}" / f"{date.day:02d}.dat"
        target.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
        target.write_text("\n".join(records) + "\n", encoding="ascii")
    # Seven independent daily files for the overview. Keep each existing ending
    # day's generated bytes unchanged, including the fixed Daily example.
    for ending in history_endings:
        for offset in range(1, 7):
            date = ending - datetime.timedelta(days=offset)
            if offset == 4:
                continue  # A missing source is never an inferred zero.
            target = profile / "stats" / f"{date.year:04d}" / f"{date.month:02d}" / f"{date.day:02d}.dat"
            target.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
            start = int(datetime.datetime.combine(date, datetime.time(), datetime.timezone.utc).timestamp()) // 60
            if offset == 2:
                data = ""  # Present file with no observations.
            else:
                factor = 0 if offset == 1 else (-1 if offset == 3 else offset)
                data = f"s,1,{start+540},100,200,300,400,500,600\n{factor*128},{factor*1024},{factor*64},{factor*2048},{factor*16},{factor*32}\n"
            target.write_text(data, encoding="ascii")
    return profile


# Qt 6.12's setFocusObject() calls disableSurface() for non-text widgets even
# when no text surface is focused. Its v3 backend then warns and returns without
# changing protocol state. Match only that redundant no-focus diagnostic:
# https://github.com/qt/qtbase/blob/v6.12.0/src/plugins/platforms/wayland/qwaylandinputcontext.cpp
# https://github.com/qt/qtbase/blob/v6.12.0/src/plugins/platforms/wayland/qwaylandtextinputv3.cpp
_IDLE_WAYLAND_DISABLE = re.compile(
    rb"qt\.qpa\.wayland\.textinput: virtual void "
    rb"QtWaylandClient::QWaylandTextInputv3::disableSurface\(wl_surface\*\) "
    rb"Trying to disable 0x[0-9a-fA-F]+ but 0x0 is focused\r?\n"
)


def run_preview(command, environment):
    """Keep the offline preview console usable without changing text input."""
    if (environment.get("BIGLYBTC_WAYLAND_DIAGNOSTICS") == "1"
            or any(name in environment for name in (
                "QT_FATAL_WARNINGS", "QT_MESSAGE_PATTERN", "QT_LOGGING_RULES", "QT_LOGGING_CONF"))):
        return subprocess.run(command, env=environment, check=False).returncode
    with subprocess.Popen(command, env=environment, stderr=subprocess.PIPE) as child:
        for line in child.stderr:
            if _IDLE_WAYLAND_DISABLE.fullmatch(line) is None:
                sys.stderr.buffer.write(line)
                sys.stderr.buffer.flush()
        return child.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare", type=Path, help="Create sample data in a new /tmp/biglybtc-* directory and exit")
    parser.add_argument("--country-db", type=Path, help="Optional private directory containing GeoLite2-Country.mmdb")
    args = parser.parse_args()
    if args.country_db is not None and not args.country_db.is_absolute():
        parser.error("--country-db requires an absolute data directory")
    if args.prepare:
        try:
            validate_root(args.prepare)
        except ValueError as error:
            parser.error(str(error))
        args.prepare.mkdir(mode=0o700)
        print(prepare(args.prepare))
        return 0
    prefix = Path(__file__).resolve().parent.parent
    executable = prefix / "lib/biglybtc/bin/BiglyBTC"
    if not executable.is_file():
        executable = prefix / "bin/BiglyBTC"
    if not executable.is_file():
        parser.error("The BiglyBTC executable is missing; reinstall the preview package")
    with tempfile.TemporaryDirectory(prefix="biglybtc-preview-", dir="/tmp") as temporary:
        root = Path(temporary)
        profile = prepare(root)
        environment = os.environ.copy()
        for name in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML2_IMPORT_PATH"):
            environment.pop(name, None)
        # Keep preview settings/cache separate from other applications too.
        for variable in ("XDG_CONFIG_HOME", "XDG_CACHE_HOME"):
            directory = root / variable.lower()
            directory.mkdir(mode=0o700)
            environment[variable] = str(directory)
        print("BiglyBTC offline preview — generated sample data:", profile, flush=True)
        command = [str(executable), "--experimental-local", str(profile), "0"]
        if args.country_db is not None:
            command += ["--country-db", str(args.country_db)]
        return run_preview(command, environment)


if __name__ == "__main__":
    raise SystemExit(main())
