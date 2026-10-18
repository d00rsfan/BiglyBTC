set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
execute_process(COMMAND "${CMAKE_CXX_COMPILER}" --version
  OUTPUT_VARIABLE compiler_identity COMMAND_ERROR_IS_FATAL ANY)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(NOT compiler_identity MATCHES "16.0.1 20260322.*r16-8246-g569ace1fa50")
    message(FATAL_ERROR "BiglyBTC requires the pinned GCC 16 snapshot")
  endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  if(NOT compiler_identity MATCHES "24.0.0.*20260911085248\\+58c46bae2118")
    message(FATAL_ERROR "BiglyBTC requires the pinned Clang 24 snapshot")
  endif()
else()
  message(FATAL_ERROR "Unqualified compiler for this Linux feasibility probe")
endif()

include(CheckCXXSourceCompiles)
check_cxx_source_compiles("#include <bits/c++config.h>
  #if _GLIBCXX_RELEASE != 16 || __GLIBCXX__ != 20260322
  #error Unqualified standard library
  #endif
  int main() {}" BIGLYBTC_PINNED_STDLIB)
if(NOT BIGLYBTC_PINNED_STDLIB)
  message(FATAL_ERROR "BiglyBTC requires libstdc++ 16 headers dated 20260322")
endif()
