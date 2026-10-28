#!/usr/bin/python3
"""Generated owned-child fixture only: localhost SAFECOOKIE; never runs Tor."""
import hashlib
import hmac
import json
import os
from pathlib import Path
import signal
import socket
import sys
import time

MODE = 'good'
assert sys.argv[1:] == ['-f', 'torrc', '__OwningControllerProcess', str(os.getppid())]
configuration = Path('torrc').read_text()
for required in ('DisableNetwork 1\n', 'SocksPort 0\n', 'ControlPort 127.0.0.1:auto\n',
                 'CookieAuthentication 1\n', 'DataDirectory data\n', 'RunAsDaemon 0\n'):
    assert required in configuration
if MODE == 'ignore-term':
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
if MODE == 'hang':
    while True:
        time.sleep(1)

server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.bind(('127.0.0.1', 0))
server.listen(1)
port = server.getsockname()[1]
cookie = os.urandom(32)
cookie_path = Path('data/control_auth_cookie')
if MODE in ('symlink-cookie', 'hardlink-cookie'):
    # A generated outsider sentinel is supplied by this exact test fixture.
    outsider = Path(__file__).resolve().parent / 'outsider-cookie'
    if MODE == 'symlink-cookie':
        cookie_path.symlink_to(outsider)
    else:
        os.link(outsider, cookie_path)
else:
    fd = os.open(cookie_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        if MODE == 'partial-cookie':
            os.write(fd, cookie[:16])
            os.fsync(fd)
            time.sleep(0.08)
            os.write(fd, cookie[16:])
        else:
            os.write(fd, cookie)
        os.fsync(fd)
    finally:
        os.close(fd)
fd = os.open('control.port', os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
try:
    host = '0.0.0.0' if MODE == 'bad-address' else '127.0.0.1'
    os.write(fd, ('PORT=' + host + ':' + str(port) + '\n').encode('ascii'))
    os.fsync(fd)
finally:
    os.close(fd)

audit = {'pid': os.getpid(), 'parent': os.getppid(), 'port': port, 'commands': []}
def record(command):
    audit['commands'].append(command)
    fd = os.open('child-audit.json', os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    try:
        os.write(fd, json.dumps(audit).encode('ascii'))
        os.fsync(fd)
    finally:
        os.close(fd)

client, remote = server.accept()
assert remote[0] == '127.0.0.1'
with client, client.makefile('rb') as lines:
    line = lines.readline(1025)
    assert line.startswith(b'AUTHCHALLENGE SAFECOOKIE ') and line.endswith(b'\r\n')
    nonce = bytes.fromhex(line.split()[2].decode('ascii'))
    assert len(nonce) == 32
    record('AUTHCHALLENGE')
    server_nonce = os.urandom(32)
    message = cookie + nonce + server_nonce
    digest = hmac.new(b'Tor safe cookie authentication server-to-controller hash', message, hashlib.sha256).hexdigest().upper()
    if MODE == 'bad-proof':
        digest = ('0' if digest[0] != '0' else '1') + digest[1:]
    client.sendall(('250 AUTHCHALLENGE SERVERHASH=' + digest + ' SERVERNONCE=' + server_nonce.hex().upper() + '\r\n').encode('ascii'))
    line = lines.readline(1025)
    if MODE == 'bad-proof':
        assert not line
        sys.exit(0)
    expected = hmac.new(b'Tor safe cookie authentication controller-to-server hash', message, hashlib.sha256).hexdigest().upper()
    assert hmac.compare_digest(line, ('AUTHENTICATE ' + expected + '\r\n').encode('ascii'))
    record('AUTHENTICATE')
    client.sendall(b'250 OK\r\n')
    for command in (b'TAKEOWNERSHIP', b'RESETCONF __OwningControllerProcess', b'GETINFO version'):
        assert lines.readline(1025) == command + b'\r\n'
        record(command.decode('ascii'))
        client.sendall(b'250-version=owned-child-fixture\r\n250 OK\r\n' if command == b'GETINFO version' else b'250 OK\r\n')
    if MODE == 'crash':
        time.sleep(0.03)
        os._exit(17)
    while lines.readline(1025):
        raise AssertionError('No post-setup command is authorized')
    if MODE == 'ignore-term':
        while True:
            time.sleep(1)
