#!/usr/bin/env bash
set -euo pipefail

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
work=${1:?Provide a fresh absolute build directory}
mode=${2:-build}
[[ $# -le 2 && ( $mode == build || $mode == --source-only || $mode == --sdk-test-inputs-only || $mode == --desktop-inputs-only ) ]]
[[ $work == /* && ! -e $work ]]
mkdir -m 700 "$work"

archive() {
  local project=$1 commit=$2 digest=$3
  local path="$work/${project##*/}.tar.gz"
  curl --fail --location --retry 3 --proto '=https' \
    "https://codeload.github.com/$project/tar.gz/$commit" -o "$path"
  printf '%s  %s\n' "$digest" "$path" | sha256sum --check --strict
  tar --extract --gzip --no-same-owner --no-same-permissions --file "$path" --directory "$work"
}

lt=740a0b9aeabe00e762cc0efe4a0f27593db2550b
signal=105cce59972f925a33aa6b1c3109e4cd3caf583d
simulator=94dc46cccb6fec42072cb193b8a92a1dc84efca0
azmsgsync=28e5671de2f9a953fa2578ce17cdb9f6ee2516ca
mldht=9c2508f5af85f3215380cf267742a7f7f70ead19
archive arvidn/libtorrent "$lt" fa8e1544712fa3d24c4b345c120d6a85e2f36367877c34d500d355ed2a0d9e11
archive arvidn/try_signal "$signal" 6f111b0d77429a8051be4faed06cf23fb03cf6ee233967c84fdd9d8d3b42ba8e
archive arvidn/libsimulator "$simulator" 3ad4f0938e75d122fadc3468edaa15c8ca61554c67f170fb8a122e742648ce3e

source="$work/libtorrent-$lt"
rmdir "$source/deps/try_signal" "$source/simulation/libsimulator"
mv "$work/try_signal-$signal" "$source/deps/try_signal"
mv "$work/libsimulator-$simulator" "$source/simulation/libsimulator"
rm -r -- "$source/.github" "$source/deps/try_signal/.github" "$source/simulation/libsimulator/.github"
rm -- "$source/.gitignore" "$source/.gitmodules" \
  "$source/deps/try_signal/.gitignore" "$source/simulation/libsimulator/.gitignore"
patch --batch --forward --fuzz=0 -p1 -d "$source" \
  < "$repo/preview/migration/dependencies/libtorrent/daily-runtime.patch"

binding="$repo/preview/native/engine/runtime-binding"
python3 -B "$binding/verify_release_sdk.py" --source "$source"
if [[ $mode == --source-only ]]; then
  exit 0
fi

archive d00rsfan/BiglyBTC-plugin-mlDHT "$mldht" 51925b58a32b444a1417363f77e52e8c9fb601ffae457e0b79034010ca43b470
if [[ $mode == --sdk-test-inputs-only ]]; then
  exit 0
fi
archive d00rsfan/BiglyBTC-plugin-azmsgsync "$azmsgsync" defd7b07dea43ddabb62ec4815877334ecef78eb3b1dfee01d806c4282ee8de8
mkdir "$work/geoip"
geoip() {
  local database=$1 month=$2 name=$3 digest=$4
  curl --fail --location --retry 3 --proto '=https' \
    "https://download.db-ip.com/free/dbip-$database-lite-$month.mmdb.gz" -o "$work/$database.mmdb.gz"
  gzip -dc "$work/$database.mmdb.gz" > "$work/geoip/$name"
  printf '%s  %s\n' "$digest" "$work/geoip/$name" | sha256sum --check --strict
}
geoip country 2026-11 DBIP-Country.mmdb 145ce5dfb812d8de28547e6289bb5c5f8b3df2f1dea21d324d5b575e0431311f
geoip asn 2026-11 DBIP-ASN.mmdb 81565a450281c949fc83745f5ec9cfea70b035163b1152b055645884765d153d
if [[ $mode == --desktop-inputs-only ]]; then
  exit 0
fi

cmake -S "$source" -B "$work/sdk-build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/g++-16 \
  -DCMAKE_C_COMPILER=/usr/bin/gcc-16 -DCMAKE_INSTALL_PREFIX="$work/sdk" \
  -DBUILD_SHARED_LIBS=ON -Dbuild_tests=ON -Dbuild_tools=OFF
# The daily's SDK exports the peer-class API via upstream's build_tests option.
cmake --build "$work/sdk-build" --target torrent-rasterbar --parallel "${BIGLYBTC_SDK_BUILD_JOBS:-$(nproc)}"
cmake --install "$work/sdk-build"

sdk_library=$(realpath "$work/sdk/lib/libtorrent-rasterbar.so.2.0")
{
  printf 'BIGLYBTC_RELEASE_SDK_SOURCE=%s\n' "$source"
  printf 'BIGLYBTC_RELEASE_SDK_INSTALL=%s\n' "$work/sdk"
  printf 'BIGLYBTC_RELEASE_SDK_LIBRARY=%s\n' "$sdk_library"
  printf 'BIGLYBTC_RELEASE_MSGSYNC_SOURCE=%s\n' "$work/BiglyBTC-plugin-azmsgsync-$azmsgsync"
  printf 'BIGLYBTC_RELEASE_MLDHT_SOURCE=%s\n' "$work/BiglyBTC-plugin-mlDHT-$mldht"
  printf 'BIGLYBTC_RELEASE_GEOIP=%s\n' "$work/geoip"
} >> "${GITHUB_ENV:-$work/runtime.env}"
