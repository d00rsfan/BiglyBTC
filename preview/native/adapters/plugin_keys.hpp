// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/bencode.hpp"
namespace biglybtc::storage::detail {
// Default original BDecoder keys are Java ISO-8859-1 strings. Requested UTF-8
// host names must match that representation without repairing saved key bytes.
const core::Node* findOriginalKey(const core::Node& dictionary, std::string_view utf8);
}
