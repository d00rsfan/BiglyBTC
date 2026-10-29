#!/usr/bin/env python3
"""Root-pinned active tracker/channel private rows; draft refuses execution until final selections/source are frozen."""
import argparse, hashlib, importlib.util, json, os, re, shlex, shutil, sys
from pathlib import Path
import xml.etree.ElementTree as ET
sys.dont_write_bytecode = True
R = Path('/home/yurii/Java/BiglyBT'); N = R.parent/'BiglyBTC'; H = Path(__file__).resolve().parent
SUPPORT = R/'build/mainline-scrape-20261028/recipe_support_followup.py'
PLAN = R/'build/mainline-host-cohort-20261028/amendments/provider-r2-recipes/private/plan.json'
QT_PROOF = R/'build/mainline-host-cohort-20261028/amendments/provider-r2-recipes/qualification/runs/gcc-r4/runtime-images-before.json'
PINS = {SUPPORT:'64e0bd2b9ec87b9babf90bdf8d1235746f7dc63c797c82c5e5a138a4c2ea893a', PLAN:'2917d15b4d9890f6d881fb18b46a65c713a2a0a3dfe9c9996487d4deb6a302f3', QT_PROOF:'bacbdf657be1cfd5b9e531a3a5aa6325eb24b171baf2df33021e341e68a5ac9e'}
PINS.update({H.parent/'run-r2.py':'dd7b6766b07cf060a5e04967b8b5af070a5abc71984a5a5b1cbfea9a50aab754'})
HTTPS_SELECTOR='directAdmittedTrackerUsesActualAnnounceOrSavedTrustWithoutMainline:private-HTTPS-saved-JKS-regular-scrape'

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
    'pure-i2p-datagram-codec':'biglybtc-i2p-datagram-tests','pure-i2cp-initial-session-codec':'biglybtc-i2cp-protocol-tests','i2cp-initial-channel':'biglybtc-i2cp-initial-channel-tests'}
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
    parser.add_argument('--selection-plan',type=Path,required=True);parser.add_argument('--selection-plan-sha256',required=True)
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
    need(sha(a.selection_plan)==a.selection_plan_sha256, 'Selection plan changed')
    selection=load(a.selection_plan)
    need(selection['status']=='ROOT_FROZEN_PRIVATE_SELECTIONS' and selection['source_commit']==a.source_commit, 'Root must freeze final source/selections first')
    need(selection['source_receipt_sha256']==sha(H.parent/'root-adoption-r2.json'), 'Final adoption receipt differs')
    adoption=load(H.parent/'root-adoption-r2.json')
    need(adoption['source_commit']==a.source_commit and adoption['source_path_count']==len(adoption['source_paths'])>0, 'Current composed source differs')
    for name,row in adoption['source_paths'].items():need(sha(N/name)==row['after_sha256'], 'Current composed leaf differs '+name)
    need(selection['source_capsules'], 'Final reviewed capsule pins required')
    for row in selection['source_capsules']:need(sha(row['path'])==row['sha256'], 'Final capsule/review changed')
    rows=selection['selections'];need([row['role'] for row in rows]==['managed_multi','udp_current','channel_full','sole_https'], 'Expected fixed four private roles')
    for row in rows:
        need(row['expected_names'] and len(row['expected_names'])==len(set(row['expected_names'])), 'Final exact runnable case names required')
        if row['role']=='channel_full':need(row['target']=='biglybtc-i2cp-initial-channel-tests' and row['selector'] is None, 'Expected whole channel only')
        else:need(isinstance(row['selector'],str) and row['selector'], 'Final actual method/data selector required')
        if row['role'] in ('managed_multi','udp_current'):need(row['target']=='biglybtc-managed-queue-tests', 'Expected actual managed tracker row')
        if row['role']=='sole_https':need(row['target']=='biglybtc-mainline-single-http-tests' and row['selector']==HTTPS_SELECTOR, 'Known sole HTTPS selector differs')
    SELECTIONS=[(row['target'],row['selector'],row['expected_names'],row['role']) for row in rows]
    results = {}; image_maps = {}; authorities = {}
    for lane in ('gcc','clang','san'):
        path=getattr(a,lane+'_result').resolve();need(sha(path)==getattr(a,lane+'_sha256'),'Result changed '+lane);d=load(path)
        wanted={'queue-policy','managed-queue','i2cp-initial-channel'}
        need(d['status']=='PASS_ACTIVE_CHANNEL_COHORT' and d['lane']==lane and d['source_commit']==a.source_commit,'Actual combined PASS required '+lane)
        need(d['source_unchanged'] and d['images_unchanged'] and d['actual_suites']==len(wanted) and set(d['suite_names'])==wanted,'Combined suite scope differs')
        need(d['compile_only_suites']==['mainline-single-tracker-owner'],'Compile-only scope differs')
        need(d['provider_sha256']==provider['artifact_sha256'],'Combined provider differs')
        need(load(path.parent/'source-before.json')==source,'Combined source differs '+lane)
        need(sha(path.parent/'tests.xml')==d['tests_xml_sha256'],'Actual combined XML changed');s.passed_xml(path.parent/'tests.xml',wanted)
        need(sha(path.parent/'ctest-full.log')==d['full_ctest_log_sha256'],'Full test log changed')
        need(all(row.get('status','run')=='run' for row in ET.parse(path.parent/'sole-https-regression.xml').getroot().iter('testcase')), 'Qt case explicitly not run')
        need(not re.search(r'^(?:SKIP\s*:|FAIL!)',(path.parent/'ctest-full.log').read_text(),re.M),'Failed/skipped Qt row')
        need(sha(H.parent/'run-r2.py')==d['runner_sha256'] and sha(H.parent/'root-adoption-r2.json')==d['source_receipt_sha256'],'Runner/source authority differs')
        need(sha(path.parent/'commands.json')==d['commands_sha256'],'Actual commands changed');commands=load(path.parent/'commands.json')
        need(all(row.get('exit')==0 and 'exception' not in row for row in commands),'Incomplete command')
        need([row['name'] for row in commands]==['build','http-loader','tests','sole-https-regression'],'Actual four-command inventory differs')
        for row in commands:need(sha(row['log'])==row['log_sha256'],'Actual command log changed '+row['name'])
        images=load(path.parent/'images-before.json')
        need(len(images)==8 and set(images)==set(d['compiled_targets']), 'Exact eight compiled images required')
        need(set(images)=={'biglybtc-mainline-single-http-tests','biglybtc-managed-queue-tests','biglybtc-queue-policy-tests','biglybtc-mainline-single-tracker-owner-tests','biglybtc-i2cp-initial-channel-tests','BiglyBTC','BiglyBTC-headless','biglybtc-preview-smoke'}, 'Compiled products differ')
        actual_suite_commands(path.parent/'ctest-full.log',wanted,images)
        need(sha(path.parent/'sole-https-regression.xml')==d['sole_https_xml_sha256'] and d['sole_https_selector']==HTTPS_SELECTOR, 'Actual separate HTTPS receipt differs')
        names=s.passed_xml(path.parent/'sole-https-regression.xml');normalized=[x.rsplit('::',1)[-1].removesuffix('()') for x in names]
        need(len(normalized)==3 and set(normalized)=={'initTestCase','cleanupTestCase','directAdmittedTrackerUsesActualAnnounceOrSavedTrustWithoutMainline(private-HTTPS-saved-JKS-regular-scrape)'}, 'Separate HTTPS exact row differs')
        need(commands[3]['command']==['nice','-n','5',images['biglybtc-mainline-single-http-tests']['path'],HTTPS_SELECTOR,'-o',str(path.parent/'sole-https-regression.txt')+',txt','-o',str(path.parent/'sole-https-regression.xml')+',junitxml'], 'Actual separate HTTPS command/image differs')
        need(all(row.get('status')=='run' for name in ('tests.xml',) for row in ET.parse(path.parent/name).getroot().iter('testcase')), 'Test row was not run')
        need(not re.search(r'^(?:SKIP\s*:|FAIL!)',(path.parent/'sole-https-regression.txt').read_text(),re.M), 'Separate HTTPS row skipped/failed')
        for image in images.values():need(sha(image['path'])==image['sha256'] and Path(image['path']).stat().st_size==image['bytes'],'Qualified image changed')
        image_maps[lane]=images;results[lane]=d
        authorities[lane]={'path':str(path),'sha256':sha(path),'images_sha256':sha(path.parent/'images-before.json'),'source_before_sha256':sha(path.parent/'source-before.json')}
    out = H/'runs'/a.attempt; out.mkdir(parents=True,exist_ok=False); env = s.environment('gcc')
    for key in ('ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','LD_DEBUG_OUTPUT','BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_PREVIEW_CAPTURE','BIGLYBTC_HTTP_PRIVATE_RUNTIME','QT_TLS_BACKEND','QML_IMPORT_PATH','QML2_IMPORT_PATH','QT_QPA_PLATFORMTHEME'): env.pop(key,None)
    for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
        p = out/key.lower(); p.mkdir(mode=0o700); env[key] = str(p)
    runner = s.Runner(out,env); candidate = out/'candidate'; result = {'status':'FAIL','source_commit':a.source_commit,'focused_results':authorities,'builds_executed':False,'promotion_executed':False,'historical_runtime_passes_pooled':False,'qualification_scope':'fresh three CTest suites plus one separate sole HTTPS row per lane; owner fullsuite compile-only all lanes; no pooled runtime origins','selection_plan_sha256':sha(a.selection_plan)}
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
        for i, (target, selector, expected_names, role) in enumerate(SELECTIONS,1):
            label = 'selected-'+str(i); copied=copies[target]; env['LD_DEBUG'] = 'libs'
            try: runner.run(label,[copied,*([selector] if selector else []),'-o',str(out/(label+'.txt'))+',txt','-o',str(out/(label+'.xml'))+',junitxml'],timeout=180)
            finally: env.pop('LD_DEBUG',None)
            names = s.passed_xml(out/(label+'.xml'))
            normalized=[x.rsplit('::',1)[-1].removesuffix('()') for x in names]
            need(set(normalized)=={'initTestCase','cleanupTestCase',*expected_names} and len(normalized)==len(expected_names)+2, 'Wrong/extra selected method '+label)
            loaded = {Path(x.strip()).resolve() for x in re.findall(r'calling init:\s*([^\n]+)',(out/(label+'.log')).read_text(errors='replace'))}
            need((private/'qt/lib/libQt6Core.so.6').resolve() in loaded and (private/'qt/lib/libQt6Network.so.6').resolve() in loaded, 'Actual private QtCore/QtNetwork missing '+label)
            if role!='channel_full':need((private/'lib/libtorrent-rasterbar.so.2.0.12').resolve() in loaded,'Actual private provider missing '+label)
            if role=='sole_https':
                need(tls.resolve() in loaded,'Actual private TLS backend missing')
                for name in ('libssl.so.3','libcrypto.so.3'):
                    values=[p for p in loaded if p.name==name];need(len(values)==1 and values[0].is_relative_to(Path('/usr/lib')),'OS OpenSSL image missing '+name)

            for p in loaded:
                if p.name.startswith(('libQt6','libqopensslbackend','libqoffscreen','libtorrent-rasterbar')):
                    need(p.is_relative_to(private.resolve()) or p==(support/'libQt6Test.so.6').resolve(), 'Actual Qt/provider escaped '+str(p))
            actual.append({'target':target,'selector':selector,'role':role,'exit':0,'xml_sha256':sha(out/(label+'.xml')),'case_names':names,'initialized_images_sha256':{str(p):sha(p) for p in sorted(loaded) if p.name.startswith(('libQt6','libqopensslbackend','libqoffscreen','libtorrent-rasterbar','libssl.so','libcrypto.so'))}})
        for target,copied in copies.items(): need(sha(copied)==image_maps['gcc'][target]['sha256'], 'Copied test changed '+target)
        need(bundle(candidate)==staged and sha(candidate/'BUILD_MANIFEST.json')==staged_pin, 'Candidate changed')
        need(s.source_snapshot(a.source_commit)==source and bundle(N/'dist')==old_dist and sha(N/'dist/BUILD_MANIFEST.json')==dist_pin, 'Source/current dist changed')
        for images in image_maps.values():
            for value in images.values(): need(sha(value['path'])==value['sha256'], 'Qualified image changed during private check')
        need(sha(artifact)==provider['artifact_sha256'], 'Provider changed')
        result.update(status='PASS_PRIVATE_ACTIVE_TRACKERS_AND_I2CP_CHANNEL',candidate=str(candidate),candidate_manifest_sha256=staged_pin,actual_leaf_count=len(staged['files_sha256']),copied_images={target:image_maps['gcc'][target] for target in copies},actual_tests=actual,provider_sha256=provider['artifact_sha256'],old_dist_unchanged=dist_pin,source_unchanged=True)
    except BaseException as e: result['failure'] = repr(e)
    finally:
        if (out/'commands.json').exists(): result['commands_sha256'] = sha(out/'commands.json')
        s.write(out/'result.json',result)
    print(result['status'],out/'result.json',flush=True); return 0 if result['status'].startswith('PASS_') else 1
if __name__=='__main__': raise SystemExit(main())
