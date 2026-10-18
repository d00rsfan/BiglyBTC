#pragma once

#include "biglybtc/bencode.hpp"

#include <expected>
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
  std::expected<std::string, CodecError> serialize() const;
private:
  EncodedDocument(core::Document document, std::string original, bool compressed, core::Limits limits)
      : document_(std::move(document)), original_(std::move(original)), compressed_(compressed), limits_(limits) {}
  core::Document document_;
  std::string original_;
  bool compressed_;
  core::Limits limits_;
};

} // namespace biglybtc::storage
