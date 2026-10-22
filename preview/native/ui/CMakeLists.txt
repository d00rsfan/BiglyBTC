find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Widgets GLOBAL)
set(BIGLYBTC_QT_RUNTIME_VERSION "${Qt6Core_VERSION}" PARENT_SCOPE)
get_target_property(biglybtc_qt_core_location Qt6::Core IMPORTED_LOCATION_RELEASE)
get_filename_component(biglybtc_qt_library_dir "${biglybtc_qt_core_location}" DIRECTORY)
get_filename_component(BIGLYBTC_QT_RUNTIME_ROOT "${biglybtc_qt_library_dir}/.." ABSOLUTE)
set(BIGLYBTC_QT_RUNTIME_ROOT "${BIGLYBTC_QT_RUNTIME_ROOT}" PARENT_SCOPE)
add_library(biglybtc_ui STATIC library_model.hpp library_model.cpp library_window.hpp library_window.cpp file_model.hpp file_model.cpp classic_filter.hpp classic_filter.cpp classic_columns.hpp classic_columns.cpp saved_service_pages.hpp saved_service_pages.cpp column_layout_presentation.hpp statistics_presentation.hpp engine_statistics_presentation.hpp statistics_page.hpp statistics_page.cpp engine_activity_presentation.hpp engine_activity_history.hpp engine_activity_history.cpp engine_activity_view.hpp engine_activity_view.cpp history_presentation.hpp history_view.hpp history_view.cpp history_week_presentation.hpp history_week_view.hpp history_week_view.cpp history_month_presentation.hpp history_month_view.hpp history_month_view.cpp saved_history_plot.hpp saved_history_plot.cpp history_range_presentation.hpp history_range_view.hpp history_range_view.cpp history_range_plot.hpp history_range_plot.cpp)
set_target_properties(biglybtc_ui PROPERTIES AUTOMOC ON)
target_include_directories(biglybtc_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(biglybtc_ui PUBLIC Qt6::Widgets BiglyBTC::Core)
target_compile_options(biglybtc_ui PRIVATE -Wall -Wextra -Wpedantic -Werror)

target_sources(biglybtc_ui PRIVATE overall_presentation.hpp overall_view.hpp overall_view.cpp
  native_session_view.hpp native_session_view.cpp
  native_activity_presentation.hpp native_activity_history.hpp native_activity_history.cpp
  native_activity_view.hpp native_activity_view.cpp)

target_sources(biglybtc_ui PRIVATE runtime_metrics_presentation.hpp runtime_metrics_view.hpp runtime_metrics_view.cpp)

target_sources(biglybtc_ui PRIVATE effective_rate_limit_text.hpp)

target_sources(biglybtc_ui PRIVATE manual_tag_presentation.hpp manual_tag_view.hpp manual_tag_view.cpp)
