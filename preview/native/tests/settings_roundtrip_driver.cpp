#include "application_settings.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace biglybtc;
using Id = core::SettingId;
void require(bool value, const char* message) {
  if (!value) { throw std::runtime_error(message); }
}
template<class T> T take(std::expected<T, storage::Error> result) {
  if (!result) { throw std::runtime_error(result.error().message); }
  return std::move(*result);
}
const core::SettingValue& value(const storage::ApplicationSettingsSnapshot& snapshot, Id id) {
  const auto& field = snapshot.fields.at(static_cast<std::size_t>(id));
  require(field.state == storage::SettingState::Stored && field.value.has_value(), "Expected stored typed field");
  return *field.value;
}
std::string opaque(const storage::ApplicationSettingsSnapshot& snapshot) {
  require(static_cast<bool>(snapshot.source), "Expected original configuration");
  const auto* node = snapshot.source->find("native.options.opaque");
  require(node && node->type == core::Node::Type::List, "Expected original opaque list");
  return snapshot.source->originalBytes().substr(node->begin, node->end-node->begin);
}
void checkOpaque(const storage::ApplicationSettingsSnapshot& snapshot) {
  require(opaque(snapshot) == "li7e11:opaque–πe", "Original opaque list changed");
}
void common(const storage::ApplicationSettingsSnapshot& snapshot, const std::filesystem::path& payload) {
  require(std::get<std::int64_t>(value(snapshot, Id::DownloadLimit)) == 47, "Native download limit lost");
  require(std::get<std::int64_t>(value(snapshot, Id::MaxActive)) == 6, "Native queue cap lost");
  require(std::get<std::int64_t>(value(snapshot, Id::MaxDownloads)) == 3, "Native download cap lost");
  require(std::get<std::string>(value(snapshot, Id::ProxyPassword)) == "native-test-secret", "Native secret not retained");
  require(std::get<std::string>(value(snapshot, Id::DefaultSavePath)) == payload.string(), "Native destination not retained");
  require(std::get<std::string>(value(snapshot, Id::ProxyHost)) == "127.0.0.1", "Proxy host changed");
  require(std::get<std::string>(value(snapshot, Id::ProxyPort)) == "19123", "String proxy port changed");
  checkOpaque(snapshot);
}
} // namespace
int main(int argc, char** argv) {
  try {
    require(argc == 3, "Usage: settings-roundtrip-driver PROFILE apply|check");
    const std::string_view mode(argv[2]);
    require(mode == "apply" || mode == "check", "Invalid roundtrip command");
    const auto profile = std::filesystem::canonical(argv[1]);
    const auto payload = profile.parent_path()/"payload";
    auto lease = take(storage::ProfileLease::acquire(profile));
    auto before = take(storage::readApplicationSettings(lease));
    require(before.recoverySource == storage::RecoverySource::Primary, "Expected original primary configuration");
    checkOpaque(before);
    if (mode == "apply") {
      require(std::get<std::int64_t>(value(before, Id::UploadLimit)) == 17, "Original upload input mismatch");
      require(std::get<std::int64_t>(value(before, Id::DownloadLimit)) == 31, "Original download input mismatch");
      require(std::get<std::string>(value(before, Id::ProxyHost)) == "127.0.0.1", "Original proxy host mismatch");
      require(std::get<std::string>(value(before, Id::ProxyPort)) == "19123", "Original string proxy port mismatch");
      const auto originalOpaque = opaque(before);
      const std::array changes{
        core::SettingChange{Id::UploadLimit, std::int64_t{23}},
        core::SettingChange{Id::DownloadLimit, std::int64_t{47}},
        core::SettingChange{Id::MaxActive, std::int64_t{6}},
        core::SettingChange{Id::MaxDownloads, std::int64_t{3}},
        core::SettingChange{Id::ProxyPassword, std::string("native-test-secret")},
        core::SettingChange{Id::DefaultSavePath, payload.string()}
      };
      const auto prepared = take(storage::prepareApplicationSettings(before, changes));
      require(prepared.changed(), "Expected actual sparse native changes");
      const auto saved = take(storage::saveApplicationSettings(lease, before, prepared));
      require(opaque(saved) == originalOpaque, "Sparse native save changed opaque bytes");
      const auto actual = take(storage::readApplicationSettings(lease));
      require(std::get<std::int64_t>(value(actual, Id::UploadLimit)) == 23, "Native upload change not persisted");
      common(actual, payload);
    } else {
      require(std::get<std::int64_t>(value(before, Id::UploadLimit)) == 29, "Original Java resave not observed");
      require(std::get<bool>(value(before, Id::RequireEncryption)), "Original Java boolean resave not observed");
      common(before, payload);
    }
    std::cout << "application-settings " << mode << " PASS\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
