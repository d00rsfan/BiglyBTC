#!/usr/bin/env bash
# ROOT-RUN ONLY after ordinary native build/source gates. No app/router/network.
# One serial javac job; reuses its class for both exact reference JARs.
set -euo pipefail
[[ $# == 2 ]] || { echo 'Usage: OFFLINE-COMMANDS.sh ACTUAL_CURRENT_IDENTITY_TEST_ELF FRESH_ABSOLUTE_OUTPUT' >&2; exit 2; }
identity_test=$1
identity_run=$2
identity_repo=/home/yurii/Java/BiglyBT
identity_native=/home/yurii/Java/BiglyBTC
identity_capsule=$(cd -- "$(dirname -- "$0")" && pwd)
identity_official="$identity_repo/build/i2p-datagram-codec-20261029/amendments/actual-offline-interop/dependencies/i2p-2.13.0.jar"
identity_modified="$identity_repo/build/i2p-modified-core-20261029/azi2pcore_2.13.0.1.jar"
identity_fixtures="$identity_native/preview/native/tests/fixtures/i2p-datagram"
identity_java=/usr/lib/jvm/java-17-openjdk-amd64/bin/java
identity_javac=/usr/lib/jvm/java-17-openjdk-amd64/bin/javac
[[ $identity_test == /* && -f $identity_test && -x $identity_test && ! -L $identity_test ]]
[[ $identity_run == "$identity_repo"/build/* && ! -e $identity_run && ! -L $identity_run ]]
[[ -d $(dirname -- "$identity_run") ]]
umask 077
mkdir -m 700 -- "$identity_run"
mkdir -m 700 -- "$identity_run/classes" "$identity_run/home" "$identity_run/tmp" "$identity_run/logs"
printf '%s  %s\n' \
  d76b3c4c4fa7043c2a3553ab3209954e63151dfd1a90f7c020fbc58b33c89896 "$identity_official" \
  b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5 "$identity_modified" \
  | sha256sum --check --strict > "$identity_run/reference-inputs.txt"
identity_inputs=("$identity_test" "$identity_java" "$identity_javac" "$identity_official" "$identity_modified" "$identity_capsule/interop/ClientIdentityVectors.java")
for identity_family in dsa ed25519; do
  for identity_leaf in sender recipient known-test-key; do
    identity_inputs+=("$identity_fixtures/$identity_family.$identity_leaf")
  done
done
sha256sum -- "${identity_inputs[@]}" > "$identity_run/inputs-before.sha256"
identity_command() {
  local identity_log=$1
  shift
  printf '%q ' "$@" >> "$identity_run/commands.txt"
  printf '\n' >> "$identity_run/commands.txt"
  env -i PATH=/usr/bin:/bin HOME="$identity_run/home" TMPDIR="$identity_run/tmp" LC_ALL=C.UTF-8 TZ=UTC \
    /usr/bin/nice -n 5 "$@" > "$identity_run/logs/$identity_log" 2>&1
}
identity_command javac.log "$identity_javac" --release 8 -cp "$identity_official" -d "$identity_run/classes" "$identity_capsule/interop/ClientIdentityVectors.java"
for identity_reference in official modified; do
  identity_jar=$identity_official
  [[ $identity_reference != modified ]] || identity_jar=$identity_modified
  identity_command "$identity_reference-produce.log" "$identity_java" \
    "-Duser.home=$identity_run/home" "-Djava.io.tmpdir=$identity_run/tmp" \
    "-Xlog:class+load=info:file=$identity_run/$identity_reference-produce-classes.log" \
    -cp "$identity_run/classes:$identity_jar" ClientIdentityVectors produce "$identity_fixtures" "$identity_run/$identity_reference"
  identity_command "$identity_reference-native.log" "$identity_test" "$identity_fixtures" --interop-dir "$identity_run/$identity_reference"
  identity_command "$identity_reference-verify.log" "$identity_java" \
    "-Duser.home=$identity_run/home" "-Djava.io.tmpdir=$identity_run/tmp" \
    "-Xlog:class+load=info:file=$identity_run/$identity_reference-verify-classes.log" \
    -cp "$identity_run/classes:$identity_jar" ClientIdentityVectors verify "$identity_fixtures" "$identity_run/$identity_reference"
done
sha256sum -- "${identity_inputs[@]}" > "$identity_run/inputs-after.sha256"
cmp -- "$identity_run/inputs-before.sha256" "$identity_run/inputs-after.sha256"
find "$identity_run/classes" "$identity_run/official" "$identity_run/modified" -type f -print0 \
  | sort -z | xargs -0 sha256sum > "$identity_run/generated-files.sha256"
printf '%s\n' 'PASS actual official + modified offline identity import/export/signature cases (4 each)' > "$identity_run/STATUS.txt"
