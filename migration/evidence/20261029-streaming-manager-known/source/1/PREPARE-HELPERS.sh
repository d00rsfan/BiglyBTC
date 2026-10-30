#!/usr/bin/env bash
# Root-only source recipe. One javac invocation; no Java/router/native runtime.
set -euo pipefail
test "$#" -eq 3 || { echo 'OUT_ABSOLUTE JAVA_ABSOLUTE JAVAC_ABSOLUTE required' >&2; exit 2; }
out=$1
java=$2
javac=$3
case "$out" in /home/yurii/Java/BiglyBT/build/*) ;; *) exit 2 ;; esac
test "$(readlink -m -- "$out")" = "$out"
test ! -e "$out"
test "${java:0:1}" = / && test "${javac:0:1}" = /
task=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
legacy=/home/yurii/Java/BiglyBT/build/i2p-streaming-window-20261029/acceptance/StreamingPeer.java
jar=/home/yurii/Java/BiglyBT/build/i2p-modified-core-20261029/azi2pcore_2.13.0.1.jar
mkdir -m 700 -- "$out"
mkdir -m 700 -- "$out/all" "$out/legacy" "$out/manager" "$out/home" "$out/tmp"
unset JAVA_TOOL_OPTIONS _JAVA_OPTIONS JDK_JAVA_OPTIONS CLASSPATH LD_DEBUG LD_DEBUG_OUTPUT ASAN_OPTIONS LSAN_OPTIONS UBSAN_OPTIONS
export HOME="$out/home" TMPDIR="$out/tmp" XDG_CONFIG_HOME="$out/home" XDG_CACHE_HOME="$out/home"
python3 - "$out" "$java" "$javac" "$jar" "$legacy" "$task/ManagerPeer.java" "$0" <<'PY'
import hashlib,json,pathlib,sys
out,*files=map(pathlib.Path,sys.argv[1:])
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
pins={str(p.resolve()):{'sha256':sha(p.resolve()),'bytes':p.resolve().stat().st_size} for p in files}
assert sha(files[2])=='b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5'
assert sha(files[3])=='8c78205c17273122b381b30cb8ad6aadbd59105dfccbaad9fed0df5ac351e8cd'
(out/'inputs-before.json').write_text(json.dumps(pins,indent=2)+'\n')
PY
"$java" -version >"$out/java-version.log" 2>&1
"$javac" -version >"$out/javac-version.log" 2>&1
nice -n 5 "$javac" -J-Xms16m -J-Xmx128m -J-XX:ActiveProcessorCount=1 -J-XX:+UseSerialGC \
  -J-Duser.home="$HOME" -J-Djava.io.tmpdir="$TMPDIR" --release 17 -cp "$jar" \
  -d "$out/all" "$legacy" "$task/ManagerPeer.java" >"$out/javac.log" 2>&1
python3 - "$out" "$java" "$javac" "$jar" "$legacy" "$task/ManagerPeer.java" "$0" <<'PY'
import hashlib,json,pathlib,shutil,sys
out,java,javac,jar,legacy,manager,recipe=map(pathlib.Path,sys.argv[1:])
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(jar)=='b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5'
assert sha(legacy)=='8c78205c17273122b381b30cb8ad6aadbd59105dfccbaad9fed0df5ac351e8cd'
expected={'StreamingPeer.class','StreamingPeer$Adapter.class','ManagerPeer.class','ManagerPeer$Adapter.class'}
actual={str(p.relative_to(out/'all')):sha(p) for p in (out/'all').rglob('*.class')}
assert set(actual)==expected
for name in expected:
 target=out/('legacy' if name.startswith('StreamingPeer') else 'manager')/name
 shutil.copyfile(out/'all'/name,target);assert sha(target)==actual[name]
files={str(p.resolve()):{'sha256':sha(p.resolve()),'bytes':p.resolve().stat().st_size} for p in (java,javac,jar,legacy,manager,recipe)}
assert files==json.loads((out/'inputs-before.json').read_text())
result={'status':'PASS_PREPARED_ACTUAL_MANAGER_HELPERS','javac_invocations':1,'subjects_executed':False,'files':files,'inputs_before_sha256':sha(out/'inputs-before.json'),'classes':actual,'legacy_classes':str(out/'legacy'),'manager_classes':str(out/'manager'),'logs_sha256':{p.name:sha(p) for p in out.glob('*.log')}}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
PY
