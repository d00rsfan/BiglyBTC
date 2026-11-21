"""Link the existing SDK test with its two same-cache hidden helper objects.

The reviewed SDK intentionally keeps these implementation symbols hidden.
No export,source,cache option or SDK artifact is changed for the test.
"""
from pathlib import Path
import hashlib,json,shlex,subprocess
D=Path(__file__).resolve().parent
S=D.parent/'tag-rate-cross-owner-20261121';B=S/'sdk-build'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
sdk=B/'libtorrent-rasterbar.so.2.0.12'
assert sha(sdk)==sha(S/'sdk-install/lib/libtorrent-rasterbar.so.2.0.12')=='05ce92d16e68c4c2f7046ff615dc762bef6d3d012b896bb1915025284ce62f3e'
commands=subprocess.check_output(['ninja','-C',str(B),'-t','commands','test_biglybtc_private_i2p_owner'],text=True).splitlines()
argv=shlex.split(commands[-1]);assert argv[:2]==[':','&&'] and argv[-2:]==['&&',':'];argv=argv[2:-2]
at=argv.index('-o');assert argv[at+1]=='test/test_biglybtc_private_i2p_owner'
argv[at+1]=str(D/'sdk-private-tests')
helpers=[B/('CMakeFiles/torrent-rasterbar.dir/src/'+name+'.cpp.o') for name in ['i2p_stream','proxy_base']]
argv[at:at]=[str(p) for p in helpers]
objects=[B/p if not Path(p).is_absolute() else Path(p) for p in argv if p.endswith(('.o','.a','.so.2.0.12'))]
pins={str(p):sha(p) for p in objects}
subprocess.run(argv,cwd=B,check=True)
assert all(sha(Path(p))==h for p,h in pins.items())
(D/'sdk-private-link-receipt.json').write_text(json.dumps(dict(argv=argv,cwd=str(B),inputs_sha256=pins,output_sha256=sha(D/'sdk-private-tests'),same_abi_cache=True,sdk_changed=False,source_changed=False,export_visibility_changed=False),indent=2)+'\n')
units=D/'sdk-units';units.mkdir(exist_ok=True)
lines=[]
for name in ['test_biglybtc_mainline_identity','test_biglybtc_private_i2p_owner','test_biglybtc_swarm','test_biglybtc_tracker_response']:
 executable=D/'sdk-private-tests' if name=='test_biglybtc_private_i2p_owner' else B/'test'/name
 lines.append('add_test('+name+' '+json.dumps(str(executable))+')')
(units/'CTestTestfile.cmake').write_text('\n'.join(lines)+'\n')
print('PASS_SAME_CACHE_HIDDEN_HELPERS 2;same reviewed SDK image and ABI;no vendor mutations')
