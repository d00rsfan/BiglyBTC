// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace biglybtc::core {

struct ConsoleEntry {
  std::uint64_t sequence = 0;
  std::int64_t wallMillis = 0;
  std::string message;
};

class ConsoleHistory final {
public:
  void append(std::string_view message, std::string_view subject = {}) noexcept {
    if (message.empty()) { return; }
    try {
      const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count();
      std::lock_guard lock(mutex_);
      if (sequence_ == UINT64_MAX) { return; }
      entries_.push_back({std::string(subject), {sequence_ + 1, now, std::string(message.substr(0, 4096))}});
      ++sequence_;
      if (entries_.size() > 2048) { entries_.pop_front(); }
    } catch (...) {
      // Optional logging must never interrupt an operation.
    }
  }

  std::vector<ConsoleEntry> recent(std::uint64_t after = 0, std::string_view subject = {},
      std::size_t limit = 2048) const {
    std::vector<ConsoleEntry> result;
    std::lock_guard lock(mutex_);
    for (auto it = entries_.rbegin(); it != entries_.rend() && it->entry.sequence > after && result.size() < limit; ++it) {
      if (subject.empty() || it->subject == subject) { result.push_back(it->entry); }
    }
    std::ranges::reverse(result);
    return result;
  }

  std::uint64_t latestSequence() const {
    std::lock_guard lock(mutex_);
    return sequence_;
  }

private:
  struct Record {
    std::string subject;
    ConsoleEntry entry;
  };
  mutable std::mutex mutex_;
  std::deque<Record> entries_;
  std::uint64_t sequence_ = 0;
};

inline ConsoleHistory& applicationConsole() {
  static ConsoleHistory history;
  return history;
}

} // namespace biglybtc::core
