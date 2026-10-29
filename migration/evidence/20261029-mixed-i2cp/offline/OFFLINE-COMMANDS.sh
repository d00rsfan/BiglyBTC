#!/usr/bin/env bash
# Root executes after ordinary native build/source gates. One serial javac job.
# No app, connected session, router, endpoint, dependency fetch or native build.
set -euo pipefail
[[ $# == 2 ]] || { echo 'Usage: OFFLINE-COMMANDS.sh ACTUAL_CURRENT_I2CP_TEST_ELF FRESH_R_BUILD_OUTPUT' >&2; exit 2; }
i2cp_test=$1
i2cp_run=$2
i2cp_repo=/home/yurii/Java/BiglyBT
i2cp_capsule=$(cd -- "$(dirname -- "$0")" && pwd)
i2cp_official="$i2cp_repo/build/i2p-datagram-codec-20261029/amendments/actual-offline-interop/dependencies/i2p-2.13.0.jar"
i2cp_modified="$i2cp_repo/build/i2p-modified-core-20261029/azi2pcore_2.13.0.1.jar"
i2cp_java=/usr/lib/jvm/java-17-openjdk-amd64/bin/java
i2cp_javac=/usr/lib/jvm/java-17-openjdk-amd64/bin/javac
[[ $i2cp_test == /* && -f $i2cp_test && -x $i2cp_test && ! -L $i2cp_test ]]
[[ $i2cp_run == "$i2cp_repo"/build/* && ! -e $i2cp_run && ! -L $i2cp_run && -d $(dirname -- "$i2cp_run") ]]
umask 077
mkdir -m 700 -- "$i2cp_run"
mkdir -m 700 -- "$i2cp_run/classes" "$i2cp_run/home" "$i2cp_run/tmp" "$i2cp_run/logs"
printf '%s  %s\n' \
  d76b3c4c4fa7043c2a3553ab3209954e63151dfd1a90f7c020fbc58b33c89896 "$i2cp_official" \
  b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5 "$i2cp_modified" \
  | sha256sum --check --strict > "$i2cp_run/reference-inputs.txt"
i2cp_inputs=("$i2cp_test" "$i2cp_java" "$i2cp_javac" "$i2cp_official" "$i2cp_modified" "$i2cp_capsule/interop/I2cpVectors.java")
sha256sum -- "${i2cp_inputs[@]}" > "$i2cp_run/inputs-before.sha256"
i2cp_command() {
  local i2cp_log=$1
  shift
  printf '%q ' "$@" >> "$i2cp_run/commands.txt"
  printf '\n' >> "$i2cp_run/commands.txt"
  env -i PATH=/usr/bin:/bin HOME="$i2cp_run/home" TMPDIR="$i2cp_run/tmp" LC_ALL=C.UTF-8 TZ=UTC \
    /usr/bin/nice -n 5 "$@" > "$i2cp_run/logs/$i2cp_log" 2>&1
}
i2cp_command javac.log "$i2cp_javac" --release 8 -cp "$i2cp_official" -d "$i2cp_run/classes" "$i2cp_capsule/interop/I2cpVectors.java"
for i2cp_reference in official modified; do
  i2cp_jar=$i2cp_official
  [[ $i2cp_reference != modified ]] || i2cp_jar=$i2cp_modified
  i2cp_command "$i2cp_reference-produce.log" "$i2cp_java" \
    "-Duser.home=$i2cp_run/home" "-Djava.io.tmpdir=$i2cp_run/tmp" \
    "-Xlog:class+load=info:file=$i2cp_run/$i2cp_reference-produce-classes.log" \
    -cp "$i2cp_run/classes:$i2cp_jar" I2cpVectors produce "$i2cp_run/$i2cp_reference"
  i2cp_command "$i2cp_reference-native.log" "$i2cp_test" --interop-dir "$i2cp_run/$i2cp_reference"
  i2cp_command "$i2cp_reference-verify.log" "$i2cp_java" \
    "-Duser.home=$i2cp_run/home" "-Djava.io.tmpdir=$i2cp_run/tmp" \
    "-Xlog:class+load=info:file=$i2cp_run/$i2cp_reference-verify-classes.log" \
    -cp "$i2cp_run/classes:$i2cp_jar" I2cpVectors verify "$i2cp_run/$i2cp_reference"
done
sha256sum -- "${i2cp_inputs[@]}" > "$i2cp_run/inputs-after.sha256"
cmp -- "$i2cp_run/inputs-before.sha256" "$i2cp_run/inputs-after.sha256"
find "$i2cp_run/classes" "$i2cp_run/official" "$i2cp_run/modified" -type f -print0 \
  | sort -z | xargs -0 sha256sum > "$i2cp_run/generated-files.sha256"
printf '%s\n' 'PASS actual official + modified offline I2CP message bridge (12 cases each)' > "$i2cp_run/STATUS.txt"
