// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "profile.hpp"

namespace biglybtc::update::detail {

struct OriginalJarDescriptor {
  std::string properties;
  std::string identity;
  std::optional<std::string> fallbackId;
};

// Reads one unambiguous root descriptor from the selected original JARs.
std::expected<std::optional<OriginalJarDescriptor>, storage::Error> readOriginalJarDescriptor(
    int directory, std::string_view directoryName);

// A loose descriptor without an ID also uses the selected version prefix or directory name.
std::expected<std::optional<std::string>, storage::Error> originalPluginIdFallback(
    int directory, std::string_view directoryName);

} // namespace biglybtc::update::detail
