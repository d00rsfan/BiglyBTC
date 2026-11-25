#pragma once
#include "biglybtc/country_statistics.hpp"

#include <memory>
#include <optional>

namespace biglybtc::plugins { class Module; }

namespace biglybtc::storage {
struct PeerAsn {
  std::uint32_t number = 0;
  std::array<char, 252> organization{};
};
enum class CountryProviderError { ModuleUnavailable, Memory };
// Worker-affine optional location service. The artifact MUST already be
// authenticated by the existing installer; Module::load is not a sandbox or
// signature verifier. No discovery, install, download or profile path lookup.
class CountryProvider final {
public:
  // Caller creates the module through the existing authenticated installer +
  // Module::load exact ID/version/capability guard, with an explicit private DB
  // directory descriptor in its options. This facade never selects a path.
  static std::expected<std::unique_ptr<CountryProvider>,CountryProviderError> bind(
      plugins::Module&& authenticatedLocationModule);
  ~CountryProvider();
  CountryProvider(const CountryProvider&)=delete;
  CountryProvider& operator=(const CountryProvider&)=delete;
  bool available() noexcept; // Probes actual bound DB on EVERY call.
  core::CountryLookup lookup(const core::PeerNumericEndpoint&) noexcept;
  std::optional<PeerAsn> lookupAsn(const core::PeerNumericEndpoint&) noexcept;
  std::expected<core::CountryStatistics,core::CountryStatisticsError> snapshot(
      const core::PeerPayloadSnapshot&) noexcept;
private:
  struct Impl;
  explicit CountryProvider(std::unique_ptr<Impl>) noexcept;
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::storage
