#!/usr/bin/env python3
"""Root-run: one private candidate, changed tracker/Tor/console methods; no build/promotion."""
import argparse, hashlib, importlib.util, json, os, re, shlex, shutil, sys
from pathlib import Path
import xml.etree.ElementTree as ET
sys.dont_write_bytecode = True
R = Path('/home/yurii/Java/BiglyBT'); N = R.parent/'BiglyBTC'; H = Path(__file__).resolve().parent
SUPPORT = R/'build/mainline-scrape-20261028/recipe_support_followup.py'
PLAN = R/'build/mainline-host-cohort-20261028/amendments/provider-r2-recipes/private/plan.json'
QT_PROOF = R/'build/mainline-host-cohort-20261028/amendments/provider-r2-recipes/qualification/runs/gcc-r4/runtime-images-before.json'
PINS = {SUPPORT:'64e0bd2b9ec87b9babf90bdf8d1235746f7dc63c797c82c5e5a138a4c2ea893a', PLAN:'2917d15b4d9890f6d881fb18b46a65c713a2a0a3dfe9c9996487d4deb6a302f3', QT_PROOF:'bacbdf657be1cfd5b9e531a3a5aa6325eb24b171baf2df33021e341e68a5ac9e'}
SELECTIONS = [
 ('biglybtc-mainline-single-http-tests','savedTrackerTiersFailOverToActualSecondHttpAndStopRevokesHeldReply', ['savedTrackerTiersFailOverToActualSecondHttpAndStopRevokesHeldReply(ordered-tiers)','savedTrackerTiersFailOverToActualSecondHttpAndStopRevokesHeldReply(stable-shuffled-tier)']),
 ('biglybtc-tor-owned-child-tests','postReadyControlLossWithdrawsBeforeChildExitAndAllowsExplicitRestart',['postReadyControlLossWithdrawsBeforeChildExitAndAllowsExplicitRestart(control-eof)','postReadyControlLossWithdrawsBeforeChildExitAndAllowsExplicitRestart(unsupported-post-setup-event)','postReadyControlLossWithdrawsBeforeChildExitAndAllowsExplicitRestart(control-eof-kill-escalation)']),
 ('biglybtc-tor-owned-child-tests','postReadyControlLossSupportsReentrantStopAndDeletion',['postReadyControlLossSupportsReentrantStopAndDeletion']),
 ('biglybtc-preview-smoke','exactWaylandNoFocusWarningKeepsOtherDiagnosticsAndForwardHandler',['exactWaylandNoFocusWarningKeepsOtherDiagnosticsAndForwardHandler']),
 ('biglybtc-preview-smoke','explicitQtConsoleControlsDisableNativeFiltering',['explicitQtConsoleControlsDisableNativeFiltering']),
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
    'owned-tor-child-lifecycle':'biglybtc-tor-owned-child-tests'}
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

def validate_prior_origin(prior, lane, audit, digest, read):
    entry=audit['lanes'][lane]; pins=entry['evidence_sha256']
    assert {'tests.xml','ctest-full.log','result.json','commands.json','images-before.json','source-before.json'}<=set(pins)
    for name,pin in pins.items(): assert digest(prior/name)==pin, 'Prior evidence changed '+name
    wanted={'queue-policy','network-policy','mainline-dht'} | (set() if lane=='clang' else {'mainline-single-http','mainline-single-udp','mainline-single-tracker-owner','owned-tor-child-lifecycle'})
    failed={'queue-policy'} | (set() if lane=='clang' else {'owned-tor-child-lifecycle'})
    rows=list(ET.parse(prior/'tests.xml').getroot().iter('testcase'))
    assert len(rows)==len(wanted) and {row.get('name') for row in rows}==wanted, 'Prior exact inventory differs'
    assert not any(row.find(tag) is not None for row in rows for tag in ('error','skipped')), 'Prior error/skip'
    assert {row.get('name') for row in rows if row.find('failure') is not None}==failed
    passed=[]
    for row in rows:
        if row.get('name') in failed: assert row.get('status')=='fail'
        else: assert row.get('status')=='run' and row.find('failure') is None; passed.append(row.get('name'))
    assert sorted(passed)==sorted(entry['passed_suites']) and sorted(failed)==sorted(entry['failed_suites'])
    old=read(prior/'result.json'); oldsource=read(prior/'source-before.json'); images=read(prior/'images-before.json')
    assert old['status']=='FAIL' and old['source_commit']==oldsource['source_commit']==audit['source_commit']
    assert entry['all_images_unchanged'] and digest(prior/'source-before.json')==audit['source_snapshot_sha256']
    blocks=actual_suite_commands(prior/'ctest-full.log',wanted,images)
    for name in passed:
        assert not re.search(r'^(?:SKIP\s+:|FAIL!)','\n'.join(blocks[name]),re.M), 'Skipped/failed inner method '+name
    return passed,images

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
    results = {}; image_maps = {}; authorities = {}
    for lane in ('gcc','clang','san'):
        path = getattr(a,lane+'_result').resolve(); need(sha(path)==getattr(a,lane+'_sha256'), 'Result changed '+lane); d = load(path)
        wanted = {'queue-policy','network-policy','mainline-dht'} | (set() if lane=='clang' else {'mainline-single-http','mainline-single-udp','mainline-single-tracker-owner','owned-tor-child-lifecycle'})
        fresh=['queue-policy']+(['owned-tor-child-lifecycle'] if lane!='clang' else [])
        need(d['status']=='PASS_FOCUSED_NETWORK_UI_REPAIR_IDENTICAL_OTHER_IMAGES' and d['lane']==lane and d['source_commit']==a.source_commit, 'Actual focused PASS required '+lane)
        need(d['source_unchanged'] and d['images_unchanged'] and d['actual_suites']==len(fresh) and set(d['suite_names'])==set(fresh) and set(d['combined_covered_suites'])==wanted and d['all_other_source_bytes_unchanged'] and d['all_other_images_unchanged'], 'Scope differs '+lane)
        need(d['compile_only_suites']==['mainline-original-host-identity']+(['mainline-single-http','mainline-single-udp','mainline-single-tracker-owner','owned-tor-child-lifecycle'] if lane=='clang' else []), 'Compile-only scope differs '+lane)
        need(d['provider_sha256']==provider['artifact_sha256'], 'Focused provider differs')
        need(load(path.parent/'source-before.json')==source, 'Focused source differs '+lane)
        need(sha(path.parent/'tests.xml')==d['tests_xml_sha256'], 'Focused XML changed '+lane); s.passed_xml(path.parent/'tests.xml',fresh)
        need(not re.search(r'^SKIP\s*:',(path.parent/'tests.xml').read_text(),re.M), 'Skipped focused Qt method')
        need(sha(path.parent/'ctest-full.log')==d['full_ctest_log_sha256'], 'Full focused log changed')
        need(sha(H.parent/'qualify-test-fix.py')==d['runner_sha256'], 'Focused runner changed')
        need(sha(d['source_receipt'])==d['source_receipt_sha256'], 'Source receipt changed')
        adoption=load(d['source_receipt']); need(adoption['source_commit']==a.source_commit and adoption['source_path_count']==len(adoption['source_paths'])==20, 'Full root source scope differs')
        for name,row in adoption['source_paths'].items(): need(sha(N/name)==row['after_sha256'], 'Full adopted source changed '+name)
        need(sha(H.parent/'r1-failure-audit.json')==d['prior_failure_audit_sha256'], 'Identical-image qualification audit changed')
        need(sha(path.parent/'commands.json')==d['commands_sha256'], 'Focused commands changed')
        commands = load(path.parent/'commands.json'); need(all(row.get('exit')==0 and 'exception' not in row for row in commands), 'Incomplete actual command')
        images = load(path.parent/'images-before.json')
        prior=H.parent/'runs'/(lane+'-r1'); audit=load(H.parent/'r1-failure-audit.json')
        need(audit['status']=='ACTUAL_QUEUE_TOR_FAILURES_WITH_UNCHANGED_IMAGES', 'Wrong origin audit')
        passed,origin_images=validate_prior_origin(prior,lane,audit,sha,load)
        need(sorted(passed)==sorted(d['identical_image_passed_suites']) and sorted([*fresh,*passed])==sorted(d['combined_covered_suites']), 'Combined origin scope differs')
        for name,row in origin_images.items():
            if name not in {'biglybtc-queue-policy-tests','biglybtc-tor-owned-child-tests'}: need(images[name]==row, 'Unchanged origin ELF differs '+name)
        actual_suite_commands(path.parent/'ctest-full.log',fresh,images)
        need(not re.search(r'^(?:SKIP\s+:|FAIL!)',(path.parent/'ctest-full.log').read_text(errors='replace'),re.M), 'Skipped/failed fresh inner method')
        need(sha(path.parent/'wayland-smoke.xml')==d['wayland_smoke_xml_sha256'], 'Wayland XML changed'); s.passed_xml(path.parent/'wayland-smoke.xml')
        if lane!='clang':
            need(sha(path.parent/'real-offline-tor.xml')==d['real_tor_xml_sha256'] and sha(path.parent/'real-tor-observed.json')==d['real_tor_observed_sha256'], 'Actual real Tor receipts changed')
            real=s.passed_xml(path.parent/'real-offline-tor.xml'); need(len(real)==3 and any('extractedTorAuthenticatesOfflineThenStopsAndCancelsAnotherActualChild' in name for name in real), 'Missing actual real Tor case')
            observed=load(path.parent/'real-tor-observed.json'); need(observed['status']=='PASS_REAL_EXTRACTED_OFFLINE_TOR' and observed['joined'] and observed['runtime_artifact_sha256']=='25f0d75d2a2cc482b3e912457099ee666622444a0ae714151257844ca5987356', 'Wrong real Tor observation')
        for image in images.values(): need(sha(image['path'])==image['sha256'], 'Qualified image changed')
        image_maps[lane] = images; results[lane] = d
        authorities[lane] = {'path':str(path),'sha256':sha(path),'images_sha256':sha(path.parent/'images-before.json'),'source_before_sha256':sha(path.parent/'source-before.json')}
    out = H/'runs'/a.attempt; out.mkdir(parents=True,exist_ok=False); env = s.environment('gcc')
    for key in ('ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','LD_DEBUG_OUTPUT','BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_PREVIEW_CAPTURE','BIGLYBTC_HTTP_PRIVATE_RUNTIME','QT_TLS_BACKEND','QML_IMPORT_PATH','QML2_IMPORT_PATH','QT_QPA_PLATFORMTHEME'): env.pop(key,None)
    for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
        p = out/key.lower(); p.mkdir(mode=0o700); env[key] = str(p)
    runner = s.Runner(out,env); candidate = out/'candidate'; result = {'status':'FAIL','source_commit':a.source_commit,'focused_results':authorities,'builds_executed':False,'promotion_executed':False,'historical_runtime_passes_pooled':False}
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
            need((private/'qt/lib/libQt6Core.so.6').resolve() in loaded, 'Actual private QtCore missing '+label)
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
        result.update(status='PASS_PRIVATE_FOCUSED_NETWORK_UI',candidate=str(candidate),candidate_manifest_sha256=staged_pin,actual_leaf_count=len(staged['files_sha256']),copied_images={target:image_maps['gcc'][target] for target in copies},actual_tests=actual,provider_sha256=provider['artifact_sha256'],old_dist_unchanged=dist_pin,source_unchanged=True)
    except BaseException as e: result['failure'] = repr(e)
    finally:
        if (out/'commands.json').exists(): result['commands_sha256'] = sha(out/'commands.json')
        s.write(out/'result.json',result)
    print(result['status'],out/'result.json',flush=True); return 0 if result['status'].startswith('PASS_') else 1
if __name__=='__main__': raise SystemExit(main())
