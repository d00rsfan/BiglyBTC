#!/usr/bin/env python3
"""Root-only namespace metadata diagnostic; never assigns an address."""
import fcntl
import importlib.util
import json
import os
import pathlib
import subprocess
import sys

WRAPPER=pathlib.Path('/home/yurii/Java/BiglyBT/build/azureus-public-bound-20261029/amendments/bwrap-namespace/staging/preview/native/tests/azureus_public_bound_namespace.py')


def main():
    if len(sys.argv)==4 and sys.argv[1]=='--inside':
        net_fd,user_fd=map(int,sys.argv[2:])
        spec=importlib.util.spec_from_file_location('reviewed_namespace',WRAPPER)
        fixture=importlib.util.module_from_spec(spec)
        spec.loader.exec_module(fixture)
        fixture.fence(net_fd,user_fd)
        current_net=os.open('/proc/self/ns/net',os.O_RDONLY|os.O_CLOEXEC)
        owner={}
        try:
            # NS_GET_USERNS from installed linux/nsfs.h: _IO(0xb7,1).
            try:
                owner_fd=fcntl.ioctl(current_net,0xb701)
            except OSError as error:
                owner={'errno':error.errno,'error':str(error)}
            else:
                try:
                    owner={'identity':fixture.identity(f'/proc/self/fd/{owner_fd}'),
                           'same_as_current_userns':fixture.identity(f'/proc/self/fd/{owner_fd}')==fixture.identity('/proc/self/ns/user')}
                finally:
                    os.close(owner_fd)
        finally:
            os.close(current_net)
        result={'status':'METADATA_ONLY_NO_ADDRESS_OR_FUNCTIONAL_TEST',
                'label':pathlib.Path('/proc/self/attr/current').read_text().strip(),
                'privileges':fixture.privileges(),'net_owner_userns':owner,
                'current_userns':fixture.identity('/proc/self/ns/user'),
                'current_netns':fixture.identity('/proc/self/ns/net'),
                'uid_map':pathlib.Path('/proc/self/uid_map').read_text(),
                'gid_map':pathlib.Path('/proc/self/gid_map').read_text()}
        print(json.dumps(result,indent=2),flush=True)
        return 0
    if len(sys.argv)!=1:
        raise RuntimeError('No arguments: root-only metadata preflight')
    net_fd=os.open('/proc/self/ns/net',os.O_RDONLY|os.O_CLOEXEC)
    user_fd=os.open('/proc/self/ns/user',os.O_RDONLY|os.O_CLOEXEC)
    try:
        command=['/usr/bin/bwrap','--unshare-user','--unshare-net','--uid','0','--gid','0',
                 '--bind','/','/','--die-with-parent','--new-session','--cap-drop','ALL',
                 '--cap-add','CAP_NET_ADMIN','--',sys.executable,str(pathlib.Path(__file__).resolve()),
                 '--inside',str(net_fd),str(user_fd)]
        return subprocess.run(command,stdin=subprocess.DEVNULL,pass_fds=(net_fd,user_fd)).returncode
    finally:
        os.close(user_fd)
        os.close(net_fd)


if __name__=='__main__':
    raise SystemExit(main())
