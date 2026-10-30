# Append once after the reviewed window services fragment.
if(TARGET biglybtc_i2p_streaming)
  target_sources(biglybtc_i2p_streaming PRIVATE i2p_streaming/manager.cpp)
  target_sources(biglybtc_i2cp_initial_channel PRIVATE i2cp_channel/managed_streams.inc)
endif()
