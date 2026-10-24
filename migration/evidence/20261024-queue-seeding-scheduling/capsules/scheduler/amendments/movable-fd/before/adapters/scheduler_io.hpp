#pragma once
#include "profile.hpp"
#include "file_io.hpp"
#include <stop_token>
#include <sys/stat.h>

namespace biglybtc::storage::scheduler_detail {
struct Failure { Error error; };
[[noreturn]] void fail(ErrorCode, const char*);
[[noreturn]] void io(const char*);
void checkStop(std::stop_token);
bool same(const struct stat&, const struct stat&) noexcept;
struct Raw {
  std::optional<std::string> bytes;
  struct stat identity{};
};
Raw read(int directory, const char* name, std::size_t maximum);
void unchanged(int directory, const char* name, const Raw&, std::size_t maximum);
detail::Descriptor directory(int parent, const char* name, bool missingAllowed = false);
void directoryIdentity(int parent, const char* name, int held);
// Cooperative held-lease CAS; not atomic against noncooperating final-nanosecond
// writers. Fixed exclusive temporary is deliberately retained on uncertain IO.
Raw replace(int directory, const char* name, const Raw&, std::string_view, std::size_t maximum);
void remove(int directory, const char* name, const Raw&, std::size_t maximum);
std::string sha256(int fd, std::uint64_t expected);
}
