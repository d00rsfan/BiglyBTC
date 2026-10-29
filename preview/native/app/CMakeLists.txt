add_library(biglybtc_overall_service STATIC overall_statistics_service.cpp)
target_include_directories(biglybtc_overall_service PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(biglybtc_overall_service PUBLIC biglybtc_profile)
target_compile_options(biglybtc_overall_service PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(BiglyBTC-headless main.cpp)
target_link_libraries(BiglyBTC-headless PRIVATE biglybtc_local_engine)
target_compile_options(BiglyBTC-headless PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(BIGLYBTC_DESKTOP)
  add_library(biglybtc_desktop_host STATIC session_bridge.hpp session_bridge.cpp)
  set_target_properties(biglybtc_desktop_host PROPERTIES AUTOMOC ON)
  target_include_directories(biglybtc_desktop_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(biglybtc_desktop_host PUBLIC BiglyBTC::Core biglybtc_ui PRIVATE biglybtc_local_engine biglybtc_saved_service_view biglybtc_overall_service)
  target_compile_options(biglybtc_desktop_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_executable(BiglyBTC main_desktop.cpp)
  target_link_libraries(BiglyBTC PRIVATE biglybtc_desktop_host biglybtc_profile)
  target_compile_options(BiglyBTC PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(biglybtc_source_rate_projection STATIC source_rate_projection.cpp)
target_include_directories(biglybtc_source_rate_projection PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(biglybtc_source_rate_projection PUBLIC BiglyBTC::Core)
target_compile_options(biglybtc_source_rate_projection PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(TARGET biglybtc_desktop_host)
  target_link_libraries(biglybtc_desktop_host PRIVATE biglybtc_source_rate_projection)
endif()

if(BIGLYBTC_DESKTOP)
  find_path(BIGLYBTC_MAXMIND_INCLUDE_DIR maxminddb.h PATH_SUFFIXES x86_64-linux-gnu REQUIRED)
  find_library(BIGLYBTC_MAXMIND_LIBRARY NAMES maxminddb REQUIRED)
  find_package(LibArchive 3.8 REQUIRED)
  add_library(biglybtc_country_module MODULE ../plugins/BiglyBTC-plugin-azlocprov/location.cpp)
  set_target_properties(biglybtc_country_module PROPERTIES PREFIX "" OUTPUT_NAME "BiglyBTC-plugin-azlocprov"
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON CXX_EXTENSIONS OFF
    BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN")
  target_compile_features(biglybtc_country_module PRIVATE cxx_std_23)
  target_compile_options(biglybtc_country_module PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_definitions(biglybtc_country_module PRIVATE BT_NATIVE_VERSION="0.1.0")
  target_include_directories(biglybtc_country_module SYSTEM PRIVATE "${BIGLYBTC_MAXMIND_INCLUDE_DIR}")
  target_link_libraries(biglybtc_country_module PRIVATE BiglyBTC::PluginAPI "${BIGLYBTC_MAXMIND_LIBRARY}" LibArchive::LibArchive)
  get_filename_component(biglybtc_maxmind_actual "${BIGLYBTC_MAXMIND_LIBRARY}" REALPATH)
  get_target_property(biglybtc_archive_actual LibArchive::LibArchive IMPORTED_LOCATION)
  get_filename_component(biglybtc_archive_actual "${biglybtc_archive_actual}" REALPATH)
  add_custom_command(TARGET biglybtc_country_module POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${biglybtc_maxmind_actual}" "$<TARGET_FILE_DIR:biglybtc_country_module>/libmaxminddb.so.0"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${biglybtc_archive_actual}" "$<TARGET_FILE_DIR:biglybtc_country_module>/libarchive.so.13")
  set(country_approval "${CMAKE_CURRENT_BINARY_DIR}/generated/country_approval.hpp")
  add_custom_command(OUTPUT "${country_approval}"
    COMMAND "${CMAKE_COMMAND}" "-DINPUT=$<TARGET_FILE:biglybtc_country_module>" "-DOUTPUT=${country_approval}" -DVERSION=0.1.0
      -P "${CMAKE_CURRENT_SOURCE_DIR}/generate_country_approval.cmake"
    DEPENDS biglybtc_country_module generate_country_approval.cmake country_approval.hpp.in VERBATIM)
  target_sources(BiglyBTC PRIVATE "${country_approval}")
  target_include_directories(BiglyBTC PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  # FILES preserves the bytes authenticated by the compiled approval, even with --strip.
  install(FILES "$<TARGET_FILE:biglybtc_country_module>" DESTINATION lib/biglybtc/lib)
  install(FILES "${biglybtc_maxmind_actual}" DESTINATION lib/biglybtc/lib RENAME libmaxminddb.so.0)
  install(FILES "${biglybtc_archive_actual}" DESTINATION lib/biglybtc/lib RENAME libarchive.so.13)
endif()

if(BIGLYBTC_DESKTOP)
  get_filename_component(biglybtc_maxmind_libdir "${biglybtc_maxmind_actual}" DIRECTORY)
  get_filename_component(biglybtc_archive_libdir "${biglybtc_archive_actual}" DIRECTORY)
  install(FILES "${biglybtc_maxmind_libdir}/../../share/doc/libmaxminddb0/copyright"
    DESTINATION share/doc/biglybtc-experimental RENAME libmaxminddb-copyright)
  install(FILES "${biglybtc_archive_libdir}/../../share/doc/libarchive13t64/copyright"
    DESTINATION share/doc/biglybtc-experimental RENAME libarchive-copyright)
endif()

if(BIGLYBTC_DESKTOP)
  add_library(biglybtc_owned_azureus_service STATIC owned_azureus_service.cpp)
  target_include_directories(biglybtc_owned_azureus_service PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(biglybtc_owned_azureus_service PUBLIC biglybtc_azureus_owned_udp biglybtc_profile Threads::Threads)
  target_compile_features(biglybtc_owned_azureus_service PUBLIC cxx_std_23)
  target_compile_options(biglybtc_owned_azureus_service PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(biglybtc_desktop_host PRIVATE biglybtc_owned_azureus_service)
endif()

if(BIGLYBTC_DESKTOP)
  target_sources(biglybtc_desktop_host PRIVATE live_speed_mapper_binding.cpp)
endif()

if(BIGLYBTC_DESKTOP)
  target_sources(biglybtc_desktop_host PRIVATE owned_dht_accounting.cpp)
endif()

if(BIGLYBTC_DESKTOP)
  target_link_libraries(biglybtc_owned_azureus_service PUBLIC biglybtc_azureus_control)
endif()

if(BIGLYBTC_DESKTOP)
  target_link_libraries(biglybtc_desktop_host PRIVATE LibtorrentRasterbar::torrent-rasterbar)
endif()

if(BIGLYBTC_DESKTOP)
  set(biglybtc_scheduler_source "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/BiglyBTC-plugin-SpeedScheduler")
  add_library(biglybtc_scheduler_policy STATIC "${biglybtc_scheduler_source}/scheduler.cpp"
    "${biglybtc_scheduler_source}/original_xml.cpp" "${biglybtc_scheduler_source}/ownership_file.cpp")
  set_target_properties(biglybtc_scheduler_policy PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_include_directories(biglybtc_scheduler_policy PUBLIC "${biglybtc_scheduler_source}")
  target_link_libraries(biglybtc_scheduler_policy PRIVATE Qt6::Core)
  target_compile_options(biglybtc_scheduler_policy PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_library(biglybtc_scheduler_module MODULE "${biglybtc_scheduler_source}/module.cpp")
  set_target_properties(biglybtc_scheduler_module PROPERTIES PREFIX "" OUTPUT_NAME "BiglyBTC-plugin-SpeedScheduler"
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON CXX_EXTENSIONS OFF
    BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN")
  target_link_libraries(biglybtc_scheduler_module PRIVATE biglybtc_scheduler_policy BiglyBTC::PluginAPI)
  target_compile_options(biglybtc_scheduler_module PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_library(biglybtc_scheduler_service STATIC ../adapters/scheduler_service.cpp ../adapters/scheduler_xml.cpp
    ../adapters/scheduler_io.cpp ../adapters/scheduler_module.cpp)
  target_include_directories(biglybtc_scheduler_service PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../adapters")
  target_link_libraries(biglybtc_scheduler_service PUBLIC biglybtc_profile BiglyBTC::PluginHost
    biglybtc_scheduler_policy PRIVATE Qt6::Core OpenSSL::Crypto)
  target_compile_options(biglybtc_scheduler_service PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(biglybtc_desktop_host PRIVATE biglybtc_scheduler_service)
  target_include_directories(biglybtc_ui PUBLIC "${biglybtc_scheduler_source}")
  target_sources(biglybtc_ui PRIVATE ../ui/scheduler_dialog.cpp ../ui/scheduler_dialog.hpp ../ui/scheduler_presentation.hpp)
  set(scheduler_approval "${CMAKE_CURRENT_BINARY_DIR}/generated/scheduler_approval.hpp")
  add_custom_command(OUTPUT "${scheduler_approval}"
    COMMAND "${CMAKE_COMMAND}" "-DINPUT=$<TARGET_FILE:biglybtc_scheduler_module>" "-DOUTPUT=${scheduler_approval}" -DVERSION=0.1.0
      -P "${CMAKE_CURRENT_SOURCE_DIR}/generate_scheduler_approval.cmake"
    DEPENDS biglybtc_scheduler_module generate_scheduler_approval.cmake scheduler_approval.hpp.in VERBATIM)
  target_sources(BiglyBTC PRIVATE "${scheduler_approval}")
  # FILES preserves the exact artifact authenticated by the compiled approval.
  install(FILES "$<TARGET_FILE:biglybtc_scheduler_module>" DESTINATION lib/biglybtc/lib)
  install(FILES "${biglybtc_scheduler_source}/LICENSE" "${biglybtc_scheduler_source}/NOTICE.md"
    "${biglybtc_scheduler_source}/SOURCE_PROVENANCE.json" DESTINATION share/doc/biglybtc-experimental/SpeedScheduler)
endif()

if(BIGLYBTC_DESKTOP)
  # Root adoption fragment; no existing CMake file is overwritten by this capsule.
  # Include after profile/plugin/UI targets are declared in a desktop build.
  find_package(OpenSSL REQUIRED COMPONENTS Crypto)
  add_library(biglybtc_local_rating_profile STATIC
    "${PROJECT_SOURCE_DIR}/adapters/local_rating.cpp")
  target_include_directories(biglybtc_local_rating_profile PUBLIC "${PROJECT_SOURCE_DIR}/adapters")
  target_link_libraries(biglybtc_local_rating_profile PUBLIC biglybtc_profile biglybtc_rating_data PRIVATE OpenSSL::Crypto)
  target_compile_features(biglybtc_local_rating_profile PUBLIC cxx_std_23)
  target_compile_options(biglybtc_local_rating_profile PRIVATE -Wall -Wextra -Wpedantic -Werror)

  # Adopt after the rating64e Targets.cmake library/UI sources and bulk d569 files.
  if(NOT TARGET biglybtc_local_rating_profile)
    message(FATAL_ERROR "Adopt the exact local rating host library before wiring")
  endif()
  target_link_libraries(biglybtc_desktop_host PRIVATE biglybtc_local_rating_profile)
  target_link_libraries(biglybtc_owned_azureus_service PUBLIC biglybtc_local_rating_profile)
endif()

# Append after biglybtc_owned_azureus_service is defined. Preserve newer appends.
if(BIGLYBTC_DESKTOP)
  target_link_libraries(biglybtc_owned_azureus_service PUBLIC BiglyBTC::Core)
endif()
