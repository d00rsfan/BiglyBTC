#include "minute_history_writer.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace {
using namespace biglybtc;
struct Expected { std::string_view leaf; bool present; std::string_view bytes; };
struct Step {
  char action;
  std::int64_t now;
  core::HistoryCounters raw, baseline;
  std::span<const Expected> expected;
};
struct Case { std::string_view name; std::span<const Step> steps; };
#include "fixtures/minute-java/replay.inc"

std::size_t assertions = 0;
void check(bool condition, std::string_view reason) {
  ++assertions; if (!condition) { throw std::runtime_error(std::string(reason)); }
}
struct Area {
  std::filesystem::path root;
  std::optional<storage::ProfileLease> lease;
  Area() {
    char pattern[] = "/tmp/biglybtc-minute-java-native-XXXXXX";
    const char* created = ::mkdtemp(pattern); check(created != nullptr, "Cannot create owned replay root"); root = created;
    auto result = storage::ProfileLease::acquire(root); check(result.has_value(), "Cannot borrow generated replay lease");
    lease.emplace(std::move(*result));
  }
  ~Area() {
    lease.reset();
    if (!root.empty()) { std::error_code ec; std::filesystem::remove_all(root, ec); }
  }
  Area(const Area&) = delete; Area& operator=(const Area&) = delete;
};
struct Fd {
  int value;
  explicit Fd(int descriptor) : value(descriptor) {}
  ~Fd() { if (value >= 0) { (void)::close(value); } }
  Fd(const Fd&) = delete; Fd& operator=(const Fd&) = delete;
};
bool validLeaf(std::string_view leaf) {
  if (leaf.size() != 14 || leaf[4] != '/' || leaf[7] != '/' || leaf.substr(10) != ".dat") { return false; }
  for (std::size_t i = 0; i < 10; ++i) {
    if (i != 4 && i != 7 && (leaf[i] < '0' || leaf[i] > '9')) { return false; }
  }
  return true;
}
void compare(const Area& area, const Expected& expected) {
  check(validLeaf(expected.leaf), "Replay fixture has an invalid exact day pathname");
  const auto path = area.root / "stats" / expected.leaf;
  struct stat named{};
  const int status = ::lstat(path.c_str(), &named);
  if (!expected.present) {
    check(status < 0 && errno == ENOENT, "Native created a file absent in actual Java output"); return;
  }
  check(status == 0 && S_ISREG(named.st_mode) && named.st_nlink == 1 && named.st_uid == ::geteuid(),
      "Actual native output is not an owned regular day file");
  check(named.st_size >= 0 && static_cast<std::uint64_t>(named.st_size) <= core::SavedHistoryMaximumBytes,
      "Actual native output exceeds replay byte bound");
  Fd fd(::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)); check(fd.value >= 0, "Cannot read actual native output");
  struct stat opened{};
  check(::fstat(fd.value, &opened) == 0 && opened.st_dev == named.st_dev && opened.st_ino == named.st_ino,
      "Actual native replay pathname changed while reading");
  std::string bytes; std::array<char, 4096> buffer{};
  for (;;) {
    const auto count = ::read(fd.value, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) { continue; }
    check(count >= 0, "Reading native output failed"); if (!count) { break; }
    check(static_cast<std::size_t>(count) <= core::SavedHistoryMaximumBytes - bytes.size(), "Native replay read exceeds cap");
    bytes.append(buffer.data(), static_cast<std::size_t>(count));
  }
  check(bytes == expected.bytes, "Native bytes differ from actual original Java stdout");
}
} // namespace

int main() {
  std::string context;
  try {
    std::size_t actions = 0, fileObservations = 0, comparedBytes = 0;
    std::uint64_t session = 0;
    for (const auto& test : Cases) {
      Area area; core::MinuteHistoryRecorder recorder;
      const int held = area.lease->directoryHandle(); ++session;
      std::size_t stepNumber = 0;
      for (const auto& step : test.steps) {
        context = std::string(test.name) + " step " + std::to_string(++stepNumber);
        core::MinuteHistoryRequest request;
        request.identity = {1, session, 1}; request.utcMilliseconds = step.now;
        request.input = {std::array{step.raw[0], step.raw[1], step.raw[2], step.raw[3]}, std::array{step.raw[4], step.raw[5]}};
        switch (step.action) {
          case 'S': request.event = core::MinuteHistoryEvent::Start; request.absoluteBaseline = step.baseline; break;
          case 'T': request.event = core::MinuteHistoryEvent::Tick; break;
          case 'E': request.event = core::MinuteHistoryEvent::End; break;
          default: throw std::runtime_error("Invalid literal replay action");
        }
        auto written = storage::writeMinuteHistory(*area.lease, recorder, request);
        check(written.has_value(), written ? "Unexpected successful replay" : written.error().message);
        ++actions;
        for (const auto& expected : step.expected) {
          compare(area, expected); ++fileObservations; comparedBytes += expected.bytes.size();
        }
        check(area.lease->valid() && area.lease->directoryHandle() == held && ::fcntl(held, F_GETFD) >= 0,
            "Replay writer consumed or replaced the borrowed lease");
      }
    }
    check(Cases.size() == 8 && actions == 24 && fileObservations == 30 && comparedBytes == 1561,
        "Frozen original Java corpus was partially consumed");
    std::cout << "Actual original Java minute-writer replay: plans=" << Cases.size() << " actions=" << actions
        << " file_observations=" << fileObservations << " compared_bytes=" << comparedBytes << " assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << context << ": " << error.what() << '\n'; return 1;
  }
}
