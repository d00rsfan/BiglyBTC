// SPDX-License-Identifier: GPL-2.0-or-later
#include "file_operation_digest.hpp"

#include <array>
#include <openssl/evp.h>

namespace biglybtc::storage::fileop {

[[noreturn]] void fail(ErrorCode code, std::string text) {
  throw Failure{{code, std::move(text)}};
}

void require(bool ok, ErrorCode code, const char* text) {
  if (!ok) { fail(code, text); }
}

void FreeDigestContext::operator()(evp_md_ctx_st* context) const noexcept { EVP_MD_CTX_free(context); }

Sha256Context beginSha256() {
  Sha256Context context(EVP_MD_CTX_new());
  require(context && EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) == 1,
      ErrorCode::Io, "Cannot initialize file-operation digest");
  return context;
}

std::string finishSha256(Sha256Context& context) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> raw{};
  unsigned size = 0;
  require(EVP_DigestFinal_ex(context.get(), raw.data(), &size) == 1 && size == 32,
      ErrorCode::Io, "Cannot finish file-operation digest");
  constexpr char digits[] = "0123456789abcdef";
  std::string result(64, '0');
  for (std::size_t n = 0; n < size; ++n) {
    result[n * 2] = digits[raw[n] >> 4];
    result[n * 2 + 1] = digits[raw[n] & 15];
  }
  return result;
}

std::string digest(std::string_view bytes) {
  auto context = beginSha256();
  require(EVP_DigestUpdate(context.get(), bytes.data(), bytes.size()) == 1,
      ErrorCode::Io, "Cannot hash file-operation scope");
  return finishSha256(context);
}

} // namespace biglybtc::storage::fileop
