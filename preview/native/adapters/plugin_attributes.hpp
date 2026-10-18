// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/bencode.hpp"
#include <cstdint>
#include <expected>
namespace biglybtc::storage {
enum class AttributeError { Root, CacheRoot, CachedMapType, LongType, Binding, AttributeMapType };
enum class AttributeSource { Default, Primary, Cache };
struct OriginalLongAttribute { std::int64_t value; AttributeSource source; };
// Read a selected runtime-ID attribute map, including cached-only or archived
// maps, without requiring/opening a primary state file. Null means no attributes.
std::expected<std::int64_t, AttributeError> readOriginalPluginLongMap(
    const core::Node* attributes, std::string_view runtimeId, std::string_view attributeName);
// Pure snapshot lookup; inputs must remain stable for the duration of the call.
// Empty cached maps replace the whole primary map. Cached entry selection and
// filesystem/profile ownership are supplied by the host, not inferred here.
std::expected<OriginalLongAttribute, AttributeError> readOriginalPluginLongAttribute(
    const core::Document& primaryState, const core::Node* cachedEntry,
    std::string_view runtimeId, std::string_view attributeName);
}
