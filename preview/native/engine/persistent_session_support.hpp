#pragma once

// Private engine integration only. Not an application/GUI handle API.
#include "persistent_session.hpp"
#include "local_support.hpp"

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>

#include <vector>

namespace biglybtc::engine::detail {

// Extends the owner reentry fence over preparation, engine use, compatible
// publication, and every user observer. No callback can recursively admit an
// operation or close the owner while this call is on its stack.
class PersistentCall final {
public:
  PersistentCall(PersistentSession&, const storage::ProfileLease&);
  ~PersistentCall();
  PersistentCall(const PersistentCall&) = delete;
  PersistentCall& operator=(const PersistentCall&) = delete;
private:
  PersistentSession& owner_;
};

class PersistentOperation final {
public:
  PersistentOperation(PersistentSession&, const storage::ProfileLease&, const LocalOptions&);
  ~PersistentOperation();
  PersistentOperation(const PersistentOperation&) = delete;
  PersistentOperation& operator=(const PersistentOperation&) = delete;
  libtorrent::torrent_handle add(libtorrent::add_torrent_params);
  void request() noexcept;
  // One owner pop, with <=4096 alerts, copied essential counters and exact
  // admitted-handle errors. No alert or counter span survives the next pop.
  std::vector<libtorrent::alert*> pump();
  // Flush success must be bound to this admission, never just an info hash.
  bool flushed(const std::vector<libtorrent::alert*>&) const noexcept;
  // Always runs before the existing payload fsync/state/intent publication.
  // Throws Failure only for a missing/failed storage fence; statistics-only
  // failures remain in the copied retirement receipt and do not fail transfer.
  void retire();
  template<class Predicate> void wait(const LocalOptions& options, Predicate ready) {
    using namespace std::chrono_literals;
    const auto deadline = std::chrono::steady_clock::now() + options.timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      require(!options.stop.stop_requested(),
          "Local session cancelled; original state requires recheck", storage::ErrorCode::Cancelled);
      const auto pending = pump();
      if (ready(pending)) { return; }
      std::this_thread::sleep_for(20ms);
    }
    throw Failure{{storage::ErrorCode::Io,
        "Local session deadline expired; original state requires recheck"}};
  }
private:
  PersistentSession& owner_;
  const LocalOptions& options_;
  bool retired_ = false;
};

} // namespace biglybtc::engine::detail
