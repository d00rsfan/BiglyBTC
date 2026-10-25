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

# Independent explicit lane; the original c35 selector and all instrumented
# denials below remain unchanged. No version/capability-only authorization.
SELECTED_LIBTORRENT_SHA256 = "d367afd6499859c5a57d66bddd352a977ff76c7fb76b5925302c2c9008c7ddbd"
SELECTED_LIBTORRENT_BYTES = 9072560
SELECTED_RUNTIME_MANIFEST_SHA256 = "e74def09b2257a266786b8ddb116382d300716687bf26bbe57072f5ff90e8737"
SELECTED_BUILD_RECORD_SHA256 = "34bbba203833a31e59f0e9101f232fc2aaebe2bf080bdb8b640a29d68bd4016f"
SELECTED_VERIFIERS = {
    "verify_runtime_manifest.py": "36146305c9df31bba21a8daff00151a0bd1570d48e366af080b06c61ab55b50c",
    "verify_provider_runtime.py": "fc9ae8203bb6f32f94d57a05c5ea174944bdf1dbbe41ae51d9caf987ac01d89a",
}


def source_libtorrent(path):
    resolved = path.resolve(strict=True)
    if not resolved.is_file() or resolved.stat().st_size != SOURCE_LIBTORRENT_BYTES:
        raise ValueError("Expected the exact uninstrumented c35f libtorrent source runtime")
    payload = resolved.read_bytes()
    if (len(payload) != SOURCE_LIBTORRENT_BYTES or
            hashlib.sha256(payload).hexdigest() != SOURCE_LIBTORRENT_SHA256):
        raise ValueError("Source libtorrent SHA-256 does not match the approved uninstrumented image")
    return resolved, payload


def selected_source_libtorrent(path, manifest_path, build):
    """Preflight only. No output, loader, subprocess, DSO or owner invocation."""
    import importlib.util
    import sys
    sys.dont_write_bytecode = True

    def pinned_file(candidate, pin):
        if (not candidate.is_absolute() or candidate.is_symlink() or not candidate.is_file()
                or candidate.resolve(strict=True) != candidate
                or hashlib.sha256(candidate.read_bytes()).hexdigest() != pin):
            raise ValueError("Changed/unapproved selected-provider input: " + str(candidate))

    manifest_path = manifest_path.absolute()
    pinned_file(manifest_path, SELECTED_RUNTIME_MANIFEST_SHA256)
    record_path = manifest_path.parent / "provider-source-build-record.json"
    pinned_file(record_path, SELECTED_BUILD_RECORD_SHA256)
    manifest = json.loads(manifest_path.read_bytes())
    source = path.resolve(strict=True)
    if (str(source) != manifest["artifact"] or manifest["artifact_sha256"] != SELECTED_LIBTORRENT_SHA256
            or manifest["artifact_bytes"] != SELECTED_LIBTORRENT_BYTES or source.is_symlink()
            or not source.is_file() or source.stat().st_size != SELECTED_LIBTORRENT_BYTES):
        raise ValueError("Expected the exact reviewed selected-swarm provider")
    payload = source.read_bytes()
    if hashlib.sha256(payload).hexdigest() != SELECTED_LIBTORRENT_SHA256:
        raise ValueError("Selected provider full artifact SHA-256 differs")
    binding = Path(__file__).resolve().parents[2] / "native/engine/runtime-binding"
    for name, pin in SELECTED_VERIFIERS.items():
        pinned_file(binding / name, pin)
    spec = importlib.util.spec_from_file_location("biglybtc_portable_source_approval", binding / "verify_runtime_manifest.py")
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    admitted, pin = verifier.inspect(manifest_path, source, Path(manifest["install_root"]) / "include")
    if pin != SELECTED_RUNTIME_MANIFEST_SHA256 or admitted != manifest:
        raise ValueError("Selected provider failed complete source/build/SDK admission")
    generated = build.resolve(strict=True) / "engine/runtime-binding"
    receipt = json.loads((generated / "configure-receipt.json").read_bytes())
    expected = {"manifest_sha256": SELECTED_RUNTIME_MANIFEST_SHA256,
        "artifact_sha256": SELECTED_LIBTORRENT_SHA256, "artifact": str(source),
        "source_files": 449, "full_source_files": 1072, "build_rules": 13,
        "bundled_inputs": 16, "installed_files": 284, "build_files": 4,
        "causal_qualified": False, "purpose": "source-bound-qualification-only",
        "provider_counter_and_swarm_candidate": True}
    if receipt != expected:
        raise ValueError("Application was not configured against this exact provider/SDK")
    header = (generated / "persistent_runtime_config.hpp").read_text()
    required = ("#define BIGLYBTC_PERSISTENT_RUNTIME_BOUND 1",
        'inline constexpr char artifact[] = ' + json.dumps(str(source)) + ';',
        'inline constexpr char sha256[] = "' + SELECTED_LIBTORRENT_SHA256 + '";',
        'inline constexpr char manifest[] = "' + SELECTED_RUNTIME_MANIFEST_SHA256 + '";',
        "inline constexpr unsigned long long bytes = 9072560ULL;")
    if any(text not in header for text in required):
        raise ValueError("Application generated binding does not select this exact provider")
    return source, payload


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


def install_selected_libtorrent(destination, dependency, source, payload):
    # Called only after complete admission above, before any loader check.
    # Keep the original install_source_libtorrent implementation byte-exact.
    if len(payload) != SELECTED_LIBTORRENT_BYTES or hashlib.sha256(payload).hexdigest() != SELECTED_LIBTORRENT_SHA256:
        raise ValueError("Changed selected-provider bytes before private installation")
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
            if alias.resolve(strict=True) != canonical or hashlib.sha256(alias.read_bytes()).hexdigest() != SELECTED_LIBTORRENT_SHA256:
                raise ValueError("Runtime alias does not resolve to the exact selected provider: " + str(alias))
            aliases.append(alias.relative_to(destination).as_posix())
    return {"provider": "source-built-selected-swarm-counter-v1", "version": "2.0.12",
        "artifact_sha256": SELECTED_LIBTORRENT_SHA256, "artifact_bytes": SELECTED_LIBTORRENT_BYTES,
        "source_input": str(source), "upstream_commit": SOURCE_LIBTORRENT_COMMIT,
        "source_build_record_sha256": SELECTED_BUILD_RECORD_SHA256,
        "runtime_manifest_sha256": SELECTED_RUNTIME_MANIFEST_SHA256,
        "artifact": canonical.relative_to(destination).as_posix(), "aliases": aliases,
        "instrumented": False, "causal_qualified": False,
        "scope": "Exact source-built selected-swarm/counter dependency; source binding alone is not runtime or full client parity",
        "package_role": "Original Debian package metadata and notices retained; its shared objects replaced"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--libtorrent-package", type=Path, required=True)
    parser.add_argument("--source-libtorrent", type=Path,
        help="Optionally bundle only the exact approved uninstrumented GCC16 libtorrent image")
    parser.add_argument("--source-provider-manifest", type=Path,
        help="Explicit additional exact selected-swarm/counter provider manifest; no instrumented or version fallback")
    args = parser.parse_args()
    if args.source_provider_manifest is not None and args.source_libtorrent is None:
        parser.error("--source-provider-manifest also requires --source-libtorrent")
    source = None
    if args.source_libtorrent is not None:
        try:
            source = (source_libtorrent(args.source_libtorrent) if args.source_provider_manifest is None
                      else selected_source_libtorrent(args.source_libtorrent, args.source_provider_manifest, args.build))
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
        provider = (install_source_libtorrent(destination, dependency, *source) if args.source_provider_manifest is None
                    else install_selected_libtorrent(destination, dependency, *source))
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
    # Normal product entry point is relocatable too. No profile, listen port or
    # service policy is selected here; main requires explicit --profile input.
    normal_launcher = destination / "BiglyBTC"
    normal_launcher.write_text(
        '#!/usr/bin/env python3\n'
        'import os, sys\n'
        'from pathlib import Path\n'
        'root = Path(__file__).resolve().parent\n'
        "private = root / 'usr/lib/biglybtc'\n"
        "lookup = root / 'dependencies/libtorrent/usr/lib/x86_64-linux-gnu'\n"
        "os.environ['LD_LIBRARY_PATH'] = os.pathsep.join(map(str, [private / 'lib', private / 'qt/lib', lookup]))\n"
        "os.environ['QT_PLUGIN_PATH'] = str(private / 'qt/plugins')\n"
        "os.environ['QT_QPA_PLATFORM_PLUGIN_PATH'] = str(private / 'qt/plugins/platforms')\n"
        "os.execv(str(private / 'bin/BiglyBTC'), ['BiglyBTC', *sys.argv[1:]])\n"
    )
    normal_launcher.chmod(0o755)
    (destination / "README.txt").write_text(
        "BiglyBTC portable development client\n\n"
        "Normal client: ./BiglyBTC --profile /absolute/path/to/profile\n"
        "Pass an existing absolute profile directory without symbolic links.\n"
        "For development, use a separate owned directory such as ~/.biglybtc-dev;\n"
        "expand it to its absolute path when passing --profile. The launcher\n"
        "does not create a profile, choose ports or copy the Java profile.\n"
        "The original ~/.biglybt directory is never chosen automatically.\n\n"
        "Offline generated-sample preview:\n"
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
        "scope": "Portable development client; explicit profile or generated offline preview; full migration remains unfinished",
        "build": str(args.build.resolve()), "launch": "./BiglyBTC-preview",
        "normal_launch": "./BiglyBTC --profile ABSOLUTE_PROFILE",
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
