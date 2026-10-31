// Same-value foreign private markers must never become same-image authority.
#if defined(BIGLYBTC_FOREIGN_I2P_TORRENTS)
extern "C" unsigned biglybtc_private_i2p_torrents_abi_v1() noexcept { return 0x42495401U; }
#else
extern "C" unsigned biglybtc_private_i2p_owner_abi_v1() noexcept { return 0x42494f01U; }
#endif
