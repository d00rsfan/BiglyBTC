#!/usr/bin/env python3
"""Stage a directly runnable development folder, without creating or installing a DEB."""

import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess


# This is the retained, uninstrumented GCC16 source build, never a causal-test
# provider. A pathname or matching version alone cannot authorize another DSO.
SOURCE_LIBTORRENT_SHA256 = "c35f1b0408508c019f002c438d70dd361a55be20286ded56711efd6bba2424e3"
SOURCE_LIBTORRENT_BYTES = 8737560
SOURCE_LIBTORRENT_COMMIT = "740a0b9aeabe00e762cc0efe4a0f27593db2550b"
SOURCE_BUILD_RECORD_SHA256 = "436931d4c6b87274c60b89877afedc19e06485bc4f04d120a3ab3e1889abfc66"
LIBTORRENT_NAMES = ("libtorrent-rasterbar.so", "libtorrent-rasterbar.so.2.0",
                    "libtorrent-rasterbar.so.2.0.12")


def source_libtorrent(path):
    resolved = path.resolve(strict=True)
    if not resolved.is_file() or resolved.stat().st_size != SOURCE_LIBTORRENT_BYTES:
        raise ValueError("Expected the exact uninstrumented c35f libtorrent source runtime")
    payload = resolved.read_bytes()
    if (len(payload) != SOURCE_LIBTORRENT_BYTES or
            hashlib.sha256(payload).hexdigest() != SOURCE_LIBTORRENT_SHA256):
        raise ValueError("Source libtorrent SHA-256 does not match the approved uninstrumented image")
    return resolved, payload


def install_source_libtorrent(destination, dependency, source, payload):
    private = destination / "usr/lib/biglybtc/lib"
    lookup = dependency / "usr/lib/x86_64-linux-gnu"
    for directory in (private, lookup):
        directory.mkdir(parents=True, exist_ok=True)
        if directory.is_symlink() or not directory.resolve().is_relative_to(destination.resolve()):
            raise ValueError("Runtime directory escapes the fresh distribution: " + str(directory))
        for alias in directory.glob("libtorrent-rasterbar.so*"):
            if alias.name not in LIBTORRENT_NAMES or (alias.is_dir() and not alias.is_symlink()):
                raise ValueError("Unexpected runtime alias in the fresh distribution: " + str(alias))
            alias.unlink()
    canonical = private / LIBTORRENT_NAMES[-1]
    canonical.write_bytes(payload)
    canonical.chmod(0o644)
    for name in LIBTORRENT_NAMES[:-1]:
        (private / name).symlink_to(canonical.name)
    for name in LIBTORRENT_NAMES:
        (lookup / name).symlink_to(os.path.relpath(canonical, lookup))
    aliases = []
    for directory in (private, lookup):
        for name in LIBTORRENT_NAMES:
            alias = directory / name
            if (alias.resolve(strict=True) != canonical or
                    hashlib.sha256(alias.read_bytes()).hexdigest() != SOURCE_LIBTORRENT_SHA256):
                raise ValueError("Runtime alias does not resolve to the exact source image: " + str(alias))
            aliases.append(alias.relative_to(destination).as_posix())
    return {
        "provider": "source-built-uninstrumented", "version": "2.0.12",
        "artifact_sha256": SOURCE_LIBTORRENT_SHA256, "artifact_bytes": SOURCE_LIBTORRENT_BYTES,
        "source_input": str(source), "upstream_commit": SOURCE_LIBTORRENT_COMMIT,
        "source_build_record_sha256": SOURCE_BUILD_RECORD_SHA256,
        "artifact": canonical.relative_to(destination).as_posix(), "aliases": aliases,
        "instrumented": False,
        "scope": "Exact uninstrumented source-built dependency; not full original-client parity",
        "package_role": "Original Debian package metadata and notices retained; its shared objects replaced",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--libtorrent-package", type=Path, required=True)
    parser.add_argument("--source-libtorrent", type=Path,
        help="Optionally bundle only the exact approved uninstrumented GCC16 libtorrent image")
    args = parser.parse_args()
    source = None
    if args.source_libtorrent is not None:
        try:
            source = source_libtorrent(args.source_libtorrent)
        except (OSError, ValueError) as error:
            parser.error(str(error))
    destination = args.output.absolute()
    package = args.libtorrent_package.resolve()
    if destination.exists() or destination.is_symlink():
        parser.error("Use a fresh distribution directory; existing builds are preserved")
    if source is not None:
        destination = destination.resolve()
    identity = subprocess.check_output(["dpkg-deb", "--field", str(package),
        "Package", "Version", "Architecture"], text=True)
    if identity != "Package: libtorrent-rasterbar2.0t64\nVersion: 2.0.12-1\nArchitecture: amd64\n":
        parser.error("Expected the Ubuntu amd64 libtorrent 2.0.12 runtime package")
    destination.mkdir(parents=True)
    environment = os.environ.copy()
    environment["DESTDIR"] = str(destination)
    subprocess.run(["cmake", "--install", str(args.build.resolve()), "--prefix", "/usr", "--strip"],
                   env=environment, check=True, timeout=180)
    dependency = destination / "dependencies/libtorrent"
    dependency.parent.mkdir()
    subprocess.run(["dpkg-deb", "--extract", str(package), str(dependency)], check=True, timeout=60)
    provider = None
    if source is not None:
        provider = install_source_libtorrent(destination, dependency, *source)
    lookup = "dependencies/libtorrent/usr/lib/x86_64-linux-gnu"
    library_path = "str(root / " + repr(lookup) + ")"
    if provider is not None:
        library_path = "str(root / 'usr/lib/biglybtc/lib') + os.pathsep + " + library_path
    launcher = destination / "BiglyBTC-preview"
    launcher.write_text("#!/usr/bin/env python3\nimport os, sys\nfrom pathlib import Path\n"
        "root = Path(__file__).resolve().parent\n"
        f"os.environ['LD_LIBRARY_PATH'] = {library_path}\n"
        "os.execv(str(root / 'usr/bin/BiglyBTC-preview'), ['BiglyBTC-preview', *sys.argv[1:]])\n")
    launcher.chmod(0o755)
    (destination / "README.txt").write_text(
        "BiglyBTC offline desktop preview\n\n"
        "Run ./BiglyBTC-preview from this folder. Qt and the libtorrent runtime\n"
        "are included alongside the application; no system installation is needed.\n"
        "The four torrents are generated examples. Transfers are disabled.\n"
        "Check My Torrents/Ctrl+1, My Tracker/Ctrl+2, My Shares/Ctrl+3, sorting and Files/Ctrl+I.\n"
        "Saved service views are read-only; no tracker or share service is started.\n"
        "Also check Refresh/F5, scaling, auxiliary tab closure and window close.\n"
        "Each launch uses a fresh /tmp/biglybtc-preview-* sample profile.\n"
        "The original ~/.biglybt profile is never opened by this launcher.\n"
        "Full client/plugin migration and original-profile switching remain unfinished.\n")
    if provider is not None:
        with (destination / "README.txt").open("a") as notice:
            notice.write("\nThis folder uses the exact uninstrumented source-built libtorrent 2.0.12.\n"
                         "The extracted Debian package retains its metadata and license notices;\n"
                         "its shared objects resolve to the private source-built image.\n")
    environment["LD_LIBRARY_PATH"] = str(dependency / "usr/lib/x86_64-linux-gnu")
    if provider is not None:
        environment["LD_LIBRARY_PATH"] = (str(destination / "usr/lib/biglybtc/lib") +
                                           os.pathsep + environment["LD_LIBRARY_PATH"])
    linked = subprocess.check_output(["ldd", str(destination / "usr/lib/biglybtc/bin/BiglyBTC")],
                                     env=environment, text=True, timeout=30)
    if "not found" in linked or "/opt/Qt" in linked or "/tmp/" in linked:
        raise ValueError("Distribution retains a missing or build-only dependency: " + linked)
    loaders = {}
    if provider is not None:
        for name in ("BiglyBTC", "BiglyBTC-headless"):
            output = linked if name == "BiglyBTC" else subprocess.check_output(
                ["ldd", str(destination / "usr/lib/biglybtc/bin" / name)],
                env=environment, text=True, timeout=30)
            if "not found" in output or "/opt/Qt" in output or "/tmp/" in output:
                raise ValueError("Distribution retains a missing or build-only dependency: " + output)
            matches = re.findall(r"^\s*libtorrent-rasterbar\.so\.2\.0 => (.+?) \(", output, re.MULTILINE)
            if (len(matches) != 1 or Path(matches[0]).resolve(strict=True) !=
                    destination / provider["artifact"]):
                raise ValueError("Installed executable does not resolve the private source runtime: " + output)
            roots = (destination, Path("/usr/lib").resolve(),
                     Path("/lib").resolve(), Path("/lib64").resolve())
            for loaded in re.findall(r"=> (/.*?) \(", output):
                resolved = Path(loaded).resolve(strict=True)
                if not any(resolved.is_relative_to(root) for root in roots):
                    raise ValueError("Non-system dependency outside the distribution: " + loaded)
            loaders[name] = output
    country_bundle = None
    module = destination / "usr/lib/biglybtc/lib/BiglyBTC-plugin-azlocprov.so"
    approval = args.build / "app/generated/country_approval.hpp"
    if module.exists() or approval.exists():
        compiled = approval.read_text()
        approved_sha = re.search(r'Sha256\[\] = "([0-9a-f]{64})";', compiled)
        approved_bytes = re.search(r'Bytes = ([0-9]+);', compiled)
        version = re.search(r'Version\[\] = "([0-9.]+)";', compiled)
        if not approved_sha or not approved_bytes or not version:
            raise ValueError("Invalid compiled Country module approval")
        payload = module.read_bytes()
        if (len(payload) != int(approved_bytes[1]) or
                hashlib.sha256(payload).hexdigest() != approved_sha[1]):
            raise ValueError("Packaged Country module differs from its compiled approval")
        output = subprocess.check_output(["ldd", str(module)], env=environment, text=True, timeout=30)
        if "not found" in output or "/tmp/" in output or "/opt/Qt" in output:
            raise ValueError("Country module retains a missing or build-only dependency: " + output)
        for soname in ("libmaxminddb.so.0", "libarchive.so.13"):
            resolved = re.findall(r"^\s*" + re.escape(soname) + r" => (.+?) \(", output, re.MULTILINE)
            if len(resolved) != 1 or Path(resolved[0]).resolve() != module.parent / soname:
                raise ValueError("Country module does not use its private dependency: " + soname)
        country_bundle = {"artifact": module.relative_to(destination).as_posix(),
            "sha256": approved_sha[1], "bytes": len(payload), "version": version[1],
            "approval": "compiled application-owned bundle", "database_included": False,
            "loader": output}
    files = {}
    for path in sorted(destination.rglob("*")):
        if path.is_file():
            files[path.relative_to(destination).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = {
        "scope": "Offline generated-sample development preview; full migration remains unfinished",
        "build": str(args.build.resolve()), "launch": "./BiglyBTC-preview",
        "dependency_package_sha256": hashlib.sha256(package.read_bytes()).hexdigest(),
        "files_sha256": files, "loader": linked, "system_installation": False,
    }
    if country_bundle is not None:
        manifest["country_bundle"] = country_bundle
    if provider is not None:
        manifest["libtorrent_runtime"] = provider
        manifest["loaders"] = loaders
    (destination / "BUILD_MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Runnable preview:", launcher)


if __name__ == "__main__":
    main()
