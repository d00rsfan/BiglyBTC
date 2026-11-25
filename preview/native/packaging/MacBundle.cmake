find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(BIGLYBTC_MACDEPLOYQT macdeployqt HINTS "${Qt6_DIR}/../../../bin" REQUIRED)
get_property(release_sdk GLOBAL PROPERTY BIGLYBTC_RELEASE_SDK_LIBRARY)
get_property(release_receipt GLOBAL PROPERTY BIGLYBTC_RELEASE_SDK_RECEIPT)
if(NOT release_sdk OR NOT release_receipt)
  message(FATAL_ERROR "The macOS bundle requires a verified native torrent SDK")
endif()
set(BIGLYBTC_RELEASE_GEOIP "" CACHE PATH "Directory containing the release's DB-IP databases")
foreach(database IN ITEMS DBIP-Country.mmdb DBIP-ASN.mmdb)
  if(NOT EXISTS "${BIGLYBTC_RELEASE_GEOIP}/${database}")
    message(FATAL_ERROR "Missing release GeoIP database: ${database}")
  endif()
endforeach()
target_link_options(BiglyBTC PRIVATE "LINKER:-headerpad_max_install_names")
set_property(TARGET BiglyBTC APPEND PROPERTY BUILD_RPATH "@executable_path/../Frameworks")

add_custom_target(biglybtc-macos-dmg
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_LIST_DIR}/mac_bundle.py"
    --bundle "$<TARGET_BUNDLE_DIR:BiglyBTC>"
    --sdk "${release_sdk}" --receipt "${release_receipt}"
    --sdk-license "${BIGLYBTC_RELEASE_SDK_SOURCE}/COPYING"
    --geoip "${BIGLYBTC_RELEASE_GEOIP}"
    --macdeployqt "${BIGLYBTC_MACDEPLOYQT}"
    --output "${PROJECT_BINARY_DIR}/BiglyBTC-${PROJECT_VERSION}-macos-${CMAKE_SYSTEM_PROCESSOR}.dmg"
  DEPENDS BiglyBTC
  VERBATIM)
