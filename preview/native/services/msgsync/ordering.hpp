// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "message.hpp"
namespace biglybtc::services::msgsync {
struct DisplayMessage {MessageId id{};std::optional<MessageId> previous;std::int64_t sequence=0,timestamp=0;std::uint64_t ordinal=0;};
// Original BuddyPluginBeta previous-message trees, sequence/time merges and
// local arrival tie-break. Input is the preceding display order + new arrivals.
Result<std::vector<std::size_t>> displayMessageOrder(std::span<const DisplayMessage>);
}
