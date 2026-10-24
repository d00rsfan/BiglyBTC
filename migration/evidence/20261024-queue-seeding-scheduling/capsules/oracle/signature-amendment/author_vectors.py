#!/usr/bin/env python3
"""Author explicit host-value inputs only. Never computes rank/FP expected outputs."""
from pathlib import Path
import copy,json
HERE=Path(__file__).resolve().parent
BASE_CONFIG={
'StartStopManager_iRankType':1,'StartStopManager_iMinPeersToBoostNoSeeds':1,
'StartStopManager_iMinSpeedForActiveDL':512,'StartStopManager_iMinSpeedForActiveSeeding':512,
'StartStopManager_iRankTypeSeedFallback':0,'StartStopManager_bPreferLargerSwarms':True,
'StartStopManager_iMinSeedingTime':300,'StartStopManager_iMinSeedingTimeWhenActive':900,
'StartStopManager_bAutoStart0Peers':False,'StartStopManager_iIgnoreSeedCount':0,
'StartStopManager_bIgnore0Peers':True,'Stop Ratio':'0.0',
'StartStopManager_iIgnoreShareRatioSeedStart':0,'Stop Peers Ratio':0,
'StartStopManager_iIgnoreRatioPeersSeedStart':0,'StartStopManager_iNumPeersAsFullCopy':0,
'StartStopManager_iFakeFullCopySeedStart':0,'StartStopManager_iFirstPriority_ShareRatio':500,
'StartStopManager_iFirstPriority_Type':1,'StartStopManager_iFirstPriority_SeedingMinutes':0,
'StartStopManager_iFirstPriority_DLMinutes':0,'StartStopManager_iFirstPriority_ignoreSPRatio':0,
'StartStopManager_bFirstPriority_ignore0Peer':True,'StartStopManager_iFirstPriority_ignoreIdleMinutes':0,
'StartStopManager_iTimed_MinSeedingTimeWithPeers':0,'StartStopManager_iTimed_MaxSeedingTimeWithPeers':0,
# Explicitly neutralize scheduler-only features; their effects get no equality credit.
'Light Seed Slots Reserved':0,'max downloads':4,'max active torrents':4,'min downloads':1}
BASE_FACTS=dict(state=9,position=1,complete=1,persistent=1,forceStart=0,checking=0,moving=0,
 seeds=2,peers=10,scrapeResponse=1,scrapeMillis=1,shareRatio=1000,secondsSeeding=3600,
 secondsDownloading=3600,secondsSinceLastUpload=0,startedMillis=700000,
 seedingStartedMillis=800000,downloadRate=0,uploadRate=0,transientFlags=0,
 minimumShareRatio=0,maximumShareRatio=0,lastActiveMillis=0,completedMillis=0)
cases=[]
def case(name,config=None,facts=None,*,compare=True,reason=None,wall=1000000,mono=500000):
 c=copy.deepcopy(BASE_CONFIG);c.update(config or {});f=copy.deepcopy(BASE_FACTS);f.update(facts or {})
 cases.append(dict(id=name,config=c,facts=f,wallMillis=wall,monotoneMillis=mono,
  compare_fields=['seedingRank','firstPriority'] if compare else [],noncomparison_reason=reason))
for seeds,peers in [(0,1),(0,0),(1,1),(1,2),(2,3),(3,2),(10,1),(1,10),(9999,10000),(8388609,16777216),(16777217,8388608)]:
 case(f'spratio-{seeds}-{peers}',facts={'seeds':seeds,'peers':peers})
for mode in [0,2,4]:
 for seeds,peers in [(0,0),(0,1),(1,1),(2,20),(2,21),(9999,10000),(10000,10000)]:
  case(f'rank-mode{mode}-{seeds}-{peers}',{'StartStopManager_iRankType':mode},dict(seeds=seeds,peers=peers))
for seeds in [1,2,3,4]:
 case(f'seed-fallback-{seeds}',{'StartStopManager_iRankType':2,'StartStopManager_iRankTypeSeedFallback':3},dict(seeds=seeds))
case('unknown-scrape',facts=dict(seeds=0,peers=0,scrapeMillis=0,scrapeResponse=2),compare=False,reason='Original supplies fallback0; native conservative missing-provider decisions are separate policy, zero equality credit.')
case('known-zero-ignore-disabled',{'StartStopManager_bIgnore0Peers':False},dict(seeds=0,peers=0))
for ratio in [-1,1499,1500,1501]:
 case(f'ignore-share-{ratio}',{'Stop Ratio':'1.5'},dict(shareRatio=ratio))
for seeds in [2,3]:
 case(f'ignore-share-seedgate-{seeds}',{'Stop Ratio':'1.5','StartStopManager_iIgnoreShareRatioSeedStart':3},dict(seeds=seeds,shareRatio=1500))
for seeds in [4,5,6]: case(f'ignore-seed-{seeds}',{'StartStopManager_iIgnoreSeedCount':5},dict(seeds=seeds))
for peers in [3,4,5]: case(f'ignore-peer-ratio-{peers}',{'Stop Peers Ratio':2},dict(seeds=2,peers=peers))
case('per-download-share-max',{'Stop Ratio':'5.0'},dict(maximumShareRatio=1200,shareRatio=1200))
case('per-download-share-min',facts=dict(minimumShareRatio=1200,shareRatio=1199))
for mode in [0,1]:
 for ratio in [-1,499,500,501]:
  case(f'fp-mode{mode}-share{ratio}',{'StartStopManager_iFirstPriority_Type':mode},dict(shareRatio=ratio))
for seconds in [59,60,61]:
 case(f'fp-seeding-{seconds}',{'StartStopManager_iFirstPriority_SeedingMinutes':1},dict(secondsSeeding=seconds))
 case(f'fp-active-{seconds}',{'StartStopManager_iFirstPriority_DLMinutes':1},dict(secondsSeeding=seconds,secondsDownloading=0))
for seeds in [3,4,5]:
 case(f'fp-integer-sp-{seeds}',{'StartStopManager_iFirstPriority_ignoreSPRatio':2},dict(seeds=seeds,peers=2,shareRatio=1))
for seconds in [1799,1800,1801]:
 case(f'fp-idle-{seconds}',{'StartStopManager_iFirstPriority_ignoreIdleMinutes':30},dict(shareRatio=1,secondsSinceLastUpload=seconds))
case('fp-zero-peer-override-disabled',{'StartStopManager_bFirstPriority_ignore0Peer':False},dict(seeds=0,peers=0,shareRatio=1))
for flags in [1,2,4,5]:
 case(f'fp-transient-flags{flags}',facts=dict(transientFlags=flags,persistent=0,complete=0,state=7))
for state in [7,8,9]: case(f'fp-state-{state}',facts=dict(state=state,shareRatio=1))
case('fp-not-persistent',facts=dict(persistent=0,shareRatio=1))
case('incomplete-position-rank',facts=dict(complete=0,position=7,state=4))
for seconds in [0,1,3600,199899999,199900000,2147483647]:
 case(f'timed-queued-{seconds}',{'StartStopManager_iRankType':3},dict(secondsSeeding=seconds))
for elapsed in [299999,300000,300001]:
 case(f'timed-seeding-{elapsed}',{'StartStopManager_iRankType':3},dict(state=5,seedingStartedMillis=1000000-elapsed))
case('timed-ready',{'StartStopManager_iRankType':3},dict(state=3))
case('timed-first-priority',{'StartStopManager_iRankType':3},dict(shareRatio=1))
for peers in [214748364,214748365,2147483647]:
 case(f'peercount-wrap-{peers}',{'StartStopManager_iRankType':4},dict(seeds=1,peers=peers))
assert len({x['id'] for x in cases})==len(cases)
lines=['# Host values only; no expected numeric outcomes. Generated by author_vectors.py.']
for c in cases:
 lines.append('CASE\t'+c['id'])
 for k,v in c['config'].items():
  kind='B' if isinstance(v,bool) else 'F' if isinstance(v,str) else 'I'
  lines.append('\t'.join(['CFG',k,kind,str(int(v)) if isinstance(v,bool) else str(v)]))
 for k,v in c['facts'].items():lines.append('\t'.join(['FACT',k,str(v)]))
 lines.append(f"CLOCK\t{c['wallMillis']}\t{c['monotoneMillis']}");lines.append('END')
(HERE/'vectors.tsv').write_text('\n'.join(lines)+'\n')
(HERE/'vectors.json').write_text(json.dumps({'schema':1,'contains_expected_outputs':False,'cases':cases},indent=2)+'\n')
print(json.dumps({'input_cases':len(cases),'comparison_cases':sum(bool(x['compare_fields']) for x in cases),'scalar_comparisons_planned':sum(len(x['compare_fields']) for x in cases)}))
