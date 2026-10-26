// Owned preload fixture: only the optional marker, no original/base4a exports.
namespace { unsigned calls = 0; }
extern "C" __attribute__((visibility("default"))) unsigned biglybtc_socks4a_webseed_abi_v1() noexcept {
  ++calls; return 1;
}
extern "C" __attribute__((visibility("default"))) unsigned biglybtc_foreign_webseed_marker_calls() noexcept {
  return calls;
}
