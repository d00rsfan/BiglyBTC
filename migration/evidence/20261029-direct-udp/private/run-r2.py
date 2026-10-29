#!/usr/bin/env python3
"""Two DIRECT UDP rows and one HTTPS/JKS regression in one private candidate; no build/promotion."""
import argparse, hashlib, importlib.util, json, os, re, shlex, shutil, sys
from pathlib import Path
import xml.etree.ElementTree as ET
sys.dont_write_bytecode = True
R = Path('/home/yurii/Java/BiglyBT'); N = R.parent/'BiglyBTC'; H = Path(__file__).resolve().parent
SUPPORT = R/'build/mainline-scrape-20261028/recipe_support_followup.py'
PLAN = R/'build/mainline-host-cohort-20261028/amendments/provider-r2-recipes/private/plan.json'
QT_PROOF = R/'build/mainline-host-cohort-20261028/amendments/provider-r2-recipes/qualification/runs/gcc-r4/runtime-images-before.json'
PINS = {SUPPORT:'64e0bd2b9ec87b9babf90bdf8d1235746f7dc63c797c82c5e5a138a4c2ea893a', PLAN:'2917d15b4d9890f6d881fb18b46a65c713a2a0a3dfe9c9996487d4deb6a302f3', QT_PROOF:'bacbdf657be1cfd5b9e531a3a5aa6325eb24b171baf2df33021e341e68a5ac9e'}
PINS.update({H.parent/'run-r2.py':'4980fd60a27cef88a66167781a7cb5030d1290a702b36bb9068102c5f7c3c66b', H.parent/'root-adoption-r2.json':'2665c13bf589bb98b910055356eb43150c08970e26a87ce43385a0b02002651c'})
UDP_METHODS = [
 'directRegularUdpSuccessfulScrapeUsesTheOriginalSharedInterval',
 'directRegularUdpSavedProxyChangesCannotKeepDirectAuthority',
 'directRegularUdpSavedRowsRequireTrackerButNotDhtOrPayload',
 'directRegularUdpActualAdmissionSharesResponseAndCannotDeliverAfterPause',
 'directRegularUdpFreshTrackerDenialCancelsConnectOrReply',
]
UDP_CASES = [
 UDP_METHODS[0], UDP_METHODS[1],
 *[UDP_METHODS[2]+'('+tag+')' for tag in ('global-DHT-disabled','private','DHT-source-denied')],
 *[UDP_METHODS[3]+'('+tag+')' for tag in ('DHT-disabled-real-announce-failure-then-scrape','private-real-announce-failure-then-scrape','private-pause-before-held-scrape')],
 *[UDP_METHODS[4]+'('+tag+')' for tag in ('deny-before-CONNECT-consumed','deny-before-scrape-terminal')],
]
SELECTIONS = [
 ('biglybtc-mainline-single-tracker-owner-tests',UDP_METHODS[3]+':DHT-disabled-real-announce-failure-then-scrape',[UDP_CASES[5]]),
 ('biglybtc-mainline-single-tracker-owner-tests',UDP_METHODS[2]+':global-DHT-disabled',[UDP_CASES[2]]),
 ('biglybtc-mainline-single-http-tests','directAdmittedTrackerUsesActualAnnounceOrSavedTrustWithoutMainline:private-HTTPS-saved-JKS-regular-scrape',['directAdmittedTrackerUsesActualAnnounceOrSavedTrustWithoutMainline(private-HTTPS-saved-JKS-regular-scrape)']),
]

def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def load(p): return json.loads(Path(p).read_bytes())
def need(v, message):
    if not v: raise RuntimeError(message)
def bundle(root):
    d = load(root/'BUILD_MANIFEST.json')
    need(d['distribution_scope']=='private-qualification-only', 'Expected private-only bundle')
    actual = {p.relative_to(root).as_posix():sha(p) for p in root.rglob('*') if p.is_file() and p!=root/'BUILD_MANIFEST.json'}
    need(actual==d['files_sha256'], 'Bundle leaves changed'); return d

SUITE_IMAGES = {'queue-policy':'biglybtc-queue-policy-tests','network-policy':'biglybtc-network-policy-tests',
    'mainline-dht':'biglybtc-mainline-dht-tests','mainline-single-http':'biglybtc-mainline-single-http-tests',
    'mainline-single-udp':'biglybtc-mainline-single-udp-tests','mainline-single-tracker-owner':'biglybtc-mainline-single-tracker-owner-tests',
    'owned-tor-child-lifecycle':'biglybtc-tor-owned-child-tests','managed-queue':'biglybtc-managed-queue-tests',
    'pure-i2p-datagram-codec':'biglybtc-i2p-datagram-tests'}
def actual_suite_commands(log, wanted, images):
    blocks={}; active=None
    for line in log.read_text(errors='replace').splitlines():
        match=re.fullmatch(r'\d+/\d+ Testing: (.+)',line)
        if match:
            active=match.group(1); assert active not in blocks, 'Repeated actual suite'; blocks[active]=[]
        elif active is not None: blocks[active].append(line)
    assert set(blocks)==set(wanted), 'Actual command inventory differs'
    for name,lines in blocks.items():
        commands=[shlex.split(line.removeprefix('Command: ')) for line in lines if line.startswith('Command: ')]
        assert len(commands)==1 and commands[0], 'Missing/ambiguous actual suite command'
        assert Path(commands[0][0]).resolve()==Path(images[SUITE_IMAGES[name]]['path']).resolve(), 'Suite ran a different ELF '+name
    return blocks

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-commit', required=True); parser.add_argument('--attempt', required=True)
    for lane in ('gcc','clang','san'):
        parser.add_argument('--'+lane+'-result', type=Path, required=True)
        parser.add_argument('--'+lane+'-sha256', required=True)
    a = parser.parse_args(); need(re.fullmatch('[A-Za-z0-9_-]+',a.attempt), 'Unsafe attempt')
    for path, pin in PINS.items(): need(sha(path)==pin, 'Pinned helper/input changed '+str(path))
    spec = importlib.util.spec_from_file_location('private_network_ui_support',SUPPORT); s = importlib.util.module_from_spec(spec); spec.loader.exec_module(s)
    source = s.source_snapshot(a.source_commit); plan = load(PLAN); provider = plan['provider']
    for key in ('manifest','record','approval','review'):
        need(sha(provider[key]['path'])==provider[key]['sha256'], 'Provider authority changed '+key)
    binding = load(provider['manifest']['path']); artifact = Path(binding['artifact'])
    need(sha(artifact)==provider['artifact_sha256'] and artifact.stat().st_size==provider['artifact_bytes'], 'Provider bytes differ')
    adoption=load(H.parent/'root-adoption-r2.json')
    need(adoption['source_commit']==a.source_commit=='0cb5817bf07c6268bcccf9722f8c741a904163c6' and adoption['source_path_count']==len(adoption['source_paths'])==11, 'Current composed source differs')
    for name,row in adoption['source_paths'].items():need(sha(N/name)==row['after_sha256'], 'Current composed leaf differs '+name)
    results = {}; image_maps = {}; authorities = {}
    for lane in ('gcc','clang','san'):
        path=getattr(a,lane+'_result').resolve();need(sha(path)==getattr(a,lane+'_sha256'),'Result changed '+lane);d=load(path)
        wanted={'queue-policy','managed-queue','mainline-single-http'} | (set() if lane=='clang' else {'mainline-single-tracker-owner'})
        need(d['status']=='PASS_DIRECT_UDP_COHORT' and d['lane']==lane and d['source_commit']==a.source_commit,'Actual combined PASS required '+lane)
        need(d['source_unchanged'] and d['images_unchanged'] and d['actual_suites']==len(wanted) and set(d['suite_names'])==wanted,'Combined suite scope differs')
        need(d['compile_only_suites']==(['mainline-single-tracker-owner'] if lane=='clang' else []),'Compile-only scope differs')
        need(d['provider_sha256']==provider['artifact_sha256'],'Combined provider differs')
        need(load(path.parent/'source-before.json')==source,'Combined source differs '+lane)
        need(sha(path.parent/'tests.xml')==d['tests_xml_sha256'],'Actual combined XML changed');s.passed_xml(path.parent/'tests.xml',wanted)
        need(sha(path.parent/'ctest-full.log')==d['full_ctest_log_sha256'],'Full test log changed')
        need(not re.search(r'^(?:SKIP\s*:|FAIL!)',(path.parent/'ctest-full.log').read_text(),re.M),'Failed/skipped Qt row')
        need(sha(H.parent/'run-r2.py')==d['runner_sha256'] and sha(H.parent/'root-adoption-r2.json')==d['source_receipt_sha256'],'Runner/source authority differs')
        need(sha(path.parent/'commands.json')==d['commands_sha256'],'Actual commands changed');commands=load(path.parent/'commands.json')
        need(all(row.get('exit')==0 and 'exception' not in row for row in commands),'Incomplete command')
        need([row['name'] for row in commands]==(['build','http-loader','tests','udp-selected'] if lane=='clang' else ['build','http-loader','tests']),'Actual command inventory differs')
        for row in commands:need(sha(row['log'])==row['log_sha256'],'Actual command log changed '+row['name'])
        images=load(path.parent/'images-before.json')
        need(len(images)==7 and set(images)==set(d['compiled_targets']), 'Exact seven compiled images required')
        need(set(images)=={'biglybtc-mainline-single-http-tests','biglybtc-managed-queue-tests','biglybtc-queue-policy-tests','biglybtc-mainline-single-tracker-owner-tests','BiglyBTC','BiglyBTC-headless','biglybtc-preview-smoke'}, 'Compiled products differ')
        actual_suite_commands(path.parent/'ctest-full.log',wanted,images)
        need(all(row.get('status')=='run' for row in ET.parse(path.parent/'tests.xml').getroot().iter('testcase')), 'Suite was not run')
        if lane=='clang':
            selected=path.parent/'udp-selected.xml'
            need(d['actual_selected_udp_rows']==10 and sha(selected)==d['selected_udp_xml_sha256'], 'Actual selected UDP receipt differs')
            names=s.passed_xml(selected);normalized=[name.rsplit('::',1)[-1].removesuffix('()') for name in names]
            need(len(normalized)==12 and set(normalized)=={'initTestCase','cleanupTestCase',*UDP_CASES}, 'Exact ten actual UDP rows required')
            need(commands[3]['command']==[images['biglybtc-mainline-single-tracker-owner-tests']['path'],*UDP_METHODS,'-o',str(selected)+',junitxml'], 'Actual selected UDP command/ELF differs')
            need(not re.search(r'^(?:SKIP\s*:|FAIL!)',Path(commands[3]['log']).read_text(),re.M), 'Selected UDP row failed/skipped')
        for image in images.values():need(sha(image['path'])==image['sha256'] and Path(image['path']).stat().st_size==image['bytes'],'Qualified image changed')
        image_maps[lane]=images;results[lane]=d
        authorities[lane]={'path':str(path),'sha256':sha(path),'images_sha256':sha(path.parent/'images-before.json'),'source_before_sha256':sha(path.parent/'source-before.json'),'fresh_suite_names':sorted(wanted),'actual_selected_udp_rows':10 if lane=='clang' else None,'selected_udp_xml_sha256':d.get('selected_udp_xml_sha256')}
    out = H/'runs'/a.attempt; out.mkdir(parents=True,exist_ok=False); env = s.environment('gcc')
    for key in ('ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','LD_DEBUG_OUTPUT','BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_PREVIEW_CAPTURE','BIGLYBTC_HTTP_PRIVATE_RUNTIME','QT_TLS_BACKEND','QML_IMPORT_PATH','QML2_IMPORT_PATH','QT_QPA_PLATFORMTHEME'): env.pop(key,None)
    for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
        p = out/key.lower(); p.mkdir(mode=0o700); env[key] = str(p)
    runner = s.Runner(out,env); candidate = out/'candidate'; result = {'status':'FAIL','source_commit':a.source_commit,'focused_results':authorities,'builds_executed':False,'promotion_executed':False,'historical_runtime_passes_pooled':False,'qualification_scope':'fresh suites GCC4/Clang3/SAN4; Clang additionally executes ten UDP rows, full owner suite compile-only'}
    try:
        s.write(out/'source-before.json',source); old_dist = bundle(N/'dist'); dist_pin = sha(N/'dist/BUILD_MANIFEST.json')
        cache = Path(results['gcc']['cache']); stager = N/'preview/migration/tools/stage_preview.py'
        need(sha(stager)==source['files_sha256'][str(stager.relative_to(N))], 'Stager source differs')
        package = plan['debian_package']; need(sha(package['path'])==package['sha256'], 'Runtime package differs')
        runner.run('stage',['python3','-B',stager,'--build',cache,'--output',candidate,'--libtorrent-package',package['path'],'--source-libtorrent',artifact,'--private-mainline-routing-provider-manifest',provider['manifest']['path']],timeout=240)
        staged = bundle(candidate); staged_pin = sha(candidate/'BUILD_MANIFEST.json'); private = candidate/'usr/lib/biglybtc'
        need(staged['libtorrent_runtime']['runtime_manifest_sha256']==provider['manifest']['sha256'], 'Staged binding differs')
        for alias in plan['provider_aliases']: need(sha(candidate/alias)==provider['artifact_sha256'], 'Provider alias differs')
        tls = private/'qt/plugins/tls/libqopensslbackend.so'; need(sha(tls)==plan['tls_backend_sha256'], 'Private TLS backend differs')
        proof = load(QT_PROOF); qt = [v for k,v in proof.items() if re.fullmatch(r'libQt6Test\.so\.6(?:\..+)?',Path(k).name)]
        need(len(qt)==1 and sha(qt[0]['path'])==qt[0]['sha256'], 'Last qualified QtTest changed')
        support = out/'test-support'; support.mkdir(); shutil.copyfile(qt[0]['path'],support/'libQt6Test.so.6'); need(sha(support/'libQt6Test.so.6')==qt[0]['sha256'], 'QtTest copy differs')
        copies = {}
        for target in sorted({row[0] for row in SELECTIONS}):
            image = image_maps['gcc'][target]; copied = out/target
            shutil.copyfile(image['path'],copied); copied.chmod(0o755)
            need(sha(copied)==image['sha256'], 'Test ELF copy differs '+target); copies[target]=copied
        env.update(LD_LIBRARY_PATH=os.pathsep.join(map(str,[support,private/'lib',private/'qt/lib',candidate/'dependencies/libtorrent/usr/lib/x86_64-linux-gnu'])),QT_PLUGIN_PATH=str(private/'qt/plugins'),QT_QPA_PLATFORM_PLUGIN_PATH=str(private/'qt/plugins/platforms'),BIGLYBTC_HTTP_PRIVATE_RUNTIME=str(private))
        for target,copied in copies.items():
            linked = runner.run('loader-'+target,['ldd',copied],timeout=30).read_text()
            need('not found' not in linked and '/opt/Qt' not in linked, 'Nonprivate/missing dependency '+target)
        actual = []
        for i, (target, selector, expected_names) in enumerate(SELECTIONS,1):
            label = 'selected-'+str(i); copied=copies[target]; env['LD_DEBUG'] = 'libs'
            try: runner.run(label,[copied,selector,'-o',str(out/(label+'.txt'))+',txt','-o',str(out/(label+'.xml'))+',junitxml'],timeout=100)
            finally: env.pop('LD_DEBUG',None)
            names = s.passed_xml(out/(label+'.xml'))
            normalized=[x.rsplit('::',1)[-1].removesuffix('()') for x in names]
            need(set(normalized)=={'initTestCase','cleanupTestCase',*expected_names} and len(normalized)==len(expected_names)+2, 'Wrong/extra selected method '+label)
            loaded = {Path(x.strip()).resolve() for x in re.findall(r'calling init:\s*([^\n]+)',(out/(label+'.log')).read_text(errors='replace'))}
            need((private/'qt/lib/libQt6Core.so.6').resolve() in loaded and (private/'qt/lib/libQt6Network.so.6').resolve() in loaded, 'Actual private QtCore/QtNetwork missing '+label)
            need((private/'lib/libtorrent-rasterbar.so.2.0.12').resolve() in loaded,'Actual private provider missing '+label)
            if target=='biglybtc-mainline-single-http-tests':
                need(tls.resolve() in loaded,'Actual private TLS backend missing')
                for name in ('libssl.so.3','libcrypto.so.3'):
                    values=[p for p in loaded if p.name==name];need(len(values)==1 and values[0].is_relative_to(Path('/usr/lib')),'OS OpenSSL image missing '+name)

            for p in loaded:
                if p.name.startswith(('libQt6','libqopensslbackend','libqoffscreen','libtorrent-rasterbar')):
                    need(p.is_relative_to(private.resolve()) or p==(support/'libQt6Test.so.6').resolve(), 'Actual Qt/provider escaped '+str(p))
            actual.append({'target':target,'selector':selector,'exit':0,'xml_sha256':sha(out/(label+'.xml')),'case_names':names,'initialized_images_sha256':{str(p):sha(p) for p in sorted(loaded) if p.name.startswith(('libQt6','libqopensslbackend','libqoffscreen','libtorrent-rasterbar','libssl.so','libcrypto.so'))}})
        for target,copied in copies.items(): need(sha(copied)==image_maps['gcc'][target]['sha256'], 'Copied test changed '+target)
        need(bundle(candidate)==staged and sha(candidate/'BUILD_MANIFEST.json')==staged_pin, 'Candidate changed')
        need(s.source_snapshot(a.source_commit)==source and bundle(N/'dist')==old_dist and sha(N/'dist/BUILD_MANIFEST.json')==dist_pin, 'Source/current dist changed')
        for images in image_maps.values():
            for value in images.values(): need(sha(value['path'])==value['sha256'], 'Qualified image changed during private check')
        need(sha(artifact)==provider['artifact_sha256'], 'Provider changed')
        result.update(status='PASS_PRIVATE_DIRECT_UDP_AND_HTTPS',candidate=str(candidate),candidate_manifest_sha256=staged_pin,actual_leaf_count=len(staged['files_sha256']),copied_images={target:image_maps['gcc'][target] for target in copies},actual_tests=actual,provider_sha256=provider['artifact_sha256'],old_dist_unchanged=dist_pin,source_unchanged=True)
    except BaseException as e: result['failure'] = repr(e)
    finally:
        if (out/'commands.json').exists(): result['commands_sha256'] = sha(out/'commands.json')
        s.write(out/'result.json',result)
    print(result['status'],out/'result.json',flush=True); return 0 if result['status'].startswith('PASS_') else 1
if __name__=='__main__': raise SystemExit(main())
