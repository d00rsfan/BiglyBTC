#include "encoded_document.hpp"

#include <charconv>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace biglybtc;
using namespace biglybtc::storage;

namespace {
void require(bool condition) {
  if (!condition) { throw std::runtime_error("Compressed envelope check failed"); }
}
std::string hex(std::string_view value) {
  std::string bytes;
  for (std::size_t i = 0; i < value.size(); i += 2) {
    unsigned byte = 0;
    auto decoded = std::from_chars(value.data() + i, value.data() + i + 2, byte, 16);
    require(decoded.ec == std::errc{});
    bytes += static_cast<char>(byte);
  }
  return bytes;
}
} // namespace

int main() {
  try {
    // Fixed gzip fixtures generated with Python's independent standard library.
    const auto gzip = hex("1f8b08000000000002ff4b31b44acc344c35b72acdcbcecb2fcf33b162f8df509f0a002122e6fe17000000");
    const auto plain = std::string("d1:ai1e7:unknown4:") + std::string("\0\xff\x80\x7f", 4) + "e";
    auto envelope = EncodedDocument::parse(gzip);
    require(envelope && envelope->compressed());
    require(envelope->document().originalBytes() == plain);
    require(envelope->serialize() == gzip);
    require(envelope->replaceValue("a", "i1e").has_value() && envelope->serialize() == gzip);
    auto copied = *envelope;
    require(copied.setInteger("a", std::numeric_limits<std::int64_t>::min()));
    const auto encoded = copied.serialize();
    require(encoded.has_value());
    const auto decoded = EncodedDocument::parse(*encoded);
    require(decoded && decoded->document().find("a")->integer == std::numeric_limits<std::int64_t>::min());
    const auto expected = std::string("d1:ai-9223372036854775808e7:unknown4:") + std::string("\0\xff\x80\x7f", 4) + "e";
    require(decoded->document().originalBytes() == expected);
    require(envelope->serialize() == gzip);
    auto uncompressed = EncodedDocument::parse(plain);
    require(uncompressed && !uncompressed->compressed() && uncompressed->serialize() == plain);
    for (std::size_t length = 0; length < gzip.size(); ++length) {
      require(!EncodedDocument::parse(gzip.substr(0, length)));
    }
    auto corrupt = gzip;
    corrupt[corrupt.size() - 8] ^= 1;
    require(!EncodedDocument::parse(corrupt));
    require(!EncodedDocument::parse(gzip + "junk"));
    const auto bomb = hex("1f8b08000000000002ff4b31b44a34343030b0721c05a360140c7b900a0043806b23f2030000");
    const auto limited = EncodedDocument::parse(bomb, core::Limits{.inputBytes = 100});
    require(!limited && limited.error() == CodecError::Limit);
    const auto first = hex("1f8b08000000000002ff4b31b44acc0400085a738305000000");
    const auto second = hex("1f8b08000000000002ff334c4d0500805cbaff03000000");
    require(!EncodedDocument::parse(first));
    auto members = EncodedDocument::parse(first + second);
    require(members && members->document().originalBytes() == "d1:ai1ee" && members->serialize() == first + second);
    std::string tooMany;
    for (int i = 0; i < 17; ++i) { tooMany += first; }
    auto rejected = EncodedDocument::parse(tooMany);
    require(!rejected && rejected.error() == CodecError::Limit);
    auto small = EncodedDocument::parse("d1:ai1ee", core::Limits{.inputBytes = 8});
    require(small && !small->setInteger("a", std::numeric_limits<std::int64_t>::max()));
    require(small->serialize() == "d1:ai1ee");
    std::cout << "PASS plain/gzip identity, binary edits, copy lifetime, CRC/truncation, quotas and concatenated members\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
