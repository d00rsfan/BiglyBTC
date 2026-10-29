#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Explicit authenticated fixture launcher; never retries with another mode."""
import argparse
import ctypes
import errno
import fcntl
import hashlib
import json
import os
import pathlib
import selectors
import signal
import stat
import struct
import subprocess
import sys
import time


SEALS = fcntl.F_SEAL_WRITE | fcntl.F_SEAL_GROW | fcntl.F_SEAL_SHRINK | fcntl.F_SEAL_SEAL
ENV_KEYS = ("BIGLYBTC_PUBLIC_ADMIN_MODE", "BIGLYBTC_PUBLIC_ADMIN_HELPER",
            "BIGLYBTC_PUBLIC_ADMIN_SHA256", "BIGLYBTC_PUBLIC_ADMIN_RECEIPT")


def checked(condition, reason):
    if not condition:
        raise RuntimeError(reason)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def sealed_bytes(name, data, executable=False):
    fd = os.memfd_create(name, os.MFD_CLOEXEC | os.MFD_ALLOW_SEALING)
    try:
        at = 0
        while at < len(data):
            at += os.write(fd, data[at:])
        os.fchmod(fd, 0o500 if executable else 0o400)
        fcntl.fcntl(fd, fcntl.F_ADD_SEALS, SEALS)
        checked(fcntl.fcntl(fd, fcntl.F_GET_SEALS) & SEALS == SEALS, "Memfd seals missing")
        return fd
    except BaseException:
        os.close(fd)
        raise


def helper_bytes(path, expected):
    checked(len(expected) == 64 and all(c in "0123456789abcdef" for c in expected), "Exact helper SHA256 required")
    checked(path.is_absolute(), "Absolute helper required")
    fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
    try:
        before = os.fstat(fd)
        checked(stat.S_ISREG(before.st_mode) and before.st_uid == os.getuid()
                and before.st_gid == os.getgid() and not before.st_mode & 0o6000
                and 0 < before.st_size < 2097152, "Ordinary caller-owned bounded helper required")
        try:
            os.getxattr(fd, "security.capability")
        except OSError as error:
            checked(error.errno in (errno.ENODATA, errno.ENOTSUP), "Cannot inspect helper capabilities")
        else:
            raise RuntimeError("File-capability helper refused")
        data = b""
        while len(data) <= before.st_size:
            block = os.read(fd, min(65536, before.st_size + 1 - len(data)))
            if not block:
                break
            data += block
        after = os.fstat(fd)
        stable = lambda value: (value.st_dev, value.st_ino, value.st_mode, value.st_uid, value.st_gid, value.st_size, value.st_mtime_ns, value.st_ctime_ns, value.st_nlink)
        checked(stable(after) == stable(before) and len(data) == before.st_size
                and data.startswith(b"\x7fELF") and digest(data) == expected,
                "Helper image changed or does not match approved SHA256")
        return data
    finally:
        os.close(fd)


def payload(argv, environment):
    values = [os.fsencode(value) for value in argv]
    entries = [os.fsencode(key) + b"=" + os.fsencode(value) for key, value in sorted(environment.items())]
    checked(0 < len(values) <= 256 and len(entries) <= 256, "Payload count bound")
    checked(all(0 < len(value) <= 16384 and b"\0" not in value for value in values + entries), "Payload string bound")
    result = b"BBPUB1\0\0" + struct.pack("!II", len(values), len(entries))
    for value in values + entries:
        result += struct.pack("!I", len(value)) + value
    checked(len(result) < 131072, "Payload byte bound")
    return result


def launch(mode, helper, expected, receipt, argv):
    checked(os.getuid() != 0 and os.getresuid() == (os.getuid(),) * 3
            and os.getresgid() == (os.getgid(),) * 3, "Actual ordinary caller required")
    checked(argv and pathlib.Path(argv[0]).is_absolute(), "Explicit absolute user ELF required")
    argv = [str(pathlib.Path(argv[0]).resolve(strict=True)), *argv[1:]]
    checked(receipt.is_absolute() and not receipt.exists(), "Fresh absolute launch receipt required")
    receipt_fd = os.open(receipt, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW, 0o600)
    image_fd = data_fd = -1
    process = None
    old_handlers = {}
    outcome = {"schema": 1, "status": "FAIL_BEFORE_AUTHORIZATION", "mode": mode,
               "caller_uid": os.getuid(), "caller_gid": os.getgid(),
               "helper": {"path": str(helper), "sha256": expected},
               "invoker": {"path": str(pathlib.Path(__file__).resolve()),
                           "sha256": digest(pathlib.Path(__file__).read_bytes())},
               "launch_environment": {key: os.environ[key] for key in ENV_KEYS if key in os.environ},
               "argv": argv, "no_fallback": True, "subject_exit": None}
    try:
        # Read-only caller metadata before authentication; no policy changes.
        libc = ctypes.CDLL(None, use_errno=True)
        libc.prctl.argtypes = [ctypes.c_int, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong]
        libc.prctl.restype = ctypes.c_int
        dumpable = libc.prctl(3, 0, 0, 0, 0)  # PR_GET_DUMPABLE
        checked(dumpable >= 0, "Cannot observe invoker dumpability")
        outcome["invoker_metadata"] = {"dumpable": dumpable, "uid": list(os.getresuid()),
                                       "gid": list(os.getresgid()),
                                       "namespaces": {name: {"device": os.stat("/proc/self/ns/" + name).st_dev,
                                                             "inode": os.stat("/proc/self/ns/" + name).st_ino,
                                                             "link": os.readlink("/proc/self/ns/" + name)}
                                                      for name in ("user", "net")}}
        image = helper_bytes(helper, expected)
        image_fd = sealed_bytes("biglybtc-public-admin", image, True)
        data_fd = sealed_bytes("biglybtc-public-user-payload", payload(argv, os.environ.copy()))
        outcome["helper"]["bytes"] = len(image)
        outcome["memfd_seals"] = SEALS
        invoker = os.getpid()
        start = pathlib.Path("/proc/self/stat").read_text().rsplit(")", 1)[1].split()[19]
        image_path = f"/proc/{invoker}/fd/{image_fd}"
        prefix = ["/usr/bin/pkexec", "--disable-internal-agent"] if mode == "pkexec" else ["/usr/bin/sudo", "-n"]
        checked(mode in ("pkexec", "sudo"), "Explicit pkexec or sudo mode required")
        command = [*prefix, image_path, mode, str(invoker), str(image_fd), str(data_fd), start, "fixed-public-fixture-v1"]
        outcome["command"] = command
        # User loader/Qt/environment data only lives in sealed data until after
        # the helper's verified UID/GID/capability/NNP drop. No env-preserve flag.
        # Fixed stdin survives auth fd cleanup without any foreign proc access.
        # The privileged helper never reads it; only its verified-drop child does.
        os.lseek(data_fd, 0, os.SEEK_SET)
        outcome["payload_transport"] = "sealed-fixed-stdin-after-verified-drop"
        process = subprocess.Popen(command, stdin=data_fd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   env={"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LANG": "C", "LC_ALL": "C"}, close_fds=True)
        stopped = []
        for number in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
            old_handlers[number] = signal.signal(number, lambda signum, frame: stopped.append(signum))
        selector = selectors.DefaultSelector()
        selector.register(process.stdout, selectors.EVENT_READ, 0)
        selector.register(process.stderr, selectors.EVENT_READ, 1)
        counts, tails = [0, 0], [b"", b""]
        begun, stopping, proof = time.monotonic(), None, False
        while selector.get_map() or process.poll() is None:
            now = time.monotonic()
            if stopped or now - begun > 270 or any(count > 8388608 for count in counts):
                if stopping is None:
                    stopping = now
                    outcome["status"] = "CANCELLED_OR_BOUNDED_LAUNCH_EXPIRED"
                    try:
                        process.terminate()
                    except ProcessLookupError:
                        pass
                    except PermissionError:
                        # The fixed privileged setup lasts <=5s. On invoker exit,
                        # its pidfd causes the helper to kill/reap only its child.
                        pass
                if now - stopping > 7:
                    try:
                        process.kill()
                    except (ProcessLookupError, PermissionError):
                        pass
                    raise RuntimeError("Bounded cancellation; invoker exit revokes helper child")
            for key, _ in selector.select(0.1):
                block = os.read(key.fileobj.fileno(), 65536)
                which = key.data
                if not block:
                    selector.unregister(key.fileobj)
                    key.fileobj.close()
                    continue
                counts[which] += len(block)
                combined = tails[which] + block
                proof |= which == 0 and f"BBPUB_DROP uid={os.getuid()} gid={os.getgid()} groups=0 capabilities=0 bounding=0 ambient=0 nnp=1\n".encode() in combined
                tails[which] = combined[-65536:]
                stream = sys.stdout.buffer if which == 0 else sys.stderr.buffer
                stream.write(block)
                stream.flush()
        code = process.wait(timeout=1)
        outcome.update(subject_exit=code, elapsed_milliseconds=round((time.monotonic() - begun) * 1000),
                       output_bytes=counts, verified_drop_marker=proof)
        if stopping is None:
            outcome["status"] = "PASS_AUTHENTICATED_NAMESPACE_SUBJECT" if code == 0 and proof else (
                "BLOCKED_AUTH_OR_NAMESPACE_SETUP" if not proof else "FAIL_NAMESPACE_SUBJECT")
        return 0 if outcome["status"] == "PASS_AUTHENTICATED_NAMESPACE_SUBJECT" else (code or 1)
    except BaseException as error:
        outcome["error"] = str(error)
        if process is not None and process.poll() is None:
            try:
                process.terminate()
            except (ProcessLookupError, PermissionError):
                pass
        raise
    finally:
        for number, handler in old_handlers.items():
            signal.signal(number, handler)
        for fd in (data_fd, image_fd):
            if fd >= 0:
                os.close(fd)
        encoded = (json.dumps(outcome, indent=2, sort_keys=True) + "\n").encode()
        at = 0
        while at < len(encoded):
            at += os.write(receipt_fd, encoded[at:])
        os.close(receipt_fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("pkexec", "sudo"), required=True)
    parser.add_argument("--helper", type=pathlib.Path, required=True)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--receipt", type=pathlib.Path, required=True)
    parser.add_argument("argv", nargs=argparse.REMAINDER)
    options = parser.parse_args()
    argv = options.argv[1:] if options.argv and options.argv[0] == "--" else options.argv
    return launch(options.mode, options.helper, options.sha256, options.receipt, argv)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        print(f"FAIL explicit Public namespace launcher: {error}", file=sys.stderr)
        sys.exit(1)
