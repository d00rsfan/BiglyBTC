#include "encoded_document.hpp"

#include <zlib.h>

#include <array>
#include <limits>
#include <string_view>

namespace biglybtc::storage {
namespace {

class Stream final {
public:
  explicit Stream(bool encoding) : encoding_(encoding) {}
  Stream(const Stream&) = delete;
  Stream& operator=(const Stream&) = delete;
  ~Stream() {
    if (initialized_) {
      if (encoding_) { ::deflateEnd(&value); } else { ::inflateEnd(&value); }
    }
  }
  bool initialize() {
    const auto result = encoding_ ? ::deflateInit2(&value, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
        15 + 16, 8, Z_DEFAULT_STRATEGY) : ::inflateInit2(&value, 15 + 16);
    initialized_ = result == Z_OK;
    return initialized_;
  }
  z_stream value{};
private:
  bool encoding_;
  bool initialized_ = false;
};

std::expected<std::string, CodecError> transform(std::string_view input, std::size_t maximum, bool encoding) {
  if (input.size() > std::numeric_limits<uInt>::max()) {
    return std::unexpected(CodecError::Limit);
  }
  Stream stream(encoding);
  if (!stream.initialize()) { return std::unexpected(CodecError::Unavailable); }
  stream.value.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
  stream.value.avail_in = static_cast<uInt>(input.size());
  std::array<char, 16384> buffer{};
  std::string output;
  unsigned members = 1;
  while (true) {
    stream.value.next_out = reinterpret_cast<Bytef*>(buffer.data());
    stream.value.avail_out = static_cast<uInt>(buffer.size());
    const auto status = encoding ? ::deflate(&stream.value, Z_FINISH) : ::inflate(&stream.value, Z_NO_FLUSH);
    const auto produced = buffer.size() - stream.value.avail_out;
    if (produced > maximum - output.size()) { return std::unexpected(CodecError::Limit); }
    output.append(buffer.data(), produced);
    if (status == Z_STREAM_END) {
      if (stream.value.avail_in == 0) { return output; }
      // Java GZIPInputStream also reads concatenated members. Bound their count;
      // arbitrary trailing bytes must not become a second partial document.
      if (encoding || members == 16) { return std::unexpected(CodecError::Limit); }
      const auto next = stream.value.next_in;
      const auto remaining = stream.value.avail_in;
      if (::inflateReset2(&stream.value, 15 + 16) != Z_OK) {
        return std::unexpected(CodecError::Unavailable);
      }
      stream.value.next_in = next;
      stream.value.avail_in = remaining;
      ++members;
    } else if (status != Z_OK || (produced == 0 && stream.value.avail_in == 0)) {
      return std::unexpected(CodecError::InvalidGzip);
    }
  }
}
} // namespace

std::expected<EncodedDocument, CodecError> EncodedDocument::parse(std::string input, core::Limits limits) {
  if (input.size() > limits.inputBytes) { return std::unexpected(CodecError::Limit); }
  const bool gzip = input.size() >= 2 && static_cast<unsigned char>(input[0]) == 0x1f
      && static_cast<unsigned char>(input[1]) == 0x8b;
  if (!gzip) {
    auto parsed = core::Document::parse(std::move(input), limits);
    if (!parsed) { return std::unexpected(CodecError::InvalidBencode); }
    return EncodedDocument(std::move(*parsed), {}, false, limits);
  }
  auto decoded = transform(input, limits.inputBytes, false);
  if (!decoded) { return std::unexpected(decoded.error()); }
  auto parsed = core::Document::parse(std::move(*decoded), limits);
  if (!parsed) { return std::unexpected(CodecError::InvalidBencode); }
  return EncodedDocument(std::move(*parsed), std::move(input), true, limits);
}

std::expected<std::string, CodecError> EncodedDocument::serialize() const {
  if (compressed_ && !document_.root().changed) { return original_; }
  auto plain = document_.serialize();
  if (plain.size() > limits_.inputBytes) { return std::unexpected(CodecError::Limit); }
  if (!compressed_) { return plain; }
  return transform(plain, limits_.inputBytes, true);
}

} // namespace biglybtc::storage
