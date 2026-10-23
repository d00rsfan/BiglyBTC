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
