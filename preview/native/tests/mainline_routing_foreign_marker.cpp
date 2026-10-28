// Same-value foreign marker; never an approved implementation.
#if defined(BIGLYBTC_FOREIGN_MAINLINE_ROUTING)
extern "C" unsigned biglybtc_mainline_routing_abi_v1() noexcept {return 1;}
#elif defined(BIGLYBTC_FOREIGN_ORIGINAL_IDENTITY)
extern "C" unsigned biglybtc_mainline_identity_abi_v1() noexcept {return 1;}
#else
extern "C" unsigned biglybtc_tracker_response_abi_v1() noexcept {return 1;}
#endif
