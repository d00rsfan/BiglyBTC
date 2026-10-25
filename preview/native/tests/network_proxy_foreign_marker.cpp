// Owned preload fixture only; no LT dependency or original export interposition.
namespace { unsigned calls = 0; }
extern "C" __attribute__((visibility("default"))) unsigned biglybtc_socks4a_abi_v1() noexcept {
  ++calls; return 1;
}
extern "C" __attribute__((visibility("default"))) unsigned biglybtc_foreign_socks4_marker_calls() noexcept {
  return calls;
}
