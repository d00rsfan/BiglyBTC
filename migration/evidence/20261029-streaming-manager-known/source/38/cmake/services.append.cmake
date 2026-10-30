# Java's float Westwood filters round each operation; never contract to FMA.
if(TARGET biglybtc_i2p_streaming)
  target_compile_options(biglybtc_i2p_streaming PRIVATE -ffp-contract=off)
endif()
