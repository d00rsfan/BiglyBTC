#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Owned namespace fixture only. Root must review/schedule; never falls back."""
import os
import pathlib
import shutil
import socket
import subprocess
import sys


def identity(path):
    value = os.stat(path)
    return value.st_dev, value.st_ino


def main():
    if len(sys.argv) >= 5 and sys.argv[1] == "--inside":
        net_fd, user_fd = int(sys.argv[2]), int(sys.argv[3])
        driver, arguments = sys.argv[4], sys.argv[5:]
        if (os.geteuid() != 0 or not os.readlink(f"/proc/self/fd/{net_fd}").startswith("net:[")
                or not os.readlink(f"/proc/self/fd/{user_fd}").startswith("user:[") or identity(f"/proc/self/fd/{net_fd}") == identity("/proc/self/ns/net")
                or identity(f"/proc/self/fd/{user_fd}") == identity("/proc/self/ns/user")
                or {name for _, name in socket.if_nameindex()} != {"lo"}):
            raise RuntimeError("PublicBound requires a distinct owned user/network namespace containing only lo")
        ip = shutil.which("ip")
        if not ip:
            raise RuntimeError("Owned namespace fixture requires iproute2; no fallback")
        for family in ("-4", "-6"):
            if subprocess.check_output([ip, family, "route", "show", "default"]).strip():
                raise RuntimeError("Owned namespace must have no default route")
        # Only after both namespace identities and interface/route fences pass.
        subprocess.run([ip, "link", "set", "dev", "lo", "up"], check=True)
        for address in ("203.0.113.1/32", "203.0.113.2/32", "203.0.113.3/32"):
            subprocess.run([ip, "-4", "address", "add", address, "dev", "lo"], check=True)
        for address in ("2001:db8::1/128", "2001:db8::2/128", "2001:db8::3/128"):
            subprocess.run([ip, "-6", "address", "add", address, "dev", "lo", "nodad"], check=True)
        environment = os.environ.copy()
        environment["BIGLYBTC_OWNED_PUBLIC_NET_FD"] = str(net_fd)
        environment["BIGLYBTC_OWNED_PUBLIC_USER_FD"] = str(user_fd)
        print("Owned distinct user/net namespace, lo only, no default route; documentation-prefix addresses only", flush=True)
        return subprocess.run([driver, *arguments], env=environment, stdin=subprocess.DEVNULL,
                              pass_fds=(net_fd, user_fd)).returncode
    if len(sys.argv) < 2:
        raise RuntimeError("Expected the explicitly supplied owned test ELF")
    driver = str(pathlib.Path(sys.argv[1]).resolve(strict=True))
    net_fd = os.open("/proc/self/ns/net", os.O_RDONLY | os.O_CLOEXEC)
    user_fd = os.open("/proc/self/ns/user", os.O_RDONLY | os.O_CLOEXEC)
    try:
        unshare = shutil.which("unshare")
        if not unshare:
            raise RuntimeError("Owned namespace fixture requires unshare; no fallback")
        command = [unshare, "--user", "--map-root-user", "--net", "--", sys.executable,
                   str(pathlib.Path(__file__).resolve()), "--inside", str(net_fd), str(user_fd),
                   driver, *sys.argv[2:]]
        return subprocess.run(command, stdin=subprocess.DEVNULL, pass_fds=(net_fd, user_fd)).returncode
    finally:
        os.close(user_fd)
        os.close(net_fd)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"FAIL owned PublicBound namespace: {error}", file=sys.stderr)
        sys.exit(1)
