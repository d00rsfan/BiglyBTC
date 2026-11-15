// SPDX-License-Identifier: GPL-2.0-or-later
// Original storage policy: Copyright (C) Azureus Software, Inc.
#pragma once
#include <biglybtc/bencode.hpp>
#include <QHostAddress>
#include <optional>
namespace biglybtc::services::azureus_identity {
enum class Error {Input,Limit,Resources};
inline constexpr std::size_t maximumAddresses=8192;
std::expected<std::optional<std::string>,Error> mostRecent(const core::Node&);
std::expected<bool,Error> isRecent(const core::Node&,std::string_view address);
// Only writing prunes old entries. No file/socket/identity effect is granted.
std::expected<std::string,Error> remember(const core::Node&,std::string_view address,std::int64_t wallMilliseconds);
std::expected<std::string,Error> originalHostAddress(const QHostAddress&);
}
