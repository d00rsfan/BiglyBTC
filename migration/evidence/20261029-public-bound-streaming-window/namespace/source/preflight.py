#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Read-only environment acceptance. Does not run any product test."""
import argparse
import ctypes
import errno
import fcntl
import json
import os
import pathlib
import socket
import subprocess


def require(value, why):
    if not value:
        raise RuntimeError(why)


def identity(fd):
    value = os.fstat(fd)
    return [value.st_dev, value.st_ino]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--uid", type=int, required=True)
    parser.add_argument("--gid", type=int, required=True)
    options = parser.parse_args()
    require(options.uid > 0 and os.getresuid() == (options.uid,) * 3
            and os.getresgid() == (options.gid,) * 3 and os.getgroups() == [], "Caller IDs/groups differ")
    fields = dict(line.split(":", 1) for line in pathlib.Path("/proc/self/status").read_text().splitlines() if ":" in line)
    capabilities = {name: int(fields[name], 16) for name in ("CapInh", "CapPrm", "CapEff", "CapBnd", "CapAmb")}
    require(not any(capabilities.values()) and int(fields["NoNewPrivs"]) == 1, "Capabilities/NNP differ")
    libc = ctypes.CDLL(None, use_errno=True)
    libc.prctl.argtypes = [ctypes.c_int, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong]
    libc.prctl.restype = ctypes.c_int
    cap_count = 0
    while cap_count < 1024:
        ctypes.set_errno(0)
        result = libc.prctl(23, cap_count, 0, 0, 0)  # PR_CAPBSET_READ
        if result == -1 and ctypes.get_errno() == errno.EINVAL:
            break
        require(result == 0 and libc.prctl(47, 1, cap_count, 0, 0) == 0, "Runtime bounding/ambient set differs")
        cap_count += 1
    securebits = libc.prctl(27, 0, 0, 0, 0)
    require(0 < cap_count < 1024 and securebits >= 0 and securebits & 3 == 3, "Runtime caps/NOROOT lock missing")
    parent_net = int(os.environ["BIGLYBTC_OWNED_PUBLIC_NET_FD"])
    parent_user = int(os.environ["BIGLYBTC_OWNED_PUBLIC_USER_FD"])
    current_net = os.open("/proc/self/ns/net", os.O_RDONLY | os.O_CLOEXEC)
    current_user = os.open("/proc/self/ns/user", os.O_RDONLY | os.O_CLOEXEC)
    owner = -1
    try:
        require(fcntl.ioctl(parent_net, 0xB703) == 0x40000000
                and fcntl.ioctl(parent_user, 0xB703) == 0x10000000, "Inherited namespace FD types differ")
        require(identity(parent_net) != identity(current_net) and identity(parent_user) != identity(current_user),
                "Parent namespace not distinct")
        owner = fcntl.ioctl(current_net, 0xB701)  # NS_GET_USERNS
        require(identity(owner) == identity(current_user), "Network is owned by a different user namespace")
        require({name for _, name in socket.if_nameindex()} == {"lo"}, "Interface inventory differs")
        ip = "/usr/sbin/ip"
        def read_ip(arguments):
            result = subprocess.run([ip, "-j", *arguments], check=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, timeout=3, env={"PATH": "/usr/sbin:/usr/bin", "LC_ALL": "C"})
            require(len(result.stdout) < 65536 and len(result.stderr) < 65536, "Read-only ip output bound")
            return json.loads(result.stdout)
        addresses = read_ip(["address", "show"])
        require(len(addresses) == 1 and addresses[0]["ifname"] == "lo" and "UP" in addresses[0]["flags"], "Loopback not up")
        actual = []
        for item in addresses[0]["addr_info"]:
            actual.append((item["local"], item["prefixlen"]))
        docs = {(f"203.0.113.{n}", 32) for n in (1, 2, 3)} | {(f"2001:db8::{n}", 128) for n in (1, 2, 3)}
        require(len(actual) == len(set(actual)) and docs <= set(actual)
                and set(actual) <= docs | {("127.0.0.1", 8), ("::1", 128)}, "Exact six documentation/normal lo addresses differ")
        routes = {family: read_ip([family, "route", "show", "table", "all"]) for family in ("-4", "-6")}
        for entries in routes.values():
            for entry in entries:
                require(entry.get("dev", "lo") == "lo" and "gateway" not in entry
                        and (entry.get("dst") != "default" or entry.get("type") in ("unreachable", "prohibit", "blackhole")),
                        "External/usable default route found")
        print(json.dumps({"status": "PASS_OWNED_CALLER_NAMESPACE_PREFLIGHT", "functional_test_credit": False,
                          "uid": list(os.getresuid()), "gid": list(os.getresgid()), "groups": os.getgroups(),
                          "capabilities": capabilities, "runtime_capabilities": cap_count, "NoNewPrivs": 1,
                          "parent_net": identity(parent_net), "parent_user": identity(parent_user),
                          "current_net": identity(current_net), "current_user": identity(current_user),
                          "net_owning_user": identity(owner), "addresses": sorted(actual), "routes": routes}, sort_keys=True))
    finally:
        for fd in (owner, current_user, current_net):
            if fd >= 0:
                os.close(fd)


if __name__ == "__main__":
    main()
