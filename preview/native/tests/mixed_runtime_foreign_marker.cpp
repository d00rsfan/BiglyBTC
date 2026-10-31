#if defined(BIGLYBTC_FOREIGN_I2P_MIXED)
extern "C" unsigned biglybtc_mixed_i2p_transport_abi_v1()noexcept{return 0x42494d01U;}
#elif defined(BIGLYBTC_FOREIGN_I2P_TORRENTS)
extern "C" unsigned biglybtc_private_i2p_torrents_abi_v1()noexcept{return 0x42495401U;}
#else
extern "C" unsigned biglybtc_private_i2p_owner_abi_v1()noexcept{return 0x42494f01U;}
#endif
