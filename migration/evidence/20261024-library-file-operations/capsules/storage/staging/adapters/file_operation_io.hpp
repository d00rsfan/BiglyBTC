#pragma once
#include "file_operations.hpp"
#include "file_io.hpp"
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
}
