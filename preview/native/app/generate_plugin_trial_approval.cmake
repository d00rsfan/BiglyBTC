# SPDX-License-Identifier: GPL-2.0-or-later
file(SHA256 "${INPUT}" HELPER_SHA256)
file(CONFIGURE OUTPUT "${OUTPUT}" CONTENT [=[
#pragma once
namespace biglybtc::app {
inline constexpr char NativePluginPublicKey[] = "@PUBLIC_KEY@";
inline constexpr char PluginTrialHelperDigest[] = "@HELPER_SHA256@";
}
]=] @ONLY)
