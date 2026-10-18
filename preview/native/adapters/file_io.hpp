#pragma once

#include "profile.hpp"

#include <sys/types.h>

namespace biglybtc::storage::detail {

class Descriptor final {
public:
  explicit Descriptor(int value) : value_(value) {}
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  Descriptor(Descriptor&& other) noexcept;
  Descriptor& operator=(Descriptor&& other) noexcept;
  ~Descriptor();
  int get() const { return value_; }
  int release();
private:
  int value_;
};

std::expected<std::optional<std::string>, Error> readFile(int directory,
    const std::string& name, std::size_t maximum);
std::expected<void, Error> writeExclusive(int directory, const std::string& name,
    std::string_view data, mode_t mode);

} // namespace biglybtc::storage::detail
