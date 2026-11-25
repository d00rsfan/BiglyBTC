// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "profile.hpp"
#include <memory>

struct evp_md_ctx_st;

namespace biglybtc::storage::fileop {

struct Failure { Error error; };
[[noreturn]] void fail(ErrorCode, std::string);
void require(bool, ErrorCode, const char*);

struct FreeDigestContext { void operator()(evp_md_ctx_st*) const noexcept; };
using Sha256Context = std::unique_ptr<evp_md_ctx_st, FreeDigestContext>;
Sha256Context beginSha256();
std::string finishSha256(Sha256Context&);
std::string digest(std::string_view);

} // namespace biglybtc::storage::fileop
