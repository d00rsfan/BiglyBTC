#!/usr/bin/env bash
# Manual CI bootstrap, not a qualification or a frozen APT/Python snapshot.
# https://github.com/actions/runner-images/issues/14747
# https://packages.ubuntu.com/resolute/libtorrent-rasterbar-dev
# https://packages.ubuntu.com/en/resolute/libboost1.90-dev
# https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6120/qt6_6120/
set -euo pipefail
if [[ $# == 0 ]]; then
  echo "Usage: $0 --install {compiler-only|qt-core|bundled-qt|system-qt} FRESH_DIRECTORY"
  echo "Hosted Ubuntu 26.04 amd64 and arm64 installation only."
  exit 0
fi
[[ $# == 3 && $1 == --install ]] || exit 2
variant=$2
destination=$3
case "$variant" in compiler-only|qt-core|bundled-qt|system-qt) ;; *) exit 2 ;; esac
[[ ${GITHUB_ACTIONS:-} == true && ${RUNNER_OS:-} == Linux ]] || exit 2
case "${RUNNER_ARCH:-}" in
  X64)
    architecture=amd64
    apt_mirror=https://archive.ubuntu.com/ubuntu
    qt_host=linux_x64
    aqt_host=linux
    qt_arch=linux_gcc_64
    qt_directory=gcc_64
    metadata_sha=615f617ad82e9c9b50e339ce19eb340901919ea29386a93843be236cb77ca5a7
    ;;
  ARM64)
    architecture=arm64
    apt_mirror=https://ports.ubuntu.com/ubuntu-ports
    qt_host=linux_arm64
    aqt_host=linux_arm64
    qt_arch=linux_gcc_arm64
    qt_directory=gcc_arm64
    metadata_sha=9ad3f6beb1ac93db544946ddd544bd14fbe90ffae27953f70edcde1f59a8111c
    ;;
  *) exit 2 ;;
esac
source /etc/os-release
[[ $ID == ubuntu && $VERSION_ID == 26.04 && $(dpkg --print-architecture) == "$architecture" ]] || exit 2
[[ $destination == /* && $destination != *$'\n'* && $destination != *$'\r'* && ! -e $destination ]] || exit 2
mkdir -m 700 "$destination"
exec > >(tee "$destination/bootstrap.log") 2>&1
compiler_version=16-20260322-1ubuntu1
packages=("g++-16=$compiler_version" "libstdc++-16-dev=$compiler_version"
  ca-certificates curl cmake ninja-build pkgconf dpkg-dev binutils git python3-venv zlib1g-dev libssl-dev)
if [[ $variant == bundled-qt || $variant == system-qt ]]; then
  packages+=(libboost1.90-dev=1.90.0-6ubuntu1 libtorrent-rasterbar-dev=2.0.12-1 libtorrent-rasterbar2.0t64=2.0.12-1 libmaxminddb-dev libarchive-dev)
fi
if [[ $variant != compiler-only ]]; then
  packages+=(bubblewrap iproute2 util-linux libgl1-mesa-dev libegl1-mesa-dev libxkbcommon-x11-0 libxcb-cursor0
    libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0 libxcb-xinerama0
    libxcb-xkb1 libxcb-shape0 libx11-xcb1 libfontconfig1 libfreetype6 libdbus-1-3
    libglib2.0-0t64 libgtk-3-0t64 libwayland-client0 libwayland-cursor0 libwayland-egl1)
fi
# The runner mirror list can prefer an Azure mirror that stalls large pinned
# compiler downloads. Keep its original contents and use Ubuntu's canonical
# HTTPS archive; package versions and APT signature verification stay unchanged.
if [[ -e /etc/apt/apt-mirrors.txt ]]; then
  [[ -f /etc/apt/apt-mirrors.txt && ! -L /etc/apt/apt-mirrors.txt ]] || { echo "Unexpected APT mirror list type" >&2; exit 1; }
  cp /etc/apt/apt-mirrors.txt "$destination/apt-mirrors-before.txt"
  printf '%s\n' "$apt_mirror" | sudo tee /etc/apt/apt-mirrors.txt >/dev/null
  cp /etc/apt/apt-mirrors.txt "$destination/apt-mirrors-selected.txt"
fi
apt_options=(-o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30 -o Acquire::Retries=2)
sudo timeout --signal=TERM --kill-after=15s 180s apt-get "${apt_options[@]}" -o APT::Update::Error-Mode=any update
if [[ $variant == system-qt ]]; then
  packages+=(qt6-base-dev qt6-base-dev-tools qt6-qpa-plugins qt6-wayland)
fi
printf '%s\n' "${packages[@]}" > "$destination/requested-packages.txt"
sudo env DEBIAN_FRONTEND=noninteractive timeout --signal=TERM --kill-after=15s 600s apt-get "${apt_options[@]}" install --yes --no-install-recommends "${packages[@]}"
dpkg-query -W -f='${binary:Package}\t${Version}\n' > "$destination/installed-packages.tsv"
/usr/bin/g++-16 --version | tee "$destination/compiler.txt"
grep -F '16.0.1 20260322' "$destination/compiler.txt" >/dev/null
grep -F 'r16-8246-g569ace1fa50' "$destination/compiler.txt" >/dev/null
qt_prefix=/usr
if [[ $variant == bundled-qt || $variant == qt-core ]]; then
  # Installer wheel pinned by SHA256; aqt retains its normal archive hash checks.
  # OS/Python transitive versions and actual Qt archive SHA256s are recorded.
  wheel=https://files.pythonhosted.org/packages/7f/70/849fa1005fa197fc28a744fd69598ab9da883b737a1ab9461d88721ddd0b/aqtinstall-3.3.0-py3-none-any.whl
  wheel_sha=e88dbd87226f276fdd5d05347a44578d390d93a7f176e9476fbda0a7c9635f69
  metadata=https://download.qt.io/online/qtsdkrepository/$qt_host/desktop/qt6_6120/qt6_6120/Updates.xml
  curl --fail --location --retry 3 --proto '=https' --tlsv1.2 "$wheel" -o "$destination/aqtinstall-3.3.0-py3-none-any.whl"
  printf '%s  %s\n' "$wheel_sha" "$destination/aqtinstall-3.3.0-py3-none-any.whl" | sha256sum --check --strict
  curl --fail --location --retry 3 --proto '=https' --tlsv1.2 "$metadata" -o "$destination/qt-Updates.xml"
  printf '%s  %s\n' "$metadata_sha" "$destination/qt-Updates.xml" | sha256sum --check --strict
  python3 -m venv "$destination/installer"
  "$destination/installer/bin/python" -m pip install "$destination/aqtinstall-3.3.0-py3-none-any.whl"
  "$destination/installer/bin/python" -m pip freeze --all > "$destination/python-packages.txt"
  mkdir "$destination/archives"
  archives=(qtbase icu)
  modules=()
  if [[ $variant == bundled-qt ]]; then
    archives+=(qtsvg qtwayland qttranslations)
    modules=(--modules qtimageformats)
  fi
  "$destination/installer/bin/python" -m aqt install-qt "$aqt_host" desktop 6.12.0 "$qt_arch" \
    --base https://download.qt.io --outputdir "$destination/Qt" \
    --archives "${archives[@]}" "${modules[@]}" --keep --archive-dest "$destination/archives"
  find "$destination/archives" -type f -print0 | sort -z | xargs -0 -r sha256sum > "$destination/qt-archives-SHA256SUMS"
  qt_prefix="$destination/Qt/6.12.0/$qt_directory"
  [[ -f $qt_prefix/lib/cmake/Qt6Core/Qt6CoreConfig.cmake ]]
  curl --fail --location --retry 3 --proto '=https' --tlsv1.2 "$metadata" -o "$destination/qt-Updates-after.xml"
  cmp "$destination/qt-Updates.xml" "$destination/qt-Updates-after.xml"
fi
{
  printf 'BIGLYBTC_CXX=/usr/bin/g++-16\n'
  printf 'BIGLYBTC_QT_PREFIX=%s\n' "$qt_prefix"
  printf 'BIGLYBTC_DEPENDENCY_EVIDENCE=%s\n' "$destination"
} | tee "$destination/github.env" >> "$GITHUB_ENV"
