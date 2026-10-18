#pragma once

#include "local_session.hpp"
#include "file_io.hpp"

#include <libtorrent/alert.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>

#include <thread>

namespace biglybtc::engine::detail {
using core::Document;
using core::Node;
using storage::Error;
using storage::ErrorCode;
using storage::detail::Descriptor;
struct Failure { Error error; };
void require(bool condition, std::string message, ErrorCode code = ErrorCode::Unsupported);
template<typename T> T take(std::expected<T, Error> result) {
  if (!result) { throw Failure{result.error()}; }
  return std::move(*result);
}
void done(std::expected<void, Error> result);
std::string encoded(const Node& node);
Document dictionary(const Node* node);
void put(Document& document, std::string_view key, std::string value);
std::string stringValue(std::string_view value);
std::string text(const Node* node);
std::int64_t integer(const Node* node, std::int64_t fallback = 0);
void observe(const LocalOptions& options, LocalStage stage);
Descriptor directory(const std::filesystem::path& path);
void requireUtf8Filesystem(const std::filesystem::path& path);
struct SavedPolicy {
  std::vector<std::uint8_t> priorities;
  int downloadLimit;
  int uploadLimit;
};
SavedPolicy readPolicy(const Node& entry, std::size_t files);
void sync(int descriptor);
libtorrent::settings_pack settings();
void errors(const std::vector<libtorrent::alert*>& pending);
template<typename Predicate> void wait(libtorrent::session& session, const LocalOptions& options, Predicate ready) {
  using namespace std::chrono_literals;
  const auto deadline = std::chrono::steady_clock::now() + options.timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    require(!options.stop.stop_requested(), "Local session cancelled; original state requires recheck", ErrorCode::Cancelled);
    std::vector<libtorrent::alert*> pending;
    session.pop_alerts(&pending);
    errors(pending);
    if (ready(pending)) { return; }
    std::this_thread::sleep_for(20ms);
  }
  throw Failure{{ErrorCode::Io, "Local session deadline expired; original state requires recheck"}};
}
} // namespace biglybtc::engine::detail
