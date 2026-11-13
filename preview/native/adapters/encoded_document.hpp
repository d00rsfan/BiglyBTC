#pragma once

#include "biglybtc/bencode.hpp"

#include <expected>
#include <limits>
#include <string>

namespace biglybtc::storage {

enum class CodecError { Limit, InvalidGzip, InvalidBencode, Unavailable };

class EncodedDocument final {
public:
  static std::expected<EncodedDocument, CodecError> parse(std::string input, core::Limits limits = {});
  const core::Document& document() const { return document_; }
  bool setInteger(std::string_view key, std::int64_t value) { return document_.setInteger(key, value); }
  std::expected<void, core::EditError> replaceValue(std::string_view key, std::string encoded) {
    return document_.replaceValue(key, std::move(encoded));
  }
  bool compressed() const { return compressed_; }
  const std::string& originalBytes() const { return compressed_ ? *original_ : document_.originalBytes(); }
  // Fresh parsed snapshots only: count decoded tree plus compressed backing.
  std::size_t storageBytes() const noexcept {
    const auto decoded=document_.storageBytes();
    const auto encoded=compressed_?original_->capacity():0;
    constexpr auto maximum=std::numeric_limits<std::size_t>::max();
    const auto overhead=sizeof(EncodedDocument)+sizeof(std::string)+32;
    if(decoded>maximum-overhead||encoded>maximum-overhead-decoded){return maximum;}
    return decoded+encoded+overhead;
  }
  std::expected<std::string, CodecError> serialize() const;
private:
  EncodedDocument(core::Document document, std::string original, bool compressed, core::Limits limits)
      : document_(std::move(document)), original_(compressed?std::make_shared<const std::string>(std::move(original)):nullptr), compressed_(compressed), limits_(limits) {}
  core::Document document_;
  std::shared_ptr<const std::string> original_;
  bool compressed_;
  core::Limits limits_;
};

} // namespace biglybtc::storage
