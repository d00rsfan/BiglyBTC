#!/usr/bin/env python3
"""Root-run environment check only: no JVM, router, native test or fallback."""
import argparse
import errno
import fcntl
import hashlib
import json
import os
import pathlib
import re
import signal
import socket
import struct
import subprocess
import sys

H = pathlib.Path(__file__).resolve().parent
BWRAP = pathlib.Path('/usr/bin/bwrap')
BWRAP_SHA = '0abea81db798ebf6b4742ac0664802d97521547a353c2a0dbdc21d76cbbfd2c0'
NAMES = {'user': 0x10000000, 'net': 0x40000000, 'pid': 0x20000000,
         'ipc': 0x08000000, 'uts': 0x04000000}


def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def write(path, value):
    pathlib.Path(path).write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def identity(fd):
    value = os.fstat(fd)
    return {'device': value.st_dev, 'inode': value.st_ino}


def validate_parent(parent):
    for name, kind in NAMES.items():
        row = parent['namespaces'][name]
        assert identity(row['fd']) == row['identity'], 'Held parent namespace changed: ' + name
        assert fcntl.ioctl(row['fd'], 0xb703) == kind, 'Held parent namespace type changed'


def fence(parent):
    namespaces = {}
    for name, kind in NAMES.items():
        row = parent['namespaces'][name]
        own = os.open('/proc/self/ns/' + name, os.O_RDONLY | os.O_CLOEXEC)
        try:
            actual = identity(own)
            assert fcntl.ioctl(own, 0xb703) == kind
            assert actual != row['identity'], 'Namespace not isolated: ' + name
            namespaces[name] = {'parent': row['identity'], 'actual': actual,
                                'link': os.readlink('/proc/self/ns/' + name)}
        finally:
            os.close(own)
    status = pathlib.Path('/proc/self/status').read_text()
    capabilities = {}
    for name in ('CapInh', 'CapPrm', 'CapEff', 'CapBnd', 'CapAmb'):
        match = re.search(r'^' + name + r':\s*([0-9a-fA-F]+)$', status, re.M)
        assert match and int(match[1], 16) == 0, 'Nonzero capability set: ' + name
        capabilities[name] = 0
    assert re.search(r'^NoNewPrivs:\s*1$', status, re.M), 'NoNewPrivs absent'
    assert [name for _, name in socket.if_nameindex()] == ['lo'], 'External interface'
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as descriptor:
        request = struct.pack('256s', b'lo')
        flags = struct.unpack_from('H', fcntl.ioctl(descriptor, 0x8913, request), 16)[0]
        assert flags & 1 and flags & 8, 'Loopback is not UP/LOOPBACK'
        ipv4 = socket.inet_ntoa(fcntl.ioctl(descriptor, 0x8915, request)[20:24])
        assert ipv4 == '127.0.0.1', 'Unexpected loopback IPv4 address'
    ipv6 = []
    for line in pathlib.Path('/proc/net/if_inet6').read_text().splitlines():
        fields = line.split()
        assert len(fields) == 6 and fields[-1] == 'lo'
        assert fields[0] == '00000000000000000000000000000001' and fields[2] == '80'
        ipv6.append('::1/128')
    assert len(ipv6) <= 1, 'Duplicate loopback IPv6 address'
    routes = {}
    for version, leaf in ((4, 'route'), (6, 'ipv6_route')):
        rows = pathlib.Path('/proc/net/' + leaf).read_text().splitlines()
        assert len(rows) <= 64, 'Route inventory bound'
        if version == 4:
            rows = rows[1:]
        for line in rows:
            fields = line.split()
            if version == 4:
                assert len(fields) == 11 and fields[0] == 'lo', 'External IPv4 route'
                default, flags = fields[1] == '00000000', int(fields[3], 16)
            else:
                assert len(fields) == 10 and fields[-1] == 'lo', 'External IPv6 route'
                default = fields[0] == '0' * 32 and fields[1] == '00'
                flags = int(fields[8], 16)
            # Permit only the kernel's unusable REJECT, !UP default sentinel.
            assert not default or flags & 0x200 and not flags & 1, 'Usable default route'
        routes[str(version)] = rows
    unreachable = {}
    for family, address in ((socket.AF_INET, '192.0.2.1'), (socket.AF_INET6, '2001:db8::1')):
        for kind in (socket.SOCK_STREAM, socket.SOCK_DGRAM):
            try:
                with socket.socket(family, kind) as descriptor:
                    descriptor.settimeout(.25)
                    descriptor.connect((address, 9))
                raise AssertionError('Documentation address unexpectedly reachable')
            except OSError as error:
                assert error.errno in (errno.ENETUNREACH, errno.EHOSTUNREACH,
                                       errno.EADDRNOTAVAIL, errno.EAFNOSUPPORT), error
                unreachable[str(family) + '/' + str(kind)] = error.errno
    return {'status': 'PASS_FIVE_NAMESPACE_LOOPBACK_PREFLIGHT', 'namespaces': namespaces,
            'capabilities': capabilities, 'no_new_privileges': True, 'interfaces': ['lo'],
            'loopback_up': True, 'ipv4': ipv4, 'ipv6': ipv6, 'routes': routes,
            'unreachable_controls_errno': unreachable, 'pid': os.getpid(),
            'proc_pid1_namespace': os.readlink('/proc/1/ns/pid'),
            'label': pathlib.Path('/proc/self/attr/current').read_text().strip(),
            'functional_subjects_executed': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=pathlib.Path)
    parser.add_argument('--inside', action='store_true')
    parser.add_argument('--parent-sha256')
    args = parser.parse_args()
    if args.inside:
        raw = pathlib.Path('/work/parent.json').read_bytes()
        assert args.parent_sha256 and hashlib.sha256(raw).hexdigest() == args.parent_sha256
        parent = json.loads(raw)
        result = fence(parent)
        result['parent_sha256'] = args.parent_sha256
        assert result['proc_pid1_namespace'] == result['namespaces']['pid']['link']
        write('/work/preflight.json', result)
        return 0
    assert args.output and args.output.is_absolute()
    assert args.output.parent.resolve() == (H / 'runs').resolve() and not args.output.exists()
    assert sha(BWRAP) == BWRAP_SHA, 'Reviewed bwrap image changed'
    (H / 'runs').mkdir(exist_ok=True)
    args.output.mkdir(mode=0o700)
    fds = []
    try:
        namespaces = {}
        for name, kind in NAMES.items():
            fd = os.open('/proc/self/ns/' + name, os.O_RDONLY | os.O_CLOEXEC)
            fds.append(fd)
            assert fcntl.ioctl(fd, 0xb703) == kind
            namespaces[name] = {'fd': fd, 'identity': identity(fd),
                                'link': os.readlink('/proc/self/ns/' + name)}
        parent = {'namespaces': namespaces, 'uid': os.getuid(), 'gid': os.getgid()}
        write(args.output / 'parent.json', parent)
        command = [str(BWRAP), '--unshare-user', '--unshare-net', '--unshare-pid',
                   '--unshare-ipc', '--unshare-uts', '--disable-userns', '--cap-drop', 'ALL',
                   '--die-with-parent', '--new-session', '--clearenv',
                   '--ro-bind', '/usr', '/usr', '--ro-bind', '/etc', '/etc',
                   '--ro-bind', '/lib', '/lib']
        for path in ('/lib64', '/opt'):
            if pathlib.Path(path).exists():
                command += ['--ro-bind', path, path]
        command += ['--proc', '/proc', '--dev', '/dev', '--tmpfs', '/tmp', '--tmpfs', '/run',
                    '--dir', '/home', '--dir', '/root', '--ro-bind', str(H), '/task',
                    '--bind', str(args.output), '/work',
                    '--ro-bind', str(args.output / 'parent.json'), '/work/parent.json',
                    '--setenv', 'PATH', '/usr/bin:/bin',
                    '--setenv', 'LANG', 'C.UTF-8', '--chdir', '/work',
                    '/usr/bin/python3', '/task/preflight.py', '--inside',
                    '--parent-sha256', sha(args.output / 'parent.json')]
        inputs = {'source': str(pathlib.Path(__file__).resolve()), 'source_sha256': sha(__file__),
                  'bwrap': str(BWRAP), 'bwrap_sha256': BWRAP_SHA,
                  'python': '/usr/bin/python3', 'python_sha256': sha('/usr/bin/python3'),
                  'parent_sha256': sha(args.output / 'parent.json'), 'command': command,
                  'no_fallback': True, 'functional_subjects_executed': False}
        write(args.output / 'inputs.json', inputs)
        validate_parent(parent)
        assert sha(args.output / 'parent.json') == inputs['parent_sha256']
        with (args.output / 'stdout.log').open('wb') as stdout, (args.output / 'stderr.log').open('wb') as stderr:
            child = subprocess.Popen(command, stdout=stdout, stderr=stderr,
                                     start_new_session=True)
            try:
                code = child.wait(timeout=20)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait()
                code = 124
        validate_parent(parent)
        assert sha(args.output / 'parent.json') == inputs['parent_sha256']
        result = {'status': 'BLOCKED_NAMESPACE_PREFLIGHT', 'exit_code': code,
                  'parent_descriptors_unchanged': True,
                  'inputs_sha256': sha(args.output / 'inputs.json'),
                  'logs_sha256': {name: sha(args.output / name) for name in ('stdout.log', 'stderr.log')},
                  'functional_subjects_executed': False, 'no_fallback': True}
        if code == 0:
            actual = json.loads((args.output / 'preflight.json').read_bytes())
            assert actual['status'] == 'PASS_FIVE_NAMESPACE_LOOPBACK_PREFLIGHT'
            assert actual['parent_sha256'] == inputs['parent_sha256']
            assert sha(__file__) == inputs['source_sha256'] and sha(BWRAP) == BWRAP_SHA
            result.update(status=actual['status'], preflight_sha256=sha(args.output / 'preflight.json'))
        write(args.output / 'result.json', result)
        return 0 if code == 0 else 1
    finally:
        for fd in fds:
            os.close(fd)


if __name__ == '__main__':
    sys.exit(main())
