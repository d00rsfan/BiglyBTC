param(
    [Parameter(Mandatory)][ValidateSet('x64', 'arm64')][string]$Architecture,
    [Parameter(Mandatory)][string]$Inputs,
    [Parameter(Mandatory)][string]$Downloads
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$Inputs = (Resolve-Path $Inputs).Path
$Downloads = (Resolve-Path $Downloads).Path
$work = Split-Path $Inputs -Parent
$source, $signer, $identity = @('libtorrent-*', 'BiglyBTC-plugin-azmsgsync-*', 'BiglyBTC-plugin-mlDHT-*') | ForEach-Object {
    $dirs = @(Get-ChildItem $Inputs -Directory -Filter $_)
    if ($dirs.Count -ne 1) { throw "Expected one verified source directory matching $_" }
    $dirs[0].FullName
}
python -B "$repo/preview/native/engine/runtime-binding/verify_release_sdk.py" --source $source --platform windows --prepare-windows-source

$arch = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
if ($arch -ine $Architecture) { throw "Expected a native $Architecture Windows runner, got $arch" }

function Fetch([string]$Url, [string]$Path, [string]$Hash, [string]$Algorithm = 'SHA256') {
    curl.exe --silent --show-error --fail --location --retry 3 --proto '=https' --tlsv1.2 --output $Path $Url
    if ((Get-FileHash -Algorithm $Algorithm $Path).Hash -ine $Hash) { throw "Unexpected $Algorithm for $Url" }
}

$qtRoot = "$work/Qt"
New-Item -ItemType Directory $qtRoot | Out-Null
if ($Architecture -eq 'arm64') {
    $qtRepository = 'windows_arm64/desktop/qt6_6120/qt6_6120'
    $qtPackage = 'win64_msvc2022_arm64'
    $qtSuffix = 'Windows-Windows_11_23H2-MSVC2022-Windows-Windows_11_23H2-AARCH64'
    $qtHashes = [ordered]@{
        qtbase = '5dfa27cf27c32fd69955407168ed6da4529d86a7'
        qtsvg = '7378ef04e876a270493b70baca892b738bbe6a40'
        qtimageformats = 'f7cb2edf7e7756c5f881f64a3d32f44cdf928442'
        qttools = '0f3e500eae8fcd63b8c47c5a30c373d224cac9b5'
        qttranslations = '748e4239bcd33a45256a0f978ec9ede4c144adb7'
    }
    $opensslTarget = 'VC-WIN64-ARM'
    $vsPlatform = 'ARM64'
} else {
    $qtRepository = 'windows_x86/desktop/qt6_6120/qt6_6120_msvc2022_64'
    $qtPackage = 'win64_msvc2022_64'
    $qtSuffix = 'Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64'
    $qtHashes = [ordered]@{
        qtbase = 'bf35f8916164c6f313af8347dd0e91efe18178f0'
        qtsvg = 'e1c5b24e2448539684ac59dfc1a1be4de01de1d1'
        qtimageformats = '5a06cd9d22d6d5e83a1ab9a1b697020e02154881'
        qttools = '879b647b61a1da35d50132e3a51ec5895b03fb95'
        qttranslations = '65b03166eff4d8cdbc431a9089255e95c7a91e3d'
    }
    $opensslTarget = 'VC-WIN64A'
    $vsPlatform = 'x64'
}
$sevenZip = (Get-Command 7z.exe -ErrorAction SilentlyContinue).Source
if (!$sevenZip) { $sevenZip = "$env:ProgramFiles/7-Zip/7z.exe" }
if (!(Test-Path $sevenZip)) { throw '7-Zip is required to extract the official Qt archives' }
foreach ($module in $qtHashes.Keys) {
    $package = if ($module -eq 'qtimageformats') { "qt.qt6.6120.addons.qtimageformats.$qtPackage" } else { "qt.qt6.6120.$qtPackage" }
    $name = "6.12.0-0-202609280346$module-$qtSuffix.7z"
    $url = "https://download.qt.io/online/qtsdkrepository/$qtRepository/$package/$name"
    Fetch $url "$work/$name" $qtHashes[$module] 'SHA1'
    & $sevenZip x -y -aoa "-o$qtRoot" "$work/$name"
    "$(Get-FileHash -Algorithm SHA256 "$work/$name" | Select-Object -ExpandProperty Hash)  $name" |
        Add-Content -Encoding ascii "$Downloads/qt-archives-SHA256SUMS"
    Remove-Item -LiteralPath "$work/$name"
}
$qtConfigs = @(Get-ChildItem $qtRoot -Recurse -File -Filter Qt6Config.cmake)
if ($qtConfigs.Count -ne 1) { throw 'Expected exactly one extracted Qt6 prefix' }
$qt = (Resolve-Path (Join-Path $qtConfigs[0].DirectoryName '../../..')).Path
if (!(Test-Path "$qt/bin/lconvert.exe" -PathType Leaf)) {
    throw 'Qt Tools must provide lconvert.exe for windeployqt translation deployment'
}

$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$visualStudio = & $vswhere -latest -products '*' -property installationPath
if (!$visualStudio) { throw 'Visual Studio with C++ tools is required' }
$developerCommand = Join-Path $visualStudio 'Common7/Tools/VsDevCmd.bat'
$command = '"{0}" -arch={1} -host_arch={1} >nul && set' -f $developerCommand, $Architecture
$envLines = & $env:ComSpec /d /c $command
foreach ($line in $envLines) {
    $split = $line.IndexOf('=')
    if ($split -gt 0) {
        [Environment]::SetEnvironmentVariable($line.Substring(0, $split), $line.Substring($split + 1), 'Process')
    }
}

$opensslHash = 'b28c91532a8b65a1f983b4c28b7488174e4a01008e29ce8e69bd789f28bc2a89'
Fetch 'https://github.com/openssl/openssl/releases/download/openssl-3.5.5/openssl-3.5.5.tar.gz' "$work/openssl.tar.gz" $opensslHash
tar.exe -xf "$work/openssl.tar.gz" -C $work
Push-Location "$work/openssl-3.5.5"
try {
    perl Configure $opensslTarget no-shared no-asm no-tests "--prefix=$work/openssl" '--libdir=lib'
    nmake.exe /NOLOGO
    nmake.exe /NOLOGO install_sw
} finally { Pop-Location }

$boostHash = '5e93d582aff26868d581a52ae78c7d8edf3f3064742c6e77901a1f18a437eea9'
Fetch 'https://archives.boost.io/release/1.90.0/source/boost_1_90_0.tar.gz' "$work/boost.tar.gz" $boostHash
tar.exe -xf "$work/boost.tar.gz" -C $work
$boostSource = "$work/boost_1_90_0"
# Boost 1.90 bundles a bootstrapper that predates Visual Studio 2026.
$b2Hash = 'ab9ba45bc4864963da09a3a97dbfb81271dca039b20f1ef0502a38f360e3b656'
Fetch 'https://github.com/bfgroup/b2/releases/download/5.4.2/b2-5.4.2.tar.bz2' "$work/b2.tar.bz2" $b2Hash
tar.exe -xf "$work/b2.tar.bz2" -C $work
Move-Item -LiteralPath "$boostSource/tools/build" -Destination "$work/boost-build-bundled"
Move-Item -LiteralPath "$work/b2-5.4.2" -Destination "$boostSource/tools/build"
Push-Location $boostSource
try {
    cmd.exe /d /c bootstrap.bat
    & ./b2.exe -j2 --layout=system "--prefix=$work/boost" libs/headers/build//install
} finally { Pop-Location }
$boostConfig = "$work/boost/lib/cmake/Boost-1.90.0"
if (!(Test-Path "$boostConfig/BoostConfig.cmake")) { throw 'The pinned Boost CMake package was not installed' }
$triplet = "$Architecture-windows"
$vcpkg = $env:VCPKG_INSTALLATION_ROOT
if (!$vcpkg) { throw 'VCPKG_INSTALLATION_ROOT is required' }
$installed = "$work/vcpkg"
& "$vcpkg/vcpkg.exe" install "libarchive[core]:$triplet" "libmaxminddb:$triplet" "zlib:$triplet" "--x-install-root=$installed" --disable-metrics
& "$vcpkg/vcpkg.exe" list "--x-install-root=$installed" | Set-Content -Encoding ascii "$Downloads/vendor-packages.txt"
$vendor = "$installed/$triplet"
$common = @(
    '-A', $vsPlatform, "-DCMAKE_TOOLCHAIN_FILE=$vcpkg/scripts/buildsystems/vcpkg.cmake",
    "-DVCPKG_TARGET_TRIPLET=$triplet", "-DVCPKG_INSTALLED_DIR=$installed",
    "-DBoost_DIR=$boostConfig",
    "-DOPENSSL_ROOT_DIR=$work/openssl", '-DOPENSSL_USE_STATIC_LIBS=TRUE',
    '-DCMAKE_CXX_FLAGS_INIT=/MP2'
)
$sdkArgs = @(
    "-DCMAKE_INSTALL_PREFIX=$work/sdk", '-DCMAKE_CXX_STANDARD=20', '-DCMAKE_CXX_STANDARD_REQUIRED=ON',
    '-DBUILD_SHARED_LIBS=ON', '-Dbuild_tests=ON', '-Dbuild_tools=OFF'
)
cmake -S $source -B "$work/sdk-build" @common @sdkArgs
cmake --build "$work/sdk-build" --config Release --target torrent-rasterbar --parallel 2
cmake --install "$work/sdk-build" --config Release

$desktopArgs = @(
    "-DCMAKE_PREFIX_PATH=$work/sdk;$qt;$vendor", '-DBIGLYBTC_LOCAL_ENGINE=ON', '-DBIGLYBTC_DESKTOP=ON',
    '-DBUILD_TESTING=ON', '-DBIGLYBTC_INSTALL_CORE=OFF', '-DBIGLYBTC_WINDOWS_PACKAGE=ON',
    "-DBIGLYBTC_WINDOWS_VENDOR_PREFIX=$vendor", "-DBIGLYBTC_WINDEPLOYQT=$qt/bin/windeployqt.exe",
    "-DBIGLYBTC_WINDOWS_OPENSSL_NOTICE=$work/openssl-3.5.5/LICENSE.txt",
    "-DBIGLYBTC_RELEASE_SDK_SOURCE=$source", "-DBIGLYBTC_SHARED_UDP_LIBRARY=$work/sdk/bin/torrent-rasterbar.dll",
    "-DBIGLYBTC_MSGSYNC_SIGNER_SOURCE_DIR=$signer", "-DBIGLYBTC_MLDHT_IDENTITY_SOURCE_DIR=$identity",
    "-DBIGLYBTC_RELEASE_GEOIP=$Inputs/geoip"
)
cmake -S "$repo/preview/native" -B "$work/desktop" @common @desktopArgs
python -B "$repo/preview/native/packaging/test_windows_bundle.py"
$targets = @(
    'biglybtc-torrent-intake-tests', 'biglybtc-azureus-profile-storage-tests', 'biglybtc-profile-preflight-tests',
    'biglybtc-portable-minute-history-tests', 'biglybtc-portable-mainline-checkpoint-tests',
    'biglybtc-portable-network-binding-tests', 'biglybtc-saved-speed-manager-tests', 'biglybtc-magnet-intake-tests',
    'biglybtc-portable-torrent-creator-tests', 'biglybtc-saved-rss-source-tests', 'biglybtc-scheduler-service-tests',
    'biglybtc-i2p-contact-store-tests', 'biglybtc-rating-driver', 'biglybtc-rating-bounds-tests',
    'biglybtc-rating-local-driver', 'biglybtc-rating-local-bounds-tests'
)
$env:Path = "$qt/bin;$work/sdk/bin;$vendor/bin;$env:Path"
cmake --build "$work/desktop" --config Release --parallel 2 --target @targets
ctest --test-dir "$work/desktop" -C Release --output-on-failure --no-tests=error
cmake --build "$work/desktop" --config Release --parallel 2 --target biglybtc-windows-zip
$packages = @(Get-ChildItem "$work/desktop" -File -Filter "BiglyBTC-*-windows-$Architecture.zip")
if ($packages.Count -ne 1) { throw "Expected one Windows $Architecture ZIP" }
$package = $packages[0]
python -B "$repo/preview/native/packaging/smoke_windows_zip.py" $package.FullName $Architecture
Copy-Item $package.FullName $Downloads
"$((Get-FileHash -Algorithm SHA256 $package.FullName).Hash.ToLowerInvariant())  $($package.Name)" |
    Set-Content -Encoding ascii "$Downloads/SHA256SUMS"
git -C $repo rev-parse HEAD | Set-Content -Encoding ascii "$Downloads/source-revision.txt"
