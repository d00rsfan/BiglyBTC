# Append to native tests/CMakeLists.txt only with the reviewed owner capsule.
# Standalone SOURCE recipe file, not an independent configure project.
# The root qualification explicitly builds this target; no ordinary CTest or
# default application/backend activation is introduced.
if(TARGET biglybtc_i2p_provider_owner)
  add_executable(biglybtc-i2p-provider-transfer-probe EXCLUDE_FROM_ALL
    i2p_provider_transfer_probe.cpp)
  target_link_libraries(biglybtc-i2p-provider-transfer-probe PRIVATE
    biglybtc_i2p_provider_owner BiglyBTC::Core
    LibtorrentRasterbar::torrent-rasterbar Qt6::Core Qt6::Network OpenSSL::Crypto)
  target_include_directories(biglybtc-i2p-provider-transfer-probe PRIVATE
    "${PROJECT_SOURCE_DIR}/adapters")
  target_compile_features(biglybtc-i2p-provider-transfer-probe PRIVATE cxx_std_23)
  target_compile_options(biglybtc-i2p-provider-transfer-probe PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
