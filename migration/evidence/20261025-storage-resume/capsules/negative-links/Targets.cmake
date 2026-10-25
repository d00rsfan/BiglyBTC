# Adoption fragment only. Root owns shared CMake and test execution.
target_sources(biglybtc_core PRIVATE src/original_file_links.cpp)

# In preview/native/tests/CMakeLists.txt:
add_executable(biglybtc-original-file-links-tests original_file_links_tests.cpp)
target_link_libraries(biglybtc-original-file-links-tests PRIVATE biglybtc_core)
target_compile_features(biglybtc-original-file-links-tests PRIVATE cxx_std_23)
target_compile_options(biglybtc-original-file-links-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME original-file-links COMMAND biglybtc-original-file-links-tests)
