// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include "rss_get_description.hpp"
#include <QList>
#include <QSslCertificate>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>

namespace biglybtc::search {
struct SavedRssSelector {
  std::size_t sourceIndex=0;
  std::int64_t id=0;
  std::string uid; // Exact original bytes, not a display string.
};
enum class SavedRssReadReason { ExplicitOneShotDescription };
enum class SavedRssSourceError { Lease,WrongWorker,SourceChanged,Malformed,Unsupported,Ambiguous,Limit,Memory };
struct SavedRssDescription : RssGetDescription { SavedRssSelector selected; };
// A worker-bound read-only description of a real saved row. This class neither
// grants I/O nor creates an effect permit. It has no resolver/socket/worker,
// request queue, HTTP operation, credential setter or loopback bypass.
// The explicit read reason is never inferred from the saved selected flag.
class SavedRssSourceSnapshot final {
public:
  static std::expected<SavedRssSourceSnapshot,SavedRssSourceError> read(
      const storage::ProfileLease&,const std::filesystem::path& profileDirectory,
      std::uint64_t profileSession,SavedRssSelector,SavedRssReadReason);
  SavedRssSourceSnapshot(SavedRssSourceSnapshot&&)noexcept;
  SavedRssSourceSnapshot& operator=(SavedRssSourceSnapshot&&)noexcept;
  ~SavedRssSourceSnapshot();
  SavedRssSourceSnapshot(const SavedRssSourceSnapshot&)=delete;
  SavedRssSourceSnapshot& operator=(const SavedRssSourceSnapshot&)=delete;
  const SavedRssDescription& description()const noexcept;
  // Requires the creator thread, same actual OFD lineage and current session.
  // Rechecks exact named sources under the source gate and returns the freshly
  // observed revision, accepting unrelated profile writes. Success describes a
  // completed observation only; future effects still need their own concrete
  // phase permits, revocation and endpoint/bind checks. No gate escapes a call.
  std::expected<std::uint64_t,SavedRssSourceError> current(
      const storage::ProfileLease& callerHeldLease,std::uint64_t profileSession)const;
private:
  struct Impl;
  explicit SavedRssSourceSnapshot(std::unique_ptr<Impl>)noexcept;
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::search
