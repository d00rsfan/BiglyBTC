// Same-value foreign marker only; never an approved provider implementation.
#if defined(BIGLYBTC_FOREIGN_ORIGINAL_IDENTITY)
extern "C" unsigned biglybtc_mainline_identity_abi_v1() noexcept {return 1;}
#else
extern "C" unsigned biglybtc_tracker_response_abi_v1() noexcept {return 1;}
#endif
