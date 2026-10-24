#pragma once
#include "profile.hpp"
#include "file_io.hpp"
#include <stop_token>
#include <utility>
#include <unistd.h>
#include <sys/stat.h>

namespace biglybtc::storage::scheduler_detail {
// Private owning fd; unlike shared storage::detail::Descriptor this service
// needs move assignment when optional directories first become present.
class Descriptor final {
public:
  explicit Descriptor(int value = -1) noexcept : value_(value) {}
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  Descriptor(Descriptor&& other) noexcept : value_(other.release()) {}
  Descriptor& operator=(Descriptor&& other) noexcept {
    if(this!=&other){if(value_>=0){::close(value_);}value_=other.release();}return *this;
  }
  ~Descriptor(){if(value_>=0){::close(value_);}}
  int get() const noexcept{return value_;}
  int release() noexcept{return std::exchange(value_,-1);}
private:
  int value_;
};
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
scheduler_detail::Descriptor directory(int parent, const char* name, bool missingAllowed = false);
void directoryIdentity(int parent, const char* name, int held);
// Cooperative held-lease CAS; not atomic against noncooperating final-nanosecond
// writers. Fixed exclusive temporary is deliberately retained on uncertain IO.
Raw replace(int directory, const char* name, const Raw&, std::string_view, std::size_t maximum);
void remove(int directory, const char* name, const Raw&, std::size_t maximum);
std::string sha256(int fd, std::uint64_t expected);
}
