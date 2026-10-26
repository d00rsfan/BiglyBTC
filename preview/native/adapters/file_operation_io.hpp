#pragma once
#include "file_operations.hpp"
#include "file_io.hpp"
#include <array>
#include <sys/stat.h>

namespace biglybtc::storage::fileop {
using Fd=detail::Descriptor;
struct Failure { Error error; };
[[noreturn]] void fail(ErrorCode,std::string);
void require(bool,ErrorCode,const char*);
void sync(int);
void pathShape(const std::filesystem::path&);
Fd directory(const std::filesystem::path&,bool create=false,bool missing=false);
std::string profilePath(const ProfileLease&);
bool identity(const struct stat&,const struct stat&);
bool unchanged(const struct stat&,const struct stat&);
std::optional<struct stat> inspect(const std::filesystem::path&);
struct Raw {std::optional<std::string> bytes;std::optional<struct stat> stamp;};
Raw read(const std::filesystem::path&,std::size_t);
void check(const std::filesystem::path&,const Raw&,std::size_t);
void publish(const std::filesystem::path&,const Raw&,std::string_view,std::size_t);
void erase(const std::filesystem::path&,const struct stat&);
std::string digest(std::string_view);
std::string digestFile(const std::filesystem::path&,const struct stat&,std::stop_token={});
// Process-local SHA receipts. Never serialized or trusted after recovery. A hit
// requires the named inode AND its retained descriptor to match the complete
// post-hash stat (including ctime and link count). Metadata changes force a read.
struct PayloadDigest {
  Fd descriptor;
  struct stat stamp{};
  std::string sha256;
};
class PayloadDigestCache final {
public:
  static constexpr std::size_t Capacity=8;
  std::string digest(const std::filesystem::path&,const struct stat&,std::stop_token={});
  void clear() noexcept {for(auto& proof:proofs_){proof.reset();}next_=0;}
private:
  std::array<std::optional<PayloadDigest>,Capacity> proofs_{};
  std::size_t next_=0;
};
}
