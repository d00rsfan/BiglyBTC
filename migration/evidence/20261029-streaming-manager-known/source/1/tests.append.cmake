# Explicit original-manager acceptance only; no Java build/download at configure.
option(BIGLYBTC_I2P_STREAMING_MANAGER_JAVA_ACCEPTANCE "Build actual Java four-stream manager acceptance" OFF)
if(BIGLYBTC_I2P_STREAMING_MANAGER_JAVA_ACCEPTANCE)
  foreach(input IN ITEMS BIGLYBTC_I2P_MANAGER_JAVA BIGLYBTC_I2P_MANAGER_JAR BIGLYBTC_I2P_MANAGER_CLASSES)
    if(NOT IS_ABSOLUTE "${${input}}" OR NOT EXISTS "${${input}}")
      message(FATAL_ERROR "Explicit existing absolute ${input} required")
    endif()
  endforeach()
  file(SHA256 "${BIGLYBTC_I2P_MANAGER_JAR}" manager_reference_sha256)
  if(NOT manager_reference_sha256 STREQUAL "b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5")
    message(FATAL_ERROR "Wrong actual modified-core manager reference")
  endif()
  if(NOT EXISTS "${BIGLYBTC_I2P_MANAGER_CLASSES}/ManagerPeer.class" OR
      NOT EXISTS "${BIGLYBTC_I2P_MANAGER_CLASSES}/ManagerPeer$Adapter.class")
    message(FATAL_ERROR "Actual compiled manager peer required; no skip credit")
  endif()
  add_executable(biglybtc-i2p-streaming-manager-interop-tests i2p_streaming_manager_interop_tests.cpp)
  set_target_properties(biglybtc-i2p-streaming-manager-interop-tests PROPERTIES AUTOMOC ON)
  target_link_libraries(biglybtc-i2p-streaming-manager-interop-tests PRIVATE biglybtc_i2cp_initial_channel biglybtc_i2p_streaming Qt6::Test Qt6::Network)
  target_compile_features(biglybtc-i2p-streaming-manager-interop-tests PRIVATE cxx_std_23)
  target_compile_options(biglybtc-i2p-streaming-manager-interop-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_definitions(biglybtc-i2p-streaming-manager-interop-tests PRIVATE
    BIGLYBTC_I2P_MANAGER_JAVA="${BIGLYBTC_I2P_MANAGER_JAVA}"
    BIGLYBTC_I2P_MANAGER_JAR="${BIGLYBTC_I2P_MANAGER_JAR}"
    BIGLYBTC_I2P_MANAGER_CLASSES="${BIGLYBTC_I2P_MANAGER_CLASSES}"
    BIGLYBTC_I2P_MANAGER_KEYS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/i2p-datagram")
  add_test(NAME i2p-original-streaming-manager COMMAND biglybtc-i2p-streaming-manager-interop-tests)
  set_tests_properties(i2p-original-streaming-manager PROPERTIES TIMEOUT 360 LABELS "reference;explicit-i2p;long")
endif()
