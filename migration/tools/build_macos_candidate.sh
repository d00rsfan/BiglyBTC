#!/usr/bin/env bash
set -euo pipefail

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
inputs=${1:?Provide the prepared desktop inputs}
downloads=${2:?Provide an existing output directory}
[[ $# -eq 2 && $inputs == /* && $downloads == /* && -d $inputs && -d $downloads ]]
[[ $(uname -s) == Darwin && $(uname -m) == arm64 ]]
work=$(mktemp -d "${RUNNER_TEMP:-/tmp}/biglybtc-mac-desktop.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
sdk_sources=("$inputs"/libtorrent-*/)
signer_sources=("$inputs"/BiglyBTC-plugin-azmsgsync-*/)
identity_sources=("$inputs"/BiglyBTC-plugin-mlDHT-*/)
[[ ${#sdk_sources[@]} == 1 && -d ${sdk_sources[0]} ]]
[[ ${#signer_sources[@]} == 1 && -d ${signer_sources[0]} ]]
[[ ${#identity_sources[@]} == 1 && -d ${identity_sources[0]} ]]
source=${sdk_sources[0]%/}
python3 -B "$repo/preview/native/engine/runtime-binding/verify_release_sdk.py" --source "$source"

fetch() {
  local url=$1 output=$2 expected=$3
  curl --fail --location --retry 3 --proto '=https' --tlsv1.2 "$url" -o "$output"
  local checksum
  checksum=$(shasum -a 256 < "$output")
  [[ ${checksum%% *} == "$expected" ]]
}

for dependency in boost libarchive libmaxminddb; do
  brew list --versions "$dependency" || brew install "$dependency"
done
fetch https://github.com/openssl/openssl/releases/download/openssl-3.5.5/openssl-3.5.5.tar.gz \
  "$work/openssl.tar.gz" b28c91532a8b65a1f983b4c28b7488174e4a01008e29ce8e69bd789f28bc2a89
tar -xzf "$work/openssl.tar.gz" -C "$work"
(
  cd "$work/openssl-3.5.5"
  ./Configure darwin64-arm64-cc no-shared no-tests --prefix="$work/openssl" --libdir=lib
  make -j3
  make install_sw
)

wheel=aqtinstall-3.3.0-py3-none-any.whl
fetch "https://files.pythonhosted.org/packages/7f/70/849fa1005fa197fc28a744fd69598ab9da883b737a1ab9461d88721ddd0b/$wheel" \
  "$work/$wheel" e88dbd87226f276fdd5d05347a44578d390d93a7f176e9476fbda0a7c9635f69
qt_metadata=https://download.qt.io/online/qtsdkrepository/mac_x64/desktop/qt6_6120/qt6_6120/Updates.xml
qt_metadata_sha=0932ba7a2c1f2b9978cb6199527dd4abb85a95791ce8afc72b7a1cf53943466c
fetch "$qt_metadata" "$work/qt-Updates.xml" "$qt_metadata_sha"
python3 -m venv "$work/installer"
"$work/installer/bin/python" -m pip install "$work/$wheel"
"$work/installer/bin/python" -m aqt install-qt mac desktop 6.12.0 clang_64 \
  --base https://download.qt.io --outputdir "$work/Qt" --archives qtbase qtsvg --modules qtimageformats
fetch "$qt_metadata" "$work/qt-Updates-after.xml" "$qt_metadata_sha"
qt="$work/Qt/6.12.0/macos"
[[ -x $qt/bin/macdeployqt ]]

cmake_common=(
  -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
  "-DOPENSSL_ROOT_DIR=$work/openssl" -DOPENSSL_USE_STATIC_LIBS=TRUE
)
deps="$(brew --prefix boost);$(brew --prefix libarchive);$(brew --prefix libmaxminddb)"
cmake -S "$source" -B "$work/sdk-build" "${cmake_common[@]}" \
  "-DCMAKE_PREFIX_PATH=$deps" "-DCMAKE_INSTALL_PREFIX=$work/sdk" \
  -DCMAKE_CXX_STANDARD=20 -DCMAKE_CXX_STANDARD_REQUIRED=ON \
  -DBUILD_SHARED_LIBS=ON -Dbuild_tests=ON -Dbuild_tools=OFF
cmake --build "$work/sdk-build" --target torrent-rasterbar --parallel 3
cmake --install "$work/sdk-build"
cmake -S "$repo/preview/native" -B "$work/desktop" "${cmake_common[@]}" \
  "-DCMAKE_PREFIX_PATH=$work/sdk;$qt;$deps" -DBUILD_TESTING=OFF -DBIGLYBTC_INSTALL_CORE=OFF \
  -DBIGLYBTC_DESKTOP=ON -DBIGLYBTC_LOCAL_ENGINE=ON \
  "-DBIGLYBTC_RELEASE_SDK_SOURCE=$source" \
  "-DBIGLYBTC_SHARED_UDP_LIBRARY=$work/sdk/lib/libtorrent-rasterbar.2.0.dylib" \
  "-DBIGLYBTC_MSGSYNC_SIGNER_SOURCE_DIR=${signer_sources[0]%/}" \
  "-DBIGLYBTC_MLDHT_IDENTITY_SOURCE_DIR=${identity_sources[0]%/}" \
  "-DBIGLYBTC_RELEASE_GEOIP=$inputs/geoip" \
  "-DBIGLYBTC_MACOS_VENDOR_ROOTS=$(brew --cellar)" \
  "-DBIGLYBTC_MACDEPLOYQT=$qt/bin/macdeployqt"
python3 -B "$repo/preview/native/packaging/test_mac_bundle.py"
cmake --build "$work/desktop" --target biglybtc-macos-dmg --parallel 3
images=("$work/desktop"/BiglyBTC-*-macos-arm64.dmg)
[[ ${#images[@]} == 1 && -f ${images[0]} ]]
python3 -B "$repo/preview/native/packaging/smoke_macos_dmg.py" "${images[0]}"
cp "${images[0]}" "$downloads/"
(cd "$downloads" && shasum -a 256 "${images[0]##*/}" > SHA256SUMS)
