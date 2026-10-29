#!/usr/bin/env bash
# Root-only offline original-object recipe. No I2P session/router/endpoints.
set -euo pipefail
umask 077
if [[ $# -ne 5 ]]; then
  echo 'Usage: OFFLINE-COMMANDS.sh FRESH_ABSOLUTE_OUTPUT NATIVE_TEST FIXTURES OFFICIAL_JAR MODIFIED_JAR' >&2
  exit 2
fi
out=$1
native=$2
fixtures=$3
official=$4
modified=$5
java=${JAVA:-/usr/lib/jvm/java-17-openjdk-amd64/bin/java}
javac=${JAVAC:-/usr/lib/jvm/java-17-openjdk-amd64/bin/javac}
recipe_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
[[ "$out" == /* && ! -e "$out" && -x "$native" && -d "$fixtures" ]]
printf '%s  %s\n' d76b3c4c4fa7043c2a3553ab3209954e63151dfd1a90f7c020fbc58b33c89896 "$official" | sha256sum -c -
printf '%s  %s\n' b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5 "$modified" | sha256sum -c -
mkdir -- "$out"
mkdir -- "$out/classes"
"$java" -version >"$out/java-version.log" 2>&1
"$javac" -version >"$out/javac-version.log" 2>&1
"$javac" --release 8 -cp "$official" -d "$out/classes" "$recipe_dir/interop/SendVectors.java" >"$out/javac.log" 2>&1
for name in official modified; do
  jar=$official
  if [[ "$name" == modified ]]; then jar=$modified; fi
  mkdir -- "$out/$name-base" "$out/$name-config"
  "$java" "-Xlog:class+load=info:file=$out/$name-classes-produce.log" \
    "-Di2p.dir.base=$out/$name-base" "-Di2p.dir.config=$out/$name-config" \
    -cp "$out/classes:$jar" SendVectors produce "$out/$name" "$fixtures" >"$out/$name-produce.log" 2>&1
  "$native" --java-interop "$out/$name" >"$out/$name-native.log" 2>&1
  "$java" "-Xlog:class+load=info:file=$out/$name-classes-verify.log" \
    "-Di2p.dir.base=$out/$name-base" "-Di2p.dir.config=$out/$name-config" \
    -cp "$out/classes:$jar" SendVectors verify "$out/$name" >"$out/$name-verify.log" 2>&1
done
echo 'PASS OFFLINE ORIGINAL/NATIVE DATAGRAM OBJECT COMMANDS'
